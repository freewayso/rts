#include "catchup.h"
#include <SDL.h>

static bool s_active;
static uint32_t s_target;
static uint32_t s_frame_start;

void Catchup_Begin(uint32_t target_tick)
{
    s_active = true;
    s_target = target_tick;
}

void Catchup_Clear(void)
{
    s_active = false;
    s_target = 0;
}

bool Catchup_Active(void) { return s_active; }
uint32_t Catchup_Target(void) { return s_target; }

void Catchup_BeginFrame(void)
{
    s_frame_start = SDL_GetTicks();
}

bool Catchup_BudgetRemain(void)
{
    if(!s_active)
        return false;
    return (SDL_GetTicks() - s_frame_start) < 8u;
}
