/*
 * Full-engine Android hooks (PF_ANDROID_FULL).
 * Bypasses Python: C bootstrap loads a fixed PFMAP when game/map are linked.
 * Until those sources are in the Android CMake target, this records paths and
 * runs a single-thread update stub.
 */
#include "pf_android.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#define LOG_TAG "PfEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static char s_dir[512];
static char s_map_path[768];
static int  s_w, s_h;
static bool s_ready;
static int  s_quality = 1;
static bool s_paused;
static bool s_water_fx; /* reflection/refraction; off on quality 0 */
static bool s_single_gl_thread = true;

static float s_last_x, s_last_y;

static bool path_is_file(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0) && S_ISREG(st.st_mode);
}

static bool find_demo_map(const char *root, char *out, size_t out_sz)
{
    /* Prefer known demo names, then first .pfmap under maps/ */
    static const char *candidates[] = {
        "maps/demo.pfmap",
        "maps/demo/demo.pfmap",
        "assets/maps/demo.pfmap",
        NULL
    };
    char buf[768];
    for(int i = 0; candidates[i]; ++i) {
        snprintf(buf, sizeof(buf), "%s/%s", root, candidates[i]);
        if(path_is_file(buf)) {
            snprintf(out, out_sz, "%s", buf);
            return true;
        }
    }

    snprintf(buf, sizeof(buf), "%s/maps", root);
    DIR *d = opendir(buf);
    if(!d)
        return false;
    struct dirent *ent;
    while((ent = readdir(d)) != NULL) {
        size_t n = strlen(ent->d_name);
        if(n > 6 && strcmp(ent->d_name + n - 6, ".pfmap") == 0) {
            snprintf(out, out_sz, "%s/maps/%s", root, ent->d_name);
            closedir(d);
            return path_is_file(out);
        }
    }
    closedir(d);
    return false;
}

bool pf_android_engine_init(const char *files_dir, int w, int h)
{
    snprintf(s_dir, sizeof(s_dir), "%s", files_dir ? files_dir : ".");
    s_w = w;
    s_h = h;
    s_ready = true;
    s_paused = false;
    s_water_fx = (s_quality > 0);
    s_map_path[0] = '\0';

    /*
     * Intended when game/map/render are linked into libpermafrost.so:
     *   g_basepath = s_dir;
     *   G_Init(); R_Init(g_basepath);  // skip S_Init / S_RunFileAsync
     *   M_LoadMap(s_map_path) / G_NewGameFromMap(...);
     *   Camera_Set(...);
     *   render: single GL thread — execute R_PushCmd immediately (no render worker).
     */
    if(find_demo_map(s_dir, s_map_path, sizeof(s_map_path))) {
        LOGI("demo map found: %s", s_map_path);
    }else{
        LOGI("no .pfmap under %s/maps yet (copy assets into APK)", s_dir);
    }

    LOGI("engine_init base=%s %dx%d single_gl=%d water_fx=%d",
         s_dir, s_w, s_h, (int)s_single_gl_thread, (int)s_water_fx);
    return true;
}

void pf_android_engine_resize(int w, int h)
{
    s_w = w;
    s_h = h;
    /* R_GL_SetViewport when render linked */
}

void pf_android_engine_frame(void)
{
    if(!s_ready || s_paused)
        return;
    /*
     * Single GL thread loop:
     *   G_Update(); G_Render(); // cmds run inline under PF_ANDROID
     *   G_SwapBuffers();
     */
}

void pf_android_engine_pause(void)
{
    s_paused = true;
    LOGI("engine pause (sim halted)");
}

void pf_android_engine_resume(void)
{
    s_paused = false;
    LOGI("engine resume");
}

void pf_android_engine_shutdown(void)
{
    s_ready = false;
    LOGI("engine shutdown");
}

void pf_android_engine_touch(int type, float x, float y, int id)
{
    (void)id;
    if(type == 0) {
        s_last_x = x;
        s_last_y = y;
    }else if(type == 1) {
        s_last_x = x;
        s_last_y = y;
    }else if(type == 2) {
        LOGI("engine touch up %.0f,%.0f", x, y);
    }
}

void pf_android_engine_set_quality(int level)
{
    s_quality = level;
    s_water_fx = (level > 0);
    /*
     * quality 0: no water reflection/refraction, smaller RTs, fewer units
     * quality 1: fake translucent water, medium RT
     * quality 2: closer to desktop (still no geom/compute on ES)
     */
    LOGI("engine quality=%d water_fx=%d", s_quality, (int)s_water_fx);
}

bool pf_android_engine_water_fx_enabled(void)
{
    return s_water_fx;
}

const char *pf_android_engine_map_path(void)
{
    return s_map_path[0] ? s_map_path : NULL;
}
