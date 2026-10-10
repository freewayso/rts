/*
 * LAN P2P lobby + lockstep without Python (mirrors py_mp.c / match_online.py).
 */
#include "android_mp.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(PF_ANDROID_ENGINE_LINKED) && defined(PF_ANDROID_HAVE_NET)

#include "net/public/net.h"
#include "lockstep/public/lockstep.h"
#include "lib/public/pf_string.h"

#define LOG_TAG "PfMp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static struct lobby_start_info s_pending_start;
static int                     s_have_match_start;
static int                     s_match_started_event;
static char                    s_match_map[NET_MAX_MAP_PATH];

static void begin_lockstep_from_pending(void)
{
    if(!s_have_match_start)
        return;

    struct ls_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.rng_seed = s_pending_start.rng_seed;
    cfg.sim_hz = s_pending_start.sim_hz ? s_pending_start.sim_hz : LS_DEFAULT_HZ;
    cfg.command_delay = s_pending_start.command_delay
        ? s_pending_start.command_delay
        : LS_DEFAULT_DELAY;
    cfg.local_player_id = s_pending_start.local_player_id;
    cfg.num_players = s_pending_start.num_players;
    cfg.is_host = Lobby_IsHost();
    cfg.local_only = false;
    pf_strlcpy(cfg.map_path, s_pending_start.map_path, sizeof(cfg.map_path));

    for(int i = 0; i < LS_MAX_PLAYERS; i++) {
        if(i < s_pending_start.num_players && s_pending_start.players[i].connected)
            cfg.faction_ids[i] = s_pending_start.players[i].faction_id;
        else
            cfg.faction_ids[i] = 1 + (i % 3);
    }

    if(cfg.local_player_id < 0 || cfg.local_player_id >= LS_MAX_PLAYERS) {
        LOGE("LS_Init bad local_player_id=%d", cfg.local_player_id);
        return;
    }

    if(!LS_Init(&cfg)) {
        LOGE("LS_Init failed");
        return;
    }

    pf_strlcpy(s_match_map, s_pending_start.map_path, sizeof(s_match_map));
    s_have_match_start = 0;
    s_match_started_event = 1;
    LOGI("lockstep active seed=%llu local=%d players=%d host=%d map=%s",
         (unsigned long long)cfg.rng_seed, cfg.local_player_id, cfg.num_players,
         (int)cfg.is_host, cfg.map_path);
}

static void on_match_start_cb(const struct lobby_start_info *info, void *user)
{
    (void)user;
    if(!info)
        return;
    s_pending_start = *info;
    s_have_match_start = 1;
    LOGI("match_start seed=%llu players=%d local=%d map=%s",
         (unsigned long long)info->rng_seed, info->num_players,
         info->local_player_id, info->map_path);
    begin_lockstep_from_pending();
}

static void on_player_list_cb(const struct lobby_player *players, int count, void *user)
{
    (void)user;
    LOGI("players=%d", count);
    for(int i = 0; i < count; i++) {
        if(players[i].connected)
            LOGI("  slot=%d fac=%d name=%s ready=%d",
                 players[i].slot, players[i].faction_id,
                 players[i].name, (int)players[i].ready);
    }
}

static void on_disconnected_cb(int peer_slot, void *user)
{
    (void)user;
    LOGI("peer disconnected slot=%d", peer_slot);
    if(LS_IsLockstep())
        LS_NotifyPeerDisconnected(peer_slot);
}

static struct lobby_callbacks make_cbs(void)
{
    struct lobby_callbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_match_start = on_match_start_cb;
    cb.on_player_list = on_player_list_cb;
    cb.on_disconnected = on_disconnected_cb;
    return cb;
}

bool pf_android_mp_host(uint16_t port, const char *name, int faction_id)
{
    s_have_match_start = 0;
    s_match_started_event = 0;
    s_match_map[0] = '\0';
    if(!name || !name[0])
        name = "AndroidHost";
    if(faction_id <= 0)
        faction_id = 1;
    struct lobby_callbacks cb = make_cbs();
    if(!Lobby_Host(port ? port : NET_DEFAULT_PORT, name, faction_id, &cb)) {
        LOGE("Lobby_Host failed");
        return false;
    }
    /* Default demo map for LAN; host may override via set_map. */
    Lobby_SetMap("maps/demo.pfmap");
    LOGI("hosting port=%u name=%s fac=%d",
         (unsigned)(port ? port : NET_DEFAULT_PORT), name, faction_id);
    return true;
}

