#include "public/lockstep.h"
#include "protocol.h"
#include "kcp_udp.h"
#include "cmd_buffer.h"
#include "../sim/sim_clock.h"
#include "../sim/sim_hash.h"
#include "../sim/catchup.h"
#include "../game/timer_events.h"
#include "../game/movement.h"
#include "../game/combat.h"
#include "../game/public/game.h"

#include <string.h>
#include <stdio.h>
#include <SDL.h>

static enum ls_role s_role;
static bool         s_started;
static bool         s_got_welcome;
static int          s_player_id;
static int          s_num_players;
static uint32_t     s_seed;
static char         s_status[64];
static uint32_t     s_last_submit_ms;
static uint32_t     s_last_apply_ms;
static uint32_t     s_hello_ms;
static uint32_t     s_wait_since_ms;
static bool         s_desync;
static uint32_t     s_last_hash;
static int          s_host_assigned;
static int          s_peer_player[LS_MAX_PLAYERS];

static struct ls_cmd s_pending[LS_MAX_CMDS];
static int           s_npending;

static void set_status(const char *s)
{
    snprintf(s_status, sizeof(s_status), "%s", s);
}

static void reset_state(void)
{
    s_role = LS_ROLE_NONE;
    s_started = false;
    s_got_welcome = false;
    s_player_id = 0;
    s_num_players = 1;
    s_seed = 0;
    s_last_submit_ms = 0;
    s_last_apply_ms = 0;
    s_hello_ms = 0;
    s_wait_since_ms = 0;
    s_desync = false;
    s_last_hash = 0;
    s_host_assigned = 0;
    s_npending = 0;
    memset(s_pending, 0, sizeof(s_pending));
    memset(s_peer_player, 0xff, sizeof(s_peer_player)); /* -1 per int */
    SimClock_Reset();
    Catchup_Clear();
    CmdBuf_Reset(1);
    set_status("off");
}

static bool send_pkt(int peer, uint8_t type, uint32_t tick, uint8_t pid,
                     const uint8_t *pay, uint16_t plen, uint32_t checksum)
{
    struct ls_packet pkt;
    uint8_t buf[LS_MAX_PACKET];
    size_t n;

    memset(&pkt, 0, sizeof(pkt));
    pkt.msg_type = type;
    pkt.tick = tick;
    pkt.player_id = pid;
    pkt.checksum = checksum;
    pkt.payload_len = plen;
    if(plen)
        memcpy(pkt.payload, pay, plen);
    n = ls_pkt_encode(&pkt, buf, sizeof(buf));
    if(!n)
        return false;
    if(peer < 0)
        return KcpUdp_Broadcast(buf, (int)n);
    return KcpUdp_SendTo(peer, buf, (int)n);
}

static void prefill_delay(void)
{
    int t, p;
    CmdBuf_SetNumPlayers(s_num_players);
    for(t = 0; t < LS_CMD_DELAY; t++) {
        for(p = 0; p < s_num_players; p++)
            CmdBuf_Put((uint32_t)t, p, NULL, 0);
    }
}

static void begin_match(void)
{
    s_started = true;
    s_last_submit_ms = SDL_GetTicks();
    s_last_apply_ms = s_last_submit_ms;
    prefill_delay();
    G_Move_SetUseGPU(false);
    G_Timer_SetWallSim(false);
    set_status("playing");
    fprintf(stderr, "LS: match start seed=%u players=%d me=%d\n",
            s_seed, s_num_players, s_player_id);
}

static void queue_cmd(struct ls_cmd cmd)
{
    if(s_npending >= LS_MAX_CMDS)
        return;
    s_pending[s_npending++] = cmd;
}

