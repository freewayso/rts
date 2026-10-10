/*
 * Single-thread Sched_* stub for Android (sync run). Replace with aarch64
 * fiber sched.c when context_switch_aarch64.S path is fully validated.
 */
#include "../src/sched.h"

#include <stdlib.h>
#include <string.h>

static uint32_t s_next_tid = 1;
static uint32_t s_active_tid = 1;

bool Sched_FutureIsReady(const struct future *future)
{
    return future && (SDL_AtomicGet((SDL_atomic_t *)&future->status) == FUTURE_COMPLETE);
}

void Sched_TryYield(void) {}

bool Sched_Init(void) { return true; }
void Sched_Shutdown(void) {}
void Sched_HandleEvent(int event, void *arg, int event_source, bool immediate)
{
    (void)event; (void)arg; (void)event_source; (void)immediate;
}
void Sched_StartBackgroundTasks(void) {}
void Sched_Tick(void) {}

static uint32_t run_task(task_func_t code, void *arg, struct future *result)
{
    uint32_t tid = s_next_tid++;
    uint32_t prev = s_active_tid;
    s_active_tid = tid;
    struct result res = code ? code(arg) : NULL_RESULT;
    s_active_tid = prev;
    if(result) {
        result->res = res;
        SDL_AtomicSet(&result->status, FUTURE_COMPLETE);
    }
    return tid;
}

uint32_t Sched_Create(int prio, task_func_t code, void *arg, const char *name,
                      struct future *result, int flags)
{
    (void)prio; (void)name; (void)flags;
    return run_task(code, arg, result);
}

uint32_t Sched_CreateBlocking(int prio, task_func_t code, void *arg, const char *name,
                              struct future *result, int flags)
{
    return Sched_Create(prio, code, arg, name, result, flags);
}

bool Sched_RunSync(uint32_t tid) { (void)tid; return true; }
void Sched_AwaitAll(const uint32_t *tids, const struct future *futures, size_t n)
{
    (void)tids; (void)futures; (void)n;
}
void Sched_ClearState(void) {}
void Sched_Flush(void) {}
bool Sched_HasBlocked(void) { return false; }
bool Sched_IsReady(uint32_t tid) { (void)tid; return true; }
bool Sched_IsEventBlocked(uint32_t tid) { (void)tid; return false; }
bool Sched_TryCancel(uint32_t tid) { (void)tid; return false; }
bool Sched_AbortScriptBlocked(uint32_t tid) { (void)tid; return false; }

uint64_t Sched_Request(struct request req)
{
    if(req.type == SCHED_REQ_MY_TID)
        return s_active_tid;
    if(req.type == SCHED_REQ_YIELD)
        return 0;
    return 0;
}

uint32_t Sched_ActiveTID(void) { return s_active_tid; }
bool Sched_UsingBigStack(void) { return true; }