bool pf_android_mp_join(const char *host, uint16_t port, const char *name, int faction_id)
{
    s_have_match_start = 0;
    s_match_started_event = 0;
    s_match_map[0] = '\0';
    if(!host || !host[0])
        host = "127.0.0.1";
    if(!name || !name[0])
        name = "AndroidClient";
    if(faction_id <= 0)
        faction_id = 2;
    struct net_address addr;
    if(!Net_ParseAddress(host, port ? port : NET_DEFAULT_PORT, &addr)) {
        LOGE("bad host address %s", host);
        return false;
    }
    struct lobby_callbacks cb = make_cbs();
    if(!Lobby_Join(addr, name, faction_id, &cb)) {
        LOGE("Lobby_Join failed (retry later)");
        return false;
    }
    LOGI("joining %s:%u name=%s fac=%d", host,
         (unsigned)(port ? port : NET_DEFAULT_PORT), name, faction_id);
    return true;
}

void pf_android_mp_shutdown(void)
{
    if(LS_IsLockstep())
        LS_Shutdown();
    Lobby_Shutdown();
    s_have_match_start = 0;
    s_match_started_event = 0;
    s_match_map[0] = '\0';
    LOGI("mp shutdown");
}

bool pf_android_mp_set_map(const char *map_path)
{
    return Lobby_SetMap(map_path);
}

bool pf_android_mp_set_seed(uint64_t seed)
{
    return Lobby_SetSeed(seed);
}

bool pf_android_mp_set_ready(bool ready)
{
    return Lobby_SetReady(ready);
}

bool pf_android_mp_start_match(void)
{
    if(!Lobby_StartMatch()) {
        LOGE("Lobby_StartMatch failed");
        return false;
    }
    return true;
}

bool pf_android_mp_is_host(void)
{
    return Lobby_IsHost();
}

int pf_android_mp_local_slot(void)
{
    return Lobby_LocalSlot();
}

int pf_android_mp_player_count(void)
{
    return Lobby_PlayerCount();
}

bool pf_android_mp_is_lockstep(void)
{
    return LS_IsLockstep();
}

int pf_android_mp_players_text(char *buf, size_t buflen)
{
    if(!buf || buflen == 0)
        return 0;
    buf[0] = '\0';
    struct lobby_player players[NET_MAX_PEERS];
    int n = NET_MAX_PEERS;
    if(!Lobby_GetPlayers(players, &n) || n <= 0)
        return 0;
    size_t used = 0;
    for(int i = 0; i < n; i++) {
        char line[160];
        int w = snprintf(line, sizeof(line), "%d|%s|%d|%d|%d\n",
                         players[i].slot, players[i].name, players[i].faction_id,
                         (int)players[i].ready, (int)players[i].connected);
        if(w < 0)
            break;
        if(used + (size_t)w + 1 >= buflen)
            break;
        memcpy(buf + used, line, (size_t)w);
        used += (size_t)w;
        buf[used] = '\0';
    }
    return (int)used;
}

bool pf_android_mp_consume_match_started(void)
{
    if(!s_match_started_event)
        return false;
    s_match_started_event = 0;
    return true;
}

const char *pf_android_mp_match_map(void)
{
    return s_match_map;
}

#else /* !ENGINE_LINKED || !HAVE_NET */

bool pf_android_mp_host(uint16_t port, const char *name, int faction_id)
{
    (void)port; (void)name; (void)faction_id;
    return false;
}
bool pf_android_mp_join(const char *host, uint16_t port, const char *name, int faction_id)
{
    (void)host; (void)port; (void)name; (void)faction_id;
    return false;
}
void pf_android_mp_shutdown(void) {}
bool pf_android_mp_set_map(const char *map_path) { (void)map_path; return false; }
bool pf_android_mp_set_seed(uint64_t seed) { (void)seed; return false; }
bool pf_android_mp_set_ready(bool ready) { (void)ready; return false; }
bool pf_android_mp_start_match(void) { return false; }
bool pf_android_mp_is_host(void) { return false; }
int  pf_android_mp_local_slot(void) { return -1; }
int  pf_android_mp_player_count(void) { return 0; }
bool pf_android_mp_is_lockstep(void) { return false; }
int  pf_android_mp_players_text(char *buf, size_t buflen)
{
    if(buf && buflen) buf[0] = '\0';
    return 0;
}
bool pf_android_mp_consume_match_started(void) { return false; }
const char *pf_android_mp_match_map(void) { return ""; }

#endif
