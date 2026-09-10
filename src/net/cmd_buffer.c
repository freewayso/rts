#include "cmd_buffer.h"
#include <string.h>

struct slot{
    bool present;
    int  ncmds;
    struct ls_cmd cmds[LS_MAX_CMDS];
};

struct tick_row{
    uint32_t tick;
    struct slot players[LS_MAX_PLAYERS];
};

static struct tick_row s_buf[LS_BUF_TICKS];
static struct tick_row s_log[LS_BUF_TICKS];
static int s_nplayers = 1;
static uint32_t s_log_oldest = 0;
static uint32_t s_log_newest = 0;
static bool s_log_init = false;

static struct tick_row *row_for(struct tick_row *tab, uint32_t tick, bool alloc)
{
    struct tick_row *r = &tab[tick % LS_BUF_TICKS];
    if(r->tick != tick) {
        if(!alloc)
            return NULL;
        memset(r, 0, sizeof(*r));
        r->tick = tick;
    }
    return r;
}

void CmdBuf_Reset(int num_players)
{
    memset(s_buf, 0, sizeof(s_buf));
    memset(s_log, 0, sizeof(s_log));
    s_nplayers = num_players < 1 ? 1 : (num_players > LS_MAX_PLAYERS ? LS_MAX_PLAYERS : num_players);
    s_log_oldest = 0;
    s_log_newest = 0;
    s_log_init = false;
}

void CmdBuf_SetNumPlayers(int n)
{
    s_nplayers = n < 1 ? 1 : (n > LS_MAX_PLAYERS ? LS_MAX_PLAYERS : n);
}

bool CmdBuf_Put(uint32_t tick, int player, const struct ls_cmd *cmds, int n)
{
    if(player < 0 || player >= s_nplayers || n < 0 || n > LS_MAX_CMDS)
        return false;
    struct tick_row *r = row_for(s_buf, tick, true);
    r->players[player].present = true;
    r->players[player].ncmds = n;
    if(n)
        memcpy(r->players[player].cmds, cmds, (size_t)n * sizeof(struct ls_cmd));
    CmdBuf_LogPut(tick, player, cmds, n);
    return true;
}

bool CmdBuf_HasPlayer(uint32_t tick, int player)
{
    if(player < 0 || player >= s_nplayers)
        return false;
    struct tick_row *r = row_for(s_buf, tick, false);
    if(!r)
        return false;
    return r->players[player].present;
}

bool CmdBuf_Ready(uint32_t tick)
{
    struct tick_row *r = row_for(s_buf, tick, false);
    if(!r)
        return false;
    for(int i = 0; i < s_nplayers; i++) {
        if(!r->players[i].present)
            return false;
    }
    return true;
}

int CmdBuf_Get(uint32_t tick, int player, struct ls_cmd *out, int maxn)
{
    if(player < 0 || player >= s_nplayers)
        return -1;
    struct tick_row *r = row_for(s_buf, tick, false);
    if(!r || !r->players[player].present)
        return -1;
    int n = r->players[player].ncmds;
    if(n > maxn)
        n = maxn;
    if(n > 0)
        memcpy(out, r->players[player].cmds, (size_t)n * sizeof(struct ls_cmd));
    return n;
}

void CmdBuf_LogPut(uint32_t tick, int player, const struct ls_cmd *cmds, int n)
{
    if(player < 0 || player >= LS_MAX_PLAYERS || n < 0 || n > LS_MAX_CMDS)
        return;
    struct tick_row *r = row_for(s_log, tick, true);
    r->players[player].present = true;
    r->players[player].ncmds = n;
    if(n)
        memcpy(r->players[player].cmds, cmds, (size_t)n * sizeof(struct ls_cmd));
    if(!s_log_init) {
        s_log_oldest = tick;
        s_log_newest = tick;
        s_log_init = true;
    }else{
        if(tick < s_log_oldest)
            s_log_oldest = tick;
        if(tick > s_log_newest)
            s_log_newest = tick;
        if(s_log_newest - s_log_oldest >= LS_BUF_TICKS)
            s_log_oldest = s_log_newest - (LS_BUF_TICKS - 1);
    }
}

int CmdBuf_LogGet(uint32_t tick, int player, struct ls_cmd *out, int maxn)
{
    if(player < 0 || player >= LS_MAX_PLAYERS)
        return -1;
    struct tick_row *r = row_for(s_log, tick, false);
    if(!r || !r->players[player].present)
        return -1;
    int n = r->players[player].ncmds;
    if(n > maxn)
        n = maxn;
    if(n > 0)
        memcpy(out, r->players[player].cmds, (size_t)n * sizeof(struct ls_cmd));
    return n;
}

uint32_t CmdBuf_LogOldest(void) { return s_log_oldest; }
uint32_t CmdBuf_LogNewest(void) { return s_log_newest; }
