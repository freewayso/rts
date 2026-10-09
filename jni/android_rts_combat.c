/*
 * Expand RTS slice: simple build / attack intents (no Python).
 * When game APIs are linked, replace LOG stubs with G_* calls.
 */
#include "pf_android_internal.h"

#include <android/log.h>
#include <stdbool.h>

#define LOG_TAG "PfRtsCombat"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static int s_player_faction = 0;
static int s_wood, s_stone, s_food;
static bool s_build_mode;

void pf_rts_slice_resources_reset(void)
{
    s_wood = 200;
    s_stone = 100;
    s_food = 150;
    s_build_mode = false;
    s_player_faction = 0;
}

void pf_rts_slice_set_build_mode(bool on)
{
    s_build_mode = on;
    LOGI("build_mode=%d", (int)on);
}

bool pf_rts_slice_try_build_at(float x, float y)
{
    if(!s_build_mode)
        return false;
    if(s_wood < 50) {
        LOGI("not enough wood for building");
        return false;
    }
    s_wood -= 50;
    /* G_Building_Place(s_player_faction, x, y) when linked */
    LOGI("place building at %.0f,%.0f wood=%d", x, y, s_wood);
    return true;
}

void pf_rts_slice_issue_attack(int attacker_uid, int target_uid)
{
    /* G_Combat_Attack(attacker_uid, target_uid) */
    LOGI("attack %d -> %d", attacker_uid, target_uid);
}

void pf_rts_slice_get_resources(int *wood, int *stone, int *food)
{
    if(wood) *wood = s_wood;
    if(stone) *stone = s_stone;
    if(food) *food = s_food;
}
