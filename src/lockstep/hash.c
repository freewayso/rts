/*
 *  State hashing for desync detection.
 */

#define MEM_FILE_SYS MEM_SYS_LOCKSTEP
#define MEM_FILE_SUB 0

#include "public/lockstep.h"
#include "../game/public/game.h"

static uint64_t (*s_hash_provider)(void);

void LS_SetHashProvider(uint64_t (*fn)(void))
{
    s_hash_provider = fn;
}

static uint64_t hash_mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

uint64_t LS_ComputeStateHash(void)
{
    if(s_hash_provider)
        return s_hash_provider();

    /* Baseline hash must be identical on every peer.
     * Do NOT mix G_GetPlayerControlledFactions() — each client only controls
     * its own seat, so that mask differs and was falsely tripping DESYNC,
     * freezing the sim barrier (orders never applied). */
    uint64_t h = 0xcbf29ce484222325ULL;
    h = hash_mix(h, LS_SimTick());
    h = hash_mix(h, (uint64_t)G_GetSimState());
    return h;
}
