/*
 *  Lockstep scheduler: command delay buffer, host relay, sim advance barrier.
 */

#define MEM_FILE_SYS MEM_SYS_LOCKSTEP
#define MEM_FILE_SUB 0

#include "public/lockstep.h"
#include "../game/public/game.h"
#include "../game/combat.h"
#include "../lib/public/pf_string.h"
#include "../mem.h"
#include "../session.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <SDL.h>

/* Internal hooks from other lockstep TUs */
void LS_SimClock_Reset(uint32_t hz);
void LS_SimClock_Advance(void);
void LS_SimClock_SetTick(uint32_t tick);
void LS_ApplyPlayerFrame(const struct ls_player_frame *frame);
size_t LS_CmdSerialize(const struct ls_player_frame *frame, uint8_t *out, size_t out_cap);
bool LS_CmdDeserialize(const uint8_t *data, size_t len, struct ls_player_frame *out);
void LS_ReplayRecordFrame(const struct ls_player_frame *frame);
bool LS_ReplayNextFrame(struct ls_player_frame *out);
void LS_ReplayStop(void);

#pragma pack(push, 1)
struct ls_net_hdr{
    uint8_t  type;
    uint8_t  player_id;
    uint16_t payload_len;
    uint32_t tick;
};
#pragma pack(pop)

struct frame_slot{
    bool                   present[LS_MAX_PLAYERS];
    struct ls_player_frame frames[LS_MAX_PLAYERS];
};

static enum game_net_mode s_mode = GAME_MODE_SP;
static struct ls_config   s_cfg;
static bool               s_active;
static bool               s_desync;
static bool               s_sim_started; /* false until READY/GO barrier clears */
static bool               s_go_sent;     /* host: GO already broadcast */
static bool               s_start_armed; /* GO prepare seen — stop READY spam */
static bool               s_peer_alive[LS_MAX_PLAYERS];
static bool               s_peer_ready[LS_MAX_PLAYERS];
static bool               s_peer_go_ack[LS_MAX_PLAYERS];

/* Commands queued for execution at execute_tick = issue_tick + delay */
static struct ls_player_frame s_pending_local;
static bool                   s_pending_local_dirty;

static struct frame_slot s_buffer[LS_CMD_BUFFER_FRAMES];
static uint32_t          s_exec_tick; /* next tick to execute when complete */
static uint32_t          s_issue_tick; /* tick currently accepting local cmds */
static uint32_t          s_last_sim_wall_ms;
static uint32_t          s_barrier_wait_start_ms;
static uint32_t          s_last_ready_ms;
static uint64_t          s_last_hashes[LS_MAX_PLAYERS];
static bool              s_hash_got[LS_MAX_PLAYERS];

static uint32_t buf_index(uint32_t tick)
{
    return tick % LS_CMD_BUFFER_FRAMES;
}

void LS_SetMode(enum game_net_mode mode)
{
    s_mode = mode;
}

enum game_net_mode LS_GetMode(void)
{
    return s_mode;
}

bool LS_IsLockstep(void)
{
    return s_mode == GAME_MODE_MP_LOCKSTEP && s_active;
}

