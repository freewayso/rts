#ifndef LS_CMD_BUFFER_H
#define LS_CMD_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

#define LS_BUF_TICKS 256

void CmdBuf_Reset(int num_players);
void CmdBuf_SetNumPlayers(int n);

bool CmdBuf_Put(uint32_t tick, int player, const struct ls_cmd *cmds, int n);
bool CmdBuf_Ready(uint32_t tick);
int  CmdBuf_Get(uint32_t tick, int player, struct ls_cmd *out, int maxn);
bool CmdBuf_HasPlayer(uint32_t tick, int player);

void CmdBuf_LogPut(uint32_t tick, int player, const struct ls_cmd *cmds, int n);
int  CmdBuf_LogGet(uint32_t tick, int player, struct ls_cmd *out, int maxn);
uint32_t CmdBuf_LogOldest(void);
uint32_t CmdBuf_LogNewest(void);

#endif
