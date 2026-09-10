#ifndef SIM_CLOCK_H
#define SIM_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

void     SimClock_Reset(void);
uint32_t SimClock_Tick(void);
void     SimClock_SetTick(uint32_t tick);
uint32_t SimClock_SubmitTick(void);
void     SimClock_Advance(void);

void     SimClock_SetPaused(bool paused);
bool     SimClock_Paused(void);

void     SimClock_SetApplying(bool applying);
bool     SimClock_Applying(void);
void     SimClock_SetStepping(bool stepping);
bool     SimClock_Stepping(void);

uint32_t SimRand(uint32_t tick, uint32_t uid);

#endif