static void submit_frame(void)
{
    uint8_t payload[LS_MAX_PAYLOAD];
    size_t n;
    uint32_t tick = SimClock_SubmitTick();

    n = ls_cmds_encode(s_pending, s_npending, payload, sizeof(payload));
    CmdBuf_Put(tick, s_player_id, s_pending, s_npending);

    if(s_role == LS_ROLE_HOST)
        send_pkt(-1, LS_MSG_CMD_FRAME, tick, (uint8_t)s_player_id,
                 payload, (uint16_t)n, 0);
    else if(s_role == LS_ROLE_CLIENT)
        send_pkt(0, LS_MSG_CMD_FRAME, tick, (uint8_t)s_player_id,
                 payload, (uint16_t)n, 0);

    s_npending = 0;
}

static void apply_cmds(uint32_t tick)
{
    int p, i, n;
    struct ls_cmd cmds[LS_MAX_CMDS];

    SimClock_SetApplying(true);
    for(p = 0; p < s_num_players; p++) {
        n = CmdBuf_Get(tick, p, cmds, LS_MAX_CMDS);
        if(n < 0)
            continue;
        for(i = 0; i < n; i++) {
            switch(cmds[i].type) {
            case LS_CMD_MOVE:
                G_Move_SetDest(cmds[i].uid, (vec2_t){cmds[i].x, cmds[i].z},
                               cmds[i].attack != 0);
                break;
            case LS_CMD_ATTACK:
                G_Combat_AttackUnit(cmds[i].uid, cmds[i].target);
                break;
            case LS_CMD_STOP:
                G_Combat_StopAttack(cmds[i].uid);
                G_Move_Stop(cmds[i].uid);
                break;
            default:
                break;
            }
        }
    }
    SimClock_SetApplying(false);

    for(i = 0; i < 6; i++)
        G_Timer_EmitOne60Hz();

    s_last_hash = SimHash_World(tick);
    if(s_role == LS_ROLE_CLIENT) {
        uint8_t pay[4];
        pay[0] = (uint8_t)(s_last_hash);
        pay[1] = (uint8_t)(s_last_hash >> 8);
        pay[2] = (uint8_t)(s_last_hash >> 16);
        pay[3] = (uint8_t)(s_last_hash >> 24);
        send_pkt(0, LS_MSG_HASH_REPORT, tick, (uint8_t)s_player_id, pay, 4, s_last_hash);
    }
    SimClock_Advance();
}

static void try_apply(uint32_t now)
{
    Catchup_BeginFrame();
    while(CmdBuf_Ready(SimClock_Tick())) {
        if(Catchup_Active()) {
            if(!Catchup_BudgetRemain())
                break;
            apply_cmds(SimClock_Tick());
            if(SimClock_Tick() >= Catchup_Target()) {
                Catchup_Clear();
                set_status("playing");
            }
            continue;
        }
        if((now - s_last_apply_ms) < 100u)
            break;
        apply_cmds(SimClock_Tick());
        s_last_apply_ms = now;
        s_wait_since_ms = 0;
        break;
    }
}

static void handle_welcome(const struct ls_packet *pkt)
{
    if(pkt->payload_len < 5)
        return;
    s_player_id = pkt->player_id;
    s_seed = (uint32_t)pkt->payload[0]
           | ((uint32_t)pkt->payload[1] << 8)
           | ((uint32_t)pkt->payload[2] << 16)
           | ((uint32_t)pkt->payload[3] << 24);
    s_num_players = pkt->payload[4];
    if(s_num_players < 1)
        s_num_players = 1;
    if(s_num_players > LS_MAX_PLAYERS)
        s_num_players = LS_MAX_PLAYERS;
    s_got_welcome = true;
    CmdBuf_SetNumPlayers(s_num_players);
    set_status("waiting");
    fprintf(stderr, "LS: welcome id=%d seed=%u n=%d\n",
            s_player_id, s_seed, s_num_players);
}

