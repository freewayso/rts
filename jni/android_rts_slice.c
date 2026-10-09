/*
 * Minimal RTS interaction slice for Android (no Python).
 * Touch → select / move / camera; hooks G_Move_* / G_Combat_* when engine linked.
 */
#include "pf_android.h"
#include "pf_android_internal.h"

#include <android/log.h>
#include <math.h>
#include <stdbool.h>

#define LOG_TAG "PfRtsSlice"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static float s_down_x, s_down_y;
static float s_cam_yaw, s_cam_pitch;
static float s_cam_dist = 120.0f;
static bool  s_has_selection;
static int   s_selected_uid = -1;
static bool  s_combat_auto;

void pf_rts_slice_reset(void)
{
    s_has_selection = false;
    s_selected_uid = -1;
    s_cam_yaw = 0;
    s_cam_pitch = 0.6f;
    s_cam_dist = 120.0f;
    s_combat_auto = true;
    pf_rts_slice_resources_reset();
    LOGI("rts slice reset");
}

void pf_rts_slice_on_touch(int type, float x, float y, int pointer_id)
{
    (void)pointer_id;
    const float tap_slop = 24.0f;

    if(type == 0) {
        s_down_x = x;
        s_down_y = y;
        return;
    }

    if(type == 1) {
        float dx = x - s_down_x;
        float dy = y - s_down_y;
        if(fabsf(dx) + fabsf(dy) > tap_slop) {
            s_cam_yaw += dx * 0.01f;
            s_cam_pitch += dy * 0.01f;
            if(s_cam_pitch < 0.2f) s_cam_pitch = 0.2f;
            if(s_cam_pitch > 1.4f) s_cam_pitch = 1.4f;
            /* Camera_SetAngles(s_cam_yaw, s_cam_pitch) when linked */
            s_down_x = x;
            s_down_y = y;
        }
        return;
    }

    if(type == 2) {
        float dx = x - s_down_x;
        float dy = y - s_down_y;
        if(fabsf(dx) + fabsf(dy) <= tap_slop) {
            if(!s_has_selection) {
                s_has_selection = true;
                s_selected_uid = 1; /* placeholder until raycast → G_Sel_ */
                LOGI("select at %.0f,%.0f uid=%d", x, y, s_selected_uid);
            }else if(pf_rts_slice_try_build_at(x, y)) {
                /* build consumed the tap */
            }else{
                /* G_Move_SetDest(s_selected_uid, world_x, world_z) */
                LOGI("move order at %.0f,%.0f for uid=%d", x, y, s_selected_uid);
                if(s_combat_auto) {
                    pf_rts_slice_issue_attack(s_selected_uid, /*nearest*/0);
                }
            }
        }
    }
}

void pf_rts_slice_tick(void)
{
    (void)s_cam_dist;
}

void pf_rts_slice_clear_selection(void)
{
    s_has_selection = false;
    s_selected_uid = -1;
}
