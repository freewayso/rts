#include "sim_clock.h"

static uint32_t s_tick;
static bool s_paused;
static bool s_applying;
static bool s_stepping;

void SimClock_Reset(void)
{
    s_tick = 0;
    s_paused = false;
    s_applying = false;
    s_stepping = false;
}

uint32_t SimClock_Tick(void) { return s_tick; }
void SimClock_SetTick(uint32_t tick) { s_tick = tick; }
uint32_t SimClock_SubmitTick(void) { return s_tick + 2; } /* LS_CMD_DELAY */
void SimClock_Advance(void) { s_tick++; }

void SimClock_SetPaused(bool paused) { s_paused = paused; }
bool SimClock_Paused(void) { return s_paused; }

void SimClock_SetApplying(bool applying) { s_applying = applying; }
bool SimClock_Applying(void) { return s_applying; }
void SimClock_SetStepping(bool stepping) { s_stepping = stepping; }
bool SimClock_Stepping(void) { return s_stepping; }

uint32_t SimRand(uint32_t tick, uint32_t uid)
{
    uint32_t x = tick * 1664525u + uid * 1013904223u + 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