static void clear_buffer(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

bool LS_Init(const struct ls_config *cfg)
{
    if(!cfg)
        return false;
    LS_Shutdown();
    s_cfg = *cfg;
    if(s_cfg.sim_hz == 0)
        s_cfg.sim_hz = LS_DEFAULT_HZ;
    if(s_cfg.command_delay == 0)
        s_cfg.command_delay = LS_DEFAULT_DELAY;
    if(s_cfg.num_players < 1)
        s_cfg.num_players = 1;
    if(s_cfg.num_players > LS_MAX_PLAYERS)
        s_cfg.num_players = LS_MAX_PLAYERS;

    LS_SimClock_Reset(s_cfg.sim_hz);
    LS_RngSeed(s_cfg.rng_seed);
    clear_buffer();
    s_exec_tick = 0;
    s_issue_tick = s_cfg.command_delay;
    s_pending_local_dirty = false;
    memset(&s_pending_local, 0, sizeof(s_pending_local));
    s_desync = false;
    s_sim_started = s_cfg.local_only || (s_cfg.num_players <= 1);
    s_go_sent = false;
    s_start_armed = s_sim_started;
    s_last_sim_wall_ms = SDL_GetTicks();
    s_barrier_wait_start_ms = 0;
    s_last_ready_ms = 0;
    s_active = true;
    s_mode = GAME_MODE_MP_LOCKSTEP;

    for(int i = 0; i < LS_MAX_PLAYERS; i++) {
        s_peer_alive[i] = (i < s_cfg.num_players);
        s_peer_ready[i] = false;
        s_peer_go_ack[i] = false;
    }
    if(s_cfg.local_player_id >= 0 && s_cfg.local_player_id < LS_MAX_PLAYERS) {
        s_peer_ready[s_cfg.local_player_id] = true;
        s_peer_go_ack[s_cfg.local_player_id] = true; /* host counts itself */
    }

    /* Prefill delay frames with empty commands so sim can start */
    for(uint32_t t = 0; t < s_cfg.command_delay; t++) {
        struct frame_slot *slot = &s_buffer[buf_index(t)];
        for(int p = 0; p < s_cfg.num_players; p++) {
            slot->present[p] = true;
            slot->frames[p].tick = t;
            slot->frames[p].player_id = (uint8_t)p;
            slot->frames[p].cmd_count = 0;
        }
    }

    /* Announce ready so host can release the start barrier together. */
    if(!s_sim_started && !s_cfg.local_only) {
        uint8_t buf[sizeof(struct ls_net_hdr)];
        struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
        h->type = LS_NET_READY;
        h->player_id = (uint8_t)s_cfg.local_player_id;
        h->tick = 0;
        h->payload_len = 0;
        Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
        fprintf(stderr, "[lockstep] READY sent local=%d\n", s_cfg.local_player_id);
    }
    return true;
}

static bool all_peers_ready(void)
{
    for(int p = 0; p < s_cfg.num_players; p++) {
        if(!s_peer_alive[p])
            continue;
        if(!s_peer_ready[p])
            return false;
    }
    return true;
}

static void reset_sim_clocks(void)
{
    s_exec_tick = 0;
    s_issue_tick = s_cfg.command_delay;
    s_last_sim_wall_ms = SDL_GetTicks();
    s_barrier_wait_start_ms = 0;
    clear_buffer();
    for(uint32_t t = 0; t < s_cfg.command_delay; t++) {
        struct frame_slot *slot = &s_buffer[buf_index(t)];
        for(int p = 0; p < s_cfg.num_players; p++) {
            slot->present[p] = true;
            slot->frames[p].tick = t;
            slot->frames[p].player_id = (uint8_t)p;
            slot->frames[p].cmd_count = 0;
        }
    }
}

static void begin_sim(void)
{
    if(s_sim_started)
        return;
    reset_sim_clocks();
    s_sim_started = true;
    fprintf(stderr, "[lockstep] sim START\n");
}

static bool all_go_acked(void)
{
    for(int p = 0; p < s_cfg.num_players; p++) {
        if(!s_peer_alive[p])
            continue;
        if(!s_peer_go_ack[p])
            return false;
    }
    return true;
}

static void send_go_ack(void)
{
    uint8_t buf[sizeof(struct ls_net_hdr)];
    struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
    h->type = LS_NET_GO_ACK;
    h->player_id = (uint8_t)s_cfg.local_player_id;
    h->tick = 0;
    h->payload_len = 0;
    Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
}

static void maybe_host_release_go(void)
{
    if(!s_cfg.is_host || s_sim_started || !all_peers_ready())
        return;
    /* Tell clients to reset clocks first; host waits for GO_ACK so we do not
     * emit CMD frames that arrive before the client's clear_buffer(). */
    if(!s_go_sent) {
        s_go_sent = true;
        s_start_armed = true;
        uint8_t buf[sizeof(struct ls_net_hdr)];
        struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
        h->type = LS_NET_GO;
        h->player_id = (uint8_t)s_cfg.local_player_id;
        h->tick = 0;
        h->payload_len = 0;
        Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
        fprintf(stderr, "[lockstep] GO sent — waiting for acks\n");
    }
    if(all_go_acked()) {
        begin_sim();
        /* Signal clients to begin; they already reset clocks on GO. */
        send_go_ack();
    }
}

void LS_Shutdown(void)
{
    s_active = false;
    clear_buffer();
    LS_ReplayEndRecord();
    LS_ReplayStop();
}

void LS_Reset(void)
{
    if(s_active) {
        struct ls_config cfg = s_cfg;
        LS_Init(&cfg);
    }
}

bool LS_CanControlEntity(uint32_t uid)
{
    if(!G_EntityExists(uid))
        return false;
    if(!LS_IsLockstep())
        return true;
    int fac = G_GetFactionID(uid);
    if(fac < 0 || s_cfg.local_player_id < 0)
        return false;
    int pid = s_cfg.local_player_id;
    if(pid >= LS_MAX_PLAYERS)
        return false;
    /* Prefer explicit faction binding; else player_id == faction_id (demos). */
    int want = s_cfg.faction_ids[pid];
    if(want == 0)
        want = pid;
    return fac == want;
}

bool LS_IssueCmd(const struct ls_cmd *cmd)
{
    if(!cmd)
        return false;
    if(!LS_IsLockstep()) {
        /* SP passthrough: apply immediately via a one-shot frame */
        struct ls_player_frame fr;
        memset(&fr, 0, sizeof(fr));
        fr.cmd_count = 1;
        fr.cmds[0] = *cmd;
        LS_ApplyPlayerFrame(&fr);
        return true;
    }
    if(s_pending_local.cmd_count >= LS_MAX_CMDS_PER_FRAME)
        return false;

    /* Validate entities belong to local player */
    for(int i = 0; i < cmd->ent_count; i++) {
        if(!LS_CanControlEntity(cmd->ents[i]))
            return false;
    }
    s_pending_local.cmds[s_pending_local.cmd_count++] = *cmd;
    s_pending_local_dirty = true;
    return true;
}

static void store_frame(const struct ls_player_frame *fr)
{
    if(fr->player_id >= LS_MAX_PLAYERS)
        return;
    struct frame_slot *slot = &s_buffer[buf_index(fr->tick)];
    slot->frames[fr->player_id] = *fr;
    slot->present[fr->player_id] = true;
    LS_ReplayRecordFrame(fr);
}

static bool slot_complete(uint32_t tick)
{
    struct frame_slot *slot = &s_buffer[buf_index(tick)];
    for(int p = 0; p < s_cfg.num_players; p++) {
        if(!s_peer_alive[p])
            continue;
        if(!slot->present[p])
            return false;
    }
    return true;
}

static void send_frame_net(const struct ls_player_frame *fr)
{
    if(s_cfg.local_only)
        return;
    uint8_t buf[NET_MAX_PACKET];
    struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
    h->type = LS_NET_CMD_FRAME;
    h->player_id = fr->player_id;
    h->tick = fr->tick;
    size_t body = LS_CmdSerialize(fr, buf + sizeof(*h), sizeof(buf) - sizeof(*h));
    if(!body)
        return;
    h->payload_len = (uint16_t)body;
    size_t total = sizeof(*h) + body;
    Net_Broadcast(NET_CH_RELIABLE, buf, total);
}

static void send_hash(uint32_t tick, uint64_t hash)
{
    if(s_cfg.local_only)
        return;
    uint8_t buf[sizeof(struct ls_net_hdr) + sizeof(uint64_t)];
    struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
    h->type = LS_NET_HASH;
    h->player_id = (uint8_t)s_cfg.local_player_id;
    h->tick = tick;
    h->payload_len = sizeof(uint64_t);
    memcpy(buf + sizeof(*h), &hash, sizeof(hash));
    Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
}

static void handle_net_payload(int peer, const uint8_t *data, size_t len)
{
    (void)peer;
    if(len < sizeof(struct ls_net_hdr))
        return;
    const struct ls_net_hdr *h = (const struct ls_net_hdr *)data;
    const uint8_t *payload = data + sizeof(*h);
    if(sizeof(*h) + h->payload_len > len)
        return;

    switch(h->type) {
    case LS_NET_CMD_FRAME: {
        if(!s_sim_started)
            break; /* drop frames that raced ahead of GO reset */
        struct ls_player_frame fr;
        if(!LS_CmdDeserialize(payload, h->payload_len, &fr)) {
            fprintf(stderr, "[lockstep] CMD deserialize fail len=%u\n",
                    (unsigned)h->payload_len);
            break;
        }
        fr.tick = h->tick;
        fr.player_id = h->player_id;
        store_frame(&fr);
        if(s_cfg.is_host && h->player_id != (uint8_t)s_cfg.local_player_id)
            Net_Broadcast(NET_CH_RELIABLE, data, len);
        if((fr.tick % 10) == 0)
            fprintf(stderr, "[lockstep] recv CMD tick=%u from=%u cmds=%u\n",
                    fr.tick, fr.player_id, fr.cmd_count);
        break;
    }
    case LS_NET_HASH: {
        if(!s_sim_started)
            break;
        if(h->payload_len < sizeof(uint64_t) || h->player_id >= LS_MAX_PLAYERS)
            break;
        uint64_t remote;
        memcpy(&remote, payload, sizeof(remote));
        s_last_hashes[h->player_id] = remote;
        s_hash_got[h->player_id] = true;
        if(s_cfg.is_host) {
            uint64_t first = 0;
            bool have = false;
            for(int p = 0; p < s_cfg.num_players; p++) {
                if(!s_peer_alive[p] || !s_hash_got[p])
                    continue;
                if(!have) {
                    first = s_last_hashes[p];
                    have = true;
                }else if(s_last_hashes[p] != first) {
                    s_desync = true;
                    uint8_t dbuf[sizeof(struct ls_net_hdr)];
                    struct ls_net_hdr *dh = (struct ls_net_hdr *)dbuf;
                    dh->type = LS_NET_DESYNC;
                    dh->player_id = (uint8_t)p;
                    dh->tick = h->tick;
                    dh->payload_len = 0;
                    Net_Broadcast(NET_CH_RELIABLE, dbuf, sizeof(dbuf));
                    fprintf(stderr, "[lockstep] DESYNC at tick %u player %d\n",
                            h->tick, p);
                }
            }
        }
        break;
    }
    case LS_NET_DESYNC:
        s_desync = true;
        break;
    case LS_NET_PEER_LEFT:
        if(h->player_id < LS_MAX_PLAYERS)
            s_peer_alive[h->player_id] = false;
        break;
    case LS_NET_READY:
        if(h->player_id < LS_MAX_PLAYERS) {
            s_peer_ready[h->player_id] = true;
            fprintf(stderr, "[lockstep] READY from player=%u\n", h->player_id);
            if(s_cfg.is_host)
                maybe_host_release_go();
        }
        break;
    case LS_NET_GO:
        /* Prepare only — reset clocks, do not issue frames yet. */
        if(!s_sim_started) {
            s_start_armed = true;
            reset_sim_clocks();
            if(!s_cfg.is_host)
                send_go_ack();
        }
        break;
    case LS_NET_GO_ACK:
        if(h->player_id >= LS_MAX_PLAYERS)
            break;
        fprintf(stderr, "[lockstep] GO_ACK from player=%u\n", h->player_id);
        if(s_cfg.is_host) {
            /* Ignore our own start echo. */
            if(h->player_id != (uint8_t)s_cfg.local_player_id) {
                s_peer_go_ack[h->player_id] = true;
                maybe_host_release_go();
            }
        }else if(h->player_id == 0) {
            /* Host GO_ACK => begin together. */
            begin_sim();
        }
        break;
    default:
        break;
    }
}

static void pump_net(void)
{
    if(s_cfg.local_only)
        return;
    struct net_event ev;
    while(Net_Service(0, &ev)) {
        if(ev.type == NET_EVENT_RECEIVE)
            handle_net_payload(ev.peer_slot, ev.data, ev.data_len);
        else if(ev.type == NET_EVENT_DISCONNECT)
            LS_NotifyPeerDisconnected(ev.peer_slot);
        Net_FreeEvent(&ev);
    }
}

static void submit_local_frame(void)
{
    struct ls_player_frame fr;
    memset(&fr, 0, sizeof(fr));
    fr.tick = s_issue_tick;
    fr.player_id = (uint8_t)s_cfg.local_player_id;
    if(s_pending_local_dirty) {
        fr.cmd_count = s_pending_local.cmd_count;
        memcpy(fr.cmds, s_pending_local.cmds, sizeof(fr.cmds[0]) * fr.cmd_count);
    }
    memset(&s_pending_local, 0, sizeof(s_pending_local));
    s_pending_local_dirty = false;

    store_frame(&fr);
    send_frame_net(&fr);

    /* local_only multi-player fake: also inject empty frames for other players */
    if(s_cfg.local_only) {
        for(int p = 0; p < s_cfg.num_players; p++) {
            if(p == s_cfg.local_player_id)
                continue;
            struct ls_player_frame empty;
            memset(&empty, 0, sizeof(empty));
            empty.tick = s_issue_tick;
            empty.player_id = (uint8_t)p;
            store_frame(&empty);
        }
    }
    s_issue_tick++;
}

static void execute_tick(uint32_t tick)
{
    struct frame_slot *slot = &s_buffer[buf_index(tick)];
    /* Apply in player_id order for determinism */
    for(int p = 0; p < s_cfg.num_players; p++) {
        if(!s_peer_alive[p])
            continue;
        assert(slot->present[p]);
        LS_ApplyPlayerFrame(&slot->frames[p]);
        slot->present[p] = false;
    }
    LS_SimClock_Advance();

    if((tick % LS_HASH_INTERVAL) == 0) {
        uint64_t h = LS_ComputeStateHash();
        s_last_hashes[s_cfg.local_player_id] = h;
        s_hash_got[s_cfg.local_player_id] = true;
        send_hash(tick, h);
    }
}

bool LS_TryAdvanceSim(void)
{
    if(!s_active)
        return false;
    /* Desync is diagnostic-only — do not freeze the barrier. */

    pump_net();

    if(!s_sim_started) {
        /* Re-broadcast READY only until GO prepare — later spam fills the
         * reliable send queue and silently drops CMD frames. */
        uint32_t now_r = SDL_GetTicks();
        if(!s_start_armed && (now_r - s_last_ready_ms > 200)) {
            s_last_ready_ms = now_r;
            uint8_t buf[sizeof(struct ls_net_hdr)];
            struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
            h->type = LS_NET_READY;
            h->player_id = (uint8_t)s_cfg.local_player_id;
            h->tick = 0;
            h->payload_len = 0;
            Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
        }
        if(s_cfg.is_host)
            maybe_host_release_go();
        return false;
    }

    uint32_t now = SDL_GetTicks();
    uint32_t period = 1000 / s_cfg.sim_hz;
    if(period == 0)
        period = 100;
    if(now - s_last_sim_wall_ms < period)
        return false;
    s_last_sim_wall_ms = now;

    /*
     * Cap how far issue may run ahead of exec. Previously we submitted a new
     * local frame every period even while the barrier was stalled, so
     * issue_tick raced away, wrapped the 128-slot buffer, and clobbered
     * pending orders — right-click appeared to do nothing.
     */
    uint32_t max_issue = s_exec_tick + s_cfg.command_delay + 3;
    if(s_issue_tick <= max_issue)
        submit_local_frame();

    if(!slot_complete(s_exec_tick)) {
        if(s_barrier_wait_start_ms == 0)
            s_barrier_wait_start_ms = now;
        /* Wait longer before inventing empty frames — short timeouts used to
         * discard real peer orders that were still in the UDP drain path. */
        if(now - s_barrier_wait_start_ms <= 500)
            return false;
        /* Peer frame missing: inject empty so local orders still apply. */
        struct frame_slot *slot = &s_buffer[buf_index(s_exec_tick)];
        for(int p = 0; p < s_cfg.num_players; p++) {
            if(!s_peer_alive[p] || slot->present[p])
                continue;
            memset(&slot->frames[p], 0, sizeof(slot->frames[p]));
            slot->frames[p].tick = s_exec_tick;
            slot->frames[p].player_id = (uint8_t)p;
            slot->present[p] = true;
            fprintf(stderr, "[lockstep] timeout empty frame tick=%u player=%d\n",
                    s_exec_tick, p);
        }
        s_barrier_wait_start_ms = 0;
    } else {
        s_barrier_wait_start_ms = 0;
    }

    if(!slot_complete(s_exec_tick))
        return false;

    execute_tick(s_exec_tick);
    s_exec_tick++;
    return true;
}

void LS_Service(void)
{
    /*
     * Lobby and lockstep share the same UDP socket and overlapping msg type
     * ids (LOBBY_HELLO=1 == LS_NET_CMD_FRAME). Once lockstep is active the
     * lobby must NOT drain the socket — it was swallowing command frames and
     * freezing the sim barrier (orders never applied).
     */
    if(!s_active || s_mode != GAME_MODE_MP_LOCKSTEP) {
        Lobby_Service();
        return;
    }

    /* Catch-up: advance as many ticks as ready (cap to avoid spiral) */
    for(int i = 0; i < 3; i++) {
        if(!LS_TryAdvanceSim())
            break;
    }
}

bool LS_LastDesync(void)
{
    return s_desync;
}

void LS_ClearDesync(void)
{
    s_desync = false;
    memset(s_hash_got, 0, sizeof(s_hash_got));
}

bool LS_RequestSnapshotExport(const char *path)
{
    if(!path)
        return false;
    Session_RequestSave(path);
    return true;
}

bool LS_NotifyPeerDisconnected(int player_id)
{
    if(player_id < 0 || player_id >= LS_MAX_PLAYERS)
        return false;
    s_peer_alive[player_id] = false;
    if(!s_cfg.local_only) {
        uint8_t buf[sizeof(struct ls_net_hdr)];
        struct ls_net_hdr *h = (struct ls_net_hdr *)buf;
        h->type = LS_NET_PEER_LEFT;
        h->player_id = (uint8_t)player_id;
        h->tick = s_exec_tick;
        h->payload_len = 0;
        if(s_cfg.is_host)
            Net_Broadcast(NET_CH_RELIABLE, buf, sizeof(buf));
    }
    /* Mark missing frames for future ticks as empty for this player */
    return true;
}

bool LS_IsPeerConnected(int player_id)
{
    if(player_id < 0 || player_id >= LS_MAX_PLAYERS)
        return false;
    return s_peer_alive[player_id];
}
