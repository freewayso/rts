/*
 *  Fixed simulation clock for lockstep / deterministic gameplay timers.
 */

#define MEM_FILE_SYS MEM_SYS_LOCKSTEP
#define MEM_FILE_SUB 0

#include "public/lockstep.h"

#include <assert.h>

static uint32_t s_tick;
static uint32_t s_hz = LS_DEFAULT_HZ;
static float    s_dt = 0.1f;

void LS_SimClock_Reset(uint32_t hz)
{
    s_hz = hz ? hz : LS_DEFAULT_HZ;
    s_dt = 1.0f / (float)s_hz;
    s_tick = 0;
}

void LS_SimClock_Advance(void)
{
    s_tick++;
}

uint32_t LS_SimTick(void)
{
    return s_tick;
}

uint32_t LS_SimHz(void)
{
    return s_hz;
}

float LS_FixedDt(void)
{
    return s_dt;
}

uint32_t LS_SimTimeMs(void)
{
    if(s_hz == 0)
        return 0;
    return (uint32_t)((uint64_t)s_tick * 1000ull / (uint64_t)s_hz);
}

void LS_SimClock_SetTick(uint32_t tick)
{
    s_tick = tick;
}
