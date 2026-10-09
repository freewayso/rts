/*
 *  Deterministic xorshift64 RNG for lockstep matches.
 */

#include "public/lockstep.h"

static uint64_t s_state = 0xDEADBEEFCAFEBABEULL;

void LS_RngSeed(uint64_t seed)
{
    s_state = seed ? seed : 0xDEADBEEFCAFEBABEULL;
}

uint32_t LS_RngNext(void)
{
    uint64_t x = s_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s_state = x;
    return (uint32_t)(x >> 32);
}

float LS_RngFloat(void)
{
    return (LS_RngNext() >> 8) * (1.0f / 16777216.0f);
}
