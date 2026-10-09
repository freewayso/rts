/*
 *  Lockstep public API: sim clock, commands, scheduler, hash, replay.
 */

#ifndef LOCKSTEP_H
#define LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../../net/public/net.h"

#define LS_MAX_PLAYERS          NET_MAX_PEERS
#define LS_MAX_CMDS_PER_FRAME   64
#define LS_MAX_ENTITIES_PER_CMD 64
#define LS_CMD_BUFFER_FRAMES    128
#define LS_DEFAULT_HZ           10
#define LS_DEFAULT_DELAY        2
#define LS_HASH_INTERVAL        10

enum game_net_mode{
    GAME_MODE_SP = 0,
    GAME_MODE_MP_LOCKSTEP
};

enum ls_cmd_type{
    LS_CMD_NONE = 0,
    LS_CMD_MOVE,
    LS_CMD_ATTACK,
    LS_CMD_STOP,
    LS_CMD_SET_STANCE,
    LS_CMD_BUILD,
    LS_CMD_GATHER,
    LS_CMD_GARRISON,
    LS_CMD_PRODUCE,
    LS_CMD_CANCEL,
    LS_CMD_CUSTOM,
    LS_CMD_COUNT
};

#pragma pack(push, 1)
struct ls_cmd{
    uint8_t  type;
    uint8_t  flags;
    uint16_t ent_count;
    uint32_t ents[LS_MAX_ENTITIES_PER_CMD];
    float    x, z;
    uint32_t target_uid;
    int32_t  i0, i1;
};
#pragma pack(pop)

struct ls_player_frame{
    uint32_t       tick;
    uint8_t        player_id;
    uint8_t        cmd_count;
    uint16_t       reserved;
    struct ls_cmd  cmds[LS_MAX_CMDS_PER_FRAME];
};

struct ls_config{
    uint32_t sim_hz;
    uint32_t command_delay;
    uint64_t rng_seed;
    int      local_player_id;
    int      num_players;
    bool     is_host;
    bool     local_only; /* fake dual / single-process lockstep */
    char     map_path[NET_MAX_MAP_PATH];
    /* Per-slot faction binding. 0 = treat player_id as faction_id (legacy demo). */
    int      faction_ids[LS_MAX_PLAYERS];
};

/* Mode / lifecycle -------------------------------------------------------- */
void     LS_SetMode(enum game_net_mode mode);
enum game_net_mode LS_GetMode(void);
bool     LS_IsLockstep(void);

bool     LS_Init(const struct ls_config *cfg);
void     LS_Shutdown(void);
void     LS_Reset(void);

/* Called once per engine frame from main loop */
void     LS_Service(void);

/* Sim clock --------------------------------------------------------------- */
uint32_t LS_SimTick(void);
uint32_t LS_SimHz(void);
float    LS_FixedDt(void);
uint32_t LS_SimTimeMs(void); /* tick * (1000/hz) — for gameplay timers */
bool     LS_TryAdvanceSim(void); /* true if a sim tick was applied */

/* Deterministic RNG ------------------------------------------------------- */
void     LS_RngSeed(uint64_t seed);
uint32_t LS_RngNext(void);
float    LS_RngFloat(void); /* [0,1) */

/* Local command issue (validated against controllable factions) ----------- */
bool     LS_IssueCmd(const struct ls_cmd *cmd);
bool     LS_IssueMove(const uint32_t *ents, int n, float x, float z, bool attack);
bool     LS_IssueAttack(const uint32_t *ents, int n, uint32_t target);
bool     LS_CanControlEntity(uint32_t uid);

/* Hash / desync ----------------------------------------------------------- */
uint64_t LS_ComputeStateHash(void);
void     LS_SetHashProvider(uint64_t (*fn)(void));
bool     LS_LastDesync(void);
void     LS_ClearDesync(void);

/* Replay ------------------------------------------------------------------ */
bool     LS_ReplayBeginRecord(const char *path);
void     LS_ReplayEndRecord(void);
bool     LS_ReplayLoad(const char *path);
bool     LS_ReplayPlaying(void);

/* Late join / disconnect -------------------------------------------------- */
bool     LS_RequestSnapshotExport(const char *path);
bool     LS_NotifyPeerDisconnected(int player_id);
bool     LS_IsPeerConnected(int player_id);

/* Wire protocol helpers (host relay).
 * Types must stay clear of lobby UDP ids (LOBBY_MSG_* = 1..9 in net.h). */
enum ls_net_msg_type{
    LS_NET_CMD_FRAME = 0x40,
    LS_NET_HASH,
    LS_NET_DESYNC,
    LS_NET_PEER_LEFT,
    LS_NET_SNAPSHOT_META,
    LS_NET_EMPTY_FRAME,
    LS_NET_READY,   /* peer finished LS_Init */
    LS_NET_GO,      /* host: all ready — clients reset clocks */
    LS_NET_GO_ACK   /* client: finished reset; host starts after all acks */
};

#endif