static void handle_cmd_frame(const struct ls_packet *pkt)
{
    struct ls_cmd cmds[LS_MAX_CMDS];
    int n = 0;
    if(pkt->payload_len > 0) {
        n = ls_cmds_decode(pkt->payload, pkt->payload_len, cmds, LS_MAX_CMDS);
        if(n < 0)
            n = 0;
    }
    CmdBuf_Put(pkt->tick, pkt->player_id, cmds, n);
    if(s_role == LS_ROLE_HOST)
        send_pkt(-1, pkt->msg_type, pkt->tick, pkt->player_id,
                 pkt->payload, pkt->payload_len, pkt->checksum);
}

static void handle_snap_begin(const struct ls_packet *pkt)
{
    uint32_t host_tick = pkt->tick;
    if(pkt->payload_len >= 8) {
        host_tick = (uint32_t)pkt->payload[4]
                  | ((uint32_t)pkt->payload[5] << 8)
                  | ((uint32_t)pkt->payload[6] << 16)
                  | ((uint32_t)pkt->payload[7] << 24);
    }
    Catchup_Begin(host_tick);
    set_status("catchup");
}

static void handle_hello_as_host(int peer)
{
    uint8_t pay[5];
    int pid = -1;

    if(peer < 0 || peer >= LS_MAX_PLAYERS)
        return;
    if(s_peer_player[peer] >= 0)
        pid = s_peer_player[peer];
    else if(s_host_assigned < s_num_players) {
        pid = s_host_assigned++;
        s_peer_player[peer] = pid;
    }
    if(pid < 0)
        return;

    pay[0] = (uint8_t)(s_seed);
    pay[1] = (uint8_t)(s_seed >> 8);
    pay[2] = (uint8_t)(s_seed >> 16);
    pay[3] = (uint8_t)(s_seed >> 24);
    pay[4] = (uint8_t)s_num_players;
    send_pkt(peer, LS_MSG_WELCOME, 0, (uint8_t)pid, pay, 5, 0);

    if(!s_started && s_host_assigned >= s_num_players) {
        send_pkt(-1, LS_MSG_START, 0, 0, NULL, 0, 0);
        begin_match();
    }
}

static void recv_one(int peer, const uint8_t *buf, int len)
{
    struct ls_packet pkt;
    if(!ls_pkt_decode(buf, (size_t)len, &pkt))
        return;

    switch(pkt.msg_type) {
    case LS_MSG_HELLO:
        if(s_role == LS_ROLE_HOST)
            handle_hello_as_host(peer);
        break;
    case LS_MSG_WELCOME:
        if(s_role == LS_ROLE_CLIENT && !s_got_welcome)
            handle_welcome(&pkt);
        break;
    case LS_MSG_START:
        if(!s_started)
            begin_match();
        break;
    case LS_MSG_CMD_FRAME:
    case LS_MSG_CMD_EMPTY:
    case LS_MSG_CMD_REPLAY:
        handle_cmd_frame(&pkt);
        break;
    case LS_MSG_SNAP_BEGIN:
        handle_snap_begin(&pkt);
        break;
    case LS_MSG_DESYNC:
        s_desync = true;
        set_status("desync");
        break;
    default:
        break;
    }
}

static void pump_recv(void)
{
    uint8_t buf[LS_MAX_PACKET];
    int peer, n;
    for(;;) {
        peer = 0;
        n = KcpUdp_RecvFrom(&peer, buf, (int)sizeof(buf));
        if(n <= 0)
            break;
        recv_one(peer, buf, n);
    }
}

void LS_Init(void)
{
    reset_state();
}

void LS_Shutdown(void)
{
    KcpUdp_Shutdown();
    G_Timer_SetWallSim(true);
    reset_state();
}

