#include "sim_hash.h"
#include "../game/public/game.h"
#include "../entity.h"

#include <stdint.h>

#define Q(f) ((int32_t)((f) * 1024.0f))

uint32_t SimHash_World(uint32_t tick)
{
    uint32_t h = 0x811c9dc5u ^ tick;
    uint32_t uids[4096];
    int n = G_Pos_EntsInRect((vec2_t){-4000.0f, -4000.0f},
                             (vec2_t){ 4000.0f,  4000.0f},
                             uids, 4096);
    /* Insertion sort for a stable checksum across platforms. */
    for(int i = 1; i < n; i++) {
        uint32_t key = uids[i];
        int j = i - 1;
        while(j >= 0 && uids[j] > key) {
            uids[j + 1] = uids[j];
            j--;
        }
        uids[j + 1] = key;
    }
    for(int i = 0; i < n; i++) {
        uint32_t uid = uids[i];
        vec2_t xz = G_Pos_GetXZ(uid);
        int hp = 0;
        if(G_FlagsGet(uid) & ENTITY_FLAG_COMBATABLE)
            hp = G_Combat_GetCurrentHP(uid);
        h ^= uid + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= (uint32_t)Q(xz.x) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= (uint32_t)Q(xz.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= (uint32_t)hp + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    h ^= (uint32_t)n;
    return h;
}
