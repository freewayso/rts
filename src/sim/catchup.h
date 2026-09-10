#ifndef LS_CATCHUP_H
#define LS_CATCHUP_H

#include <stdbool.h>
#include <stdint.h>

void     Catchup_Begin(uint32_t target_tick);
void     Catchup_Clear(void);
bool     Catchup_Active(void);
uint32_t Catchup_Target(void);
bool     Catchup_BudgetRemain(void);
void     Catchup_BeginFrame(void);

#endif