void LS_Pump(void)
{
    uint32_t now;

    if(s_role == LS_ROLE_NONE)
        return;

    KcpUdp_Poll();
    pump_recv();
    now = SDL_GetTicks();

    if(s_role == LS_ROLE_CLIENT && !s_got_welcome) {
        if(s_hello_ms == 0 || (now - s_hello_ms) >= 200u) {
            send_pkt(0, LS_MSG_HELLO, 0, 0, NULL, 0, 0);
            s_hello_ms = now;
        }
        set_status("connecting");
        return;
    }

    if(!s_started)
        return;

    if((now - s_last_submit_ms) >= 100u) {
        submit_frame();
        s_last_submit_ms = now;
    }

    if(!CmdBuf_Ready(SimClock_Tick()) && !Catchup_Active()) {
        if(s_wait_since_ms == 0)
            s_wait_since_ms = now;
        set_status("waiting_peers");
        return;
    }

    try_apply(now);
    if(s_desync)
        set_status("desync");
    else if(Catchup_Active())
        set_status("catchup");
    else
        set_status("playing");
}

static bool start_common(enum ls_role role, int nplayers, uint32_t seed)
{
    reset_state();
    s_role = role;
    s_seed = seed;
    s_num_players = nplayers < 1 ? 1 : (nplayers > LS_MAX_PLAYERS ? LS_MAX_PLAYERS : nplayers);
    CmdBuf_Reset(s_num_players);
    return true;
}

bool LS_StartLocal(uint32_t seed)
{
    if(!start_common(LS_ROLE_LOCAL, 1, seed))
        return false;
    s_player_id = 0;
    s_got_welcome = true;
    begin_match();
    return true;
}

bool LS_StartHost(uint16_t port, int num_players, uint32_t seed)
{
    int i;
    if(!start_common(LS_ROLE_HOST, num_players, seed))
        return false;
    if(!KcpUdp_Listen(port ? port : LS_DEFAULT_PORT)) {
        reset_state();
        return false;
    }
    s_player_id = 0;
    s_got_welcome = true;
    s_host_assigned = 1; /* slot 0 is the local host */
    for(i = 0; i < LS_MAX_PLAYERS; i++)
        s_peer_player[i] = -1;
    set_status("waiting");
    if(s_num_players <= 1)
        begin_match();
    return true;
}

bool LS_StartClient(const char *ip, uint16_t port)
{
    if(!ip)
        return false;
    if(!start_common(LS_ROLE_CLIENT, 1, 0))
        return false;
    if(!KcpUdp_Connect(ip, port ? port : LS_DEFAULT_PORT)) {
        reset_state();
        return false;
    }
    set_status("connecting");
    s_hello_ms = 0;
    return true;
}

bool LS_Enabled(void) { return s_role != LS_ROLE_NONE; }
bool LS_IsApplying(void) { return SimClock_Applying(); }
bool LS_ShouldQueueInput(void)
{
    if(s_role == LS_ROLE_NONE)
        return false;
    if(SimClock_Applying())
        return false;
    return true;
}
bool LS_CatchingUp(void) { return Catchup_Active(); }
bool LS_WaitingPeer(void)
{
    return s_started && !Catchup_Active() && !CmdBuf_Ready(SimClock_Tick());
}

uint32_t LS_Tick(void) { return SimClock_Tick(); }
uint32_t LS_Seed(void) { return s_seed; }
int LS_PlayerId(void) { return s_player_id; }
int LS_NumPlayers(void) { return s_num_players; }
const char *LS_Status(void) { return s_status; }

void LS_QueueMove(uint32_t uid, vec2_t dest_xz, bool attack)
{
    struct ls_cmd c;
    memset(&c, 0, sizeof(c));
    c.type = LS_CMD_MOVE;
    c.attack = attack ? 1 : 0;
    c.uid = uid;
    c.x = dest_xz.x;
    c.z = dest_xz.z;
    queue_cmd(c);
}

void LS_QueueAttack(uint32_t uid, uint32_t target)
{
    struct ls_cmd c;
    memset(&c, 0, sizeof(c));
    c.type = LS_CMD_ATTACK;
    c.uid = uid;
    c.target = target;
    queue_cmd(c);
}

void LS_QueueStop(uint32_t uid)
{
    struct ls_cmd c;
    memset(&c, 0, sizeof(c));
    c.type = LS_CMD_STOP;
    c.uid = uid;
    queue_cmd(c);
}
