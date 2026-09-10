#ifndef LOCKSTEP_H
#define LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>
#include "../../pf_math.h"

#define LS_MAX_PLAYERS     8
#define LS_CMD_DELAY       2
#define LS_TICK_HZ         10
#define LS_STALL_TICKS     30
#define LS_SNAP_INTERVAL   50
#define LS_SNAP_KEEP       12
#define LS_DEFAULT_PORT    40000

enum ls_role{
    LS_ROLE_NONE = 0,
    LS_ROLE_LOCAL,
    LS_ROLE_HOST,
    LS_ROLE_CLIENT
};

void     LS_Init(void);
void     LS_Shutdown(void);
void     LS_Pump(void);

bool     LS_StartLocal(uint32_t seed);
bool     LS_StartHost(uint16_t port, int num_players, uint32_t seed);
bool     LS_StartClient(const char *ip, uint16_t port);

bool     LS_Enabled(void);
bool     LS_IsApplying(void);
bool     LS_ShouldQueueInput(void);
bool     LS_CatchingUp(void);
bool     LS_WaitingPeer(void);

uint32_t LS_Tick(void);
uint32_t LS_Seed(void);
int      LS_PlayerId(void);
int      LS_NumPlayers(void);
const char *LS_Status(void);

void     LS_QueueMove(uint32_t uid, vec2_t dest_xz, bool attack);
void     LS_QueueAttack(uint32_t uid, uint32_t target);
void     LS_QueueStop(uint32_t uid);

#endif
