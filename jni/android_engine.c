/*
 * Android engine host (PF_ANDROID_FULL).
 * With PF_ANDROID_ENGINE_LINKED: boots Mem/Settings/…/G_Init/R_Init, loads
 * demo.pfmap + demo.pfscene, runs G_Update/G_Render on the GLSurfaceView thread.
 * Without it: path/quality stub only (triangle drawn by android_bootstrap).
 */
#include "pf_android.h"
#include "pf_android_internal.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <math.h>

#if defined(PF_ANDROID_ENGINE_LINKED)
#include <SDL.h>
#include "main.h"
#include "mem.h"
#include "lib/public/noise.h"
#include "perf.h"
#include "settings.h"
#include "sched.h"
#include "session.h"
#include "asset_load.h"
#include "asset_cache.h"
#include "cursor.h"
#include "event.h"
#include "entity.h"
#include "anim/public/anim.h"
#include "game/public/game.h"
#include "render/public/render_ctrl.h"
#include "render/gl_render.h"
#include "ui.h"
#include "navigation/public/nav.h"
#include "audio/public/audio.h"
#include "phys/public/phys.h"
#include "sprite.h"
#include "scene.h"
#include "camera.h"
#include "cam_control.h"
#include "lib/public/stb_image.h"
#include "lockstep/public/lockstep.h"
#endif

#define LOG_TAG "PfEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static char s_dir[512];
static char s_map_path[768];
static char s_scene_path[768];
static int  s_w, s_h;
static bool s_ready;
static int  s_quality = 1;
static bool s_paused;
static bool s_water_fx;
static bool s_single_gl_thread = true;
static bool s_playing;
static bool s_inited_subsystems;

static float s_last_x, s_last_y;
static float s_down_x, s_down_y;
static bool  s_touch_moved;
static int s_drawable_w = 1280;
static int s_drawable_h = 720;

#if defined(PF_ANDROID_ENGINE_LINKED)
/* Keep SDL_Event alive until E_ServiceQueue / NotifyImmediate finishes. */
static SDL_Event s_touch_ev;
#endif

void pf_android_set_drawable_size(int w, int h)
{
    if(w > 0) s_drawable_w = w;
    if(h > 0) s_drawable_h = h;
}

void pf_android_get_drawable_size(int *out_w, int *out_h)
{
    if(out_w) *out_w = s_drawable_w;
    if(out_h) *out_h = s_drawable_h;
}

static bool path_is_file(const char *p)
{
    struct stat st;
    return (stat(p, &st) == 0) && S_ISREG(st.st_mode);
}

static bool find_demo_map(const char *root, char *out, size_t out_sz)
{
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

static void derive_scene_path(const char *map_path, char *out, size_t out_sz)
{
    out[0] = '\0';
    if(!map_path || !map_path[0])
        return;
    size_t n = strlen(map_path);
    if(n > 6 && strcmp(map_path + n - 6, ".pfmap") == 0) {
        snprintf(out, out_sz, "%.*s.pfscene", (int)(n - 6), map_path);
        if(path_is_file(out))
            return;
    }
    /* Prefer demo.pfscene next to the map */
    snprintf(out, out_sz, "%s/maps/demo.pfscene", s_dir);
    if(!path_is_file(out))
        out[0] = '\0';
}

#if defined(PF_ANDROID_ENGINE_LINKED)

static bool android_load_map_and_scene(void)
{
    if(!s_map_path[0]) {
        LOGE("no map path");
        return false;
    }

    SDL_RWops *rw = SDL_RWFromFile(s_map_path, "r");
    if(!rw) {
        LOGE("SDL_RWFromFile failed: %s (%s)", s_map_path, SDL_GetError());
        return false;
    }
    bool ok = G_LoadMap(rw, true);
    SDL_RWclose(rw);
    if(!ok) {
        LOGE("G_LoadMap failed: %s", s_map_path);
        return false;
    }
    LOGI("G_LoadMap OK: %s", s_map_path);

    if(s_scene_path[0]) {
        if(Scene_Load(s_scene_path)) {
            G_BakeNavDataForScene();
            LOGI("Scene_Load OK: %s", s_scene_path);
        }else{
            LOGE("Scene_Load failed (terrain-only): %s", s_scene_path);
        }
    }

    G_Fog_Disable();
    G_SetActiveCamera(G_GetActiveCamera(), CAM_MODE_RTS);
    return true;
}

static bool android_engine_boot(int w, int h)
{
    g_basepath = s_dir;
    g_main_thread_id = SDL_ThreadID();
    g_render_thread_id = g_main_thread_id;

    if(!Mem_Init()) {
        LOGE("Mem_Init failed");
        return false;
    }
    Noise_Init();
    if(!Perf_Init()) {
        LOGE("Perf_Init failed");
        return false;
    }

    if(Settings_Init() != SS_OKAY) {
        LOGE("Settings_Init failed");
        return false;
    }
    (void)Settings_LoadFromFile();

    if(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    stbi_set_flip_vertically_on_load(true);
    R_InitAttributes();

    if(!R_InitFromCurrentContext(w, h)) {
        LOGE("R_InitFromCurrentContext failed");
        return false;
    }
    LOGI("GL subsystems ready %dx%d", w, h);

    Perf_RegisterThread(g_main_thread_id, "android-gl");

    if(!Sched_Init()) { LOGE("Sched_Init"); return false; }
    if(!Session_Init()) { LOGE("Session_Init"); return false; }
    if(!AL_Init()) { LOGE("AL_Init"); return false; }
    (void)AssetCache_Init();

    if(!Cursor_InitDefault(g_basepath))
        LOGI("Cursor_InitDefault skipped/failed (ok on Android)");

    if(!E_Init()) { LOGE("E_Init"); return false; }
    if(!Entity_Init()) { LOGE("Entity_Init"); return false; }
    if(!A_Init()) { LOGE("A_Init"); return false; }
    if(!G_Init()) { LOGE("G_Init"); return false; }
    if(!R_Init(g_basepath)) { LOGE("R_Init"); return false; }

    if(!UI_Init(g_basepath, NULL))
        LOGI("UI_Init failed (continuing without nuklear fonts)");

    if(!N_Init()) { LOGE("N_Init"); return false; }
    if(!Audio_Init()) { LOGE("Audio_Init"); return false; }
    if(!P_Projectile_Init()) { LOGE("P_Projectile_Init"); return false; }
    if(!Sprite_Init()) { LOGE("Sprite_Init"); return false; }

    s_inited_subsystems = true;

    if(!android_load_map_and_scene())
        return false;

    s_playing = true;
    LOGI("Android RTS boot complete");
    return true;
}

static void android_inject_button(int type, float x, float y, Uint8 button, Uint8 clicks)
{
    Engine_SetPointer((int)x, (int)y);
    memset(&s_touch_ev, 0, sizeof(s_touch_ev));
    if(type == 0) {
        s_touch_ev.type = SDL_MOUSEBUTTONDOWN;
        s_touch_ev.button.type = SDL_MOUSEBUTTONDOWN;
        s_touch_ev.button.button = button;
        s_touch_ev.button.state = SDL_PRESSED;
        s_touch_ev.button.clicks = clicks;
        s_touch_ev.button.x = (Sint32)x;
        s_touch_ev.button.y = (Sint32)y;
        E_Global_NotifyImmediate(SDL_MOUSEBUTTONDOWN, &s_touch_ev, ES_ENGINE);
    }else if(type == 2) {
        s_touch_ev.type = SDL_MOUSEBUTTONUP;
        s_touch_ev.button.type = SDL_MOUSEBUTTONUP;
        s_touch_ev.button.button = button;
        s_touch_ev.button.state = SDL_RELEASED;
        s_touch_ev.button.clicks = clicks;
        s_touch_ev.button.x = (Sint32)x;
        s_touch_ev.button.y = (Sint32)y;
        E_Global_NotifyImmediate(SDL_MOUSEBUTTONUP, &s_touch_ev, ES_ENGINE);
    }
}

static void android_inject_motion(float x, float y, float dx, float dy)
{
    Engine_SetPointer((int)x, (int)y);
    memset(&s_touch_ev, 0, sizeof(s_touch_ev));
    s_touch_ev.type = SDL_MOUSEMOTION;
    s_touch_ev.motion.type = SDL_MOUSEMOTION;
    s_touch_ev.motion.x = (Sint32)x;
    s_touch_ev.motion.y = (Sint32)y;
    s_touch_ev.motion.xrel = (Sint32)dx;
    s_touch_ev.motion.yrel = (Sint32)dy;
    s_touch_ev.motion.state = SDL_BUTTON_MMASK; /* treat as middle-drag for pan */
    E_Global_NotifyImmediate(SDL_MOUSEMOTION, &s_touch_ev, ES_ENGINE);
}

#endif /* PF_ANDROID_ENGINE_LINKED */

bool pf_android_engine_init(const char *files_dir, int w, int h)
{
    snprintf(s_dir, sizeof(s_dir), "%s", files_dir ? files_dir : ".");
    s_w = w;
    s_h = h;
    s_ready = true;
    s_paused = false;
    s_playing = false;
    s_water_fx = (s_quality > 0);
    s_map_path[0] = '\0';
    s_scene_path[0] = '\0';
    pf_android_set_drawable_size(w, h);

    if(find_demo_map(s_dir, s_map_path, sizeof(s_map_path))) {
        LOGI("demo map found: %s", s_map_path);
        derive_scene_path(s_map_path, s_scene_path, sizeof(s_scene_path));
        if(s_scene_path[0])
            LOGI("scene candidate: %s", s_scene_path);
    }else{
        LOGI("no .pfmap under %s/maps (copy assets into APK)", s_dir);
    }

#if defined(PF_ANDROID_ENGINE_LINKED)
    if(!android_engine_boot(w, h)) {
        LOGE("android_engine_boot failed — staying non-playing");
        s_playing = false;
        /* Still return true so Activity keeps pumping; stub triangle may show. */
    }
#else
    LOGI("engine stub (PF_ANDROID_ENGINE_LINKED off)");
#endif

    LOGI("engine_init base=%s %dx%d single_gl=%d water_fx=%d playing=%d",
         s_dir, s_w, s_h, (int)s_single_gl_thread, (int)s_water_fx, (int)s_playing);
    return true;
}

void pf_android_engine_resize(int w, int h)
{
    s_w = w;
    s_h = h;
    pf_android_set_drawable_size(w, h);
#if defined(PF_ANDROID_ENGINE_LINKED)
    if(s_playing) {
        int x = 0, y = 0;
        R_GL_SetViewport(&x, &y, &s_w, &s_h);
    }
#endif
}

void pf_android_engine_frame(void)
{
    if(!s_ready || s_paused)
        return;
#if defined(PF_ANDROID_ENGINE_LINKED)
    if(!s_playing)
        return;
    /* Align with desktop main.c ENGINE_STATE_RUNNING order */
    E_ServiceQueue();
    LS_Service(); /* lobby pump when idle; lockstep sim when active */
    G_Update();
    G_Render();
    G_SwapBuffers();
    g_frame_idx++;
#endif
}

void pf_android_engine_pause(void)
{
    s_paused = true;
    LOGI("engine pause");
}

void pf_android_engine_resume(void)
{
    s_paused = false;
    LOGI("engine resume");
}

void pf_android_engine_shutdown(void)
{
    s_ready = false;
    s_playing = false;
    LOGI("engine shutdown");
}

void pf_android_engine_touch(int type, float x, float y, int id)
{
    (void)id;
#if defined(PF_ANDROID_ENGINE_LINKED)
    if(!s_playing) {
        s_last_x = x;
        s_last_y = y;
        return;
    }

    const float tap_slop = 28.0f;

    if(type == 0) {
        s_down_x = s_last_x = x;
        s_down_y = s_last_y = y;
        s_touch_moved = false;
        /* Middle button starts RTS drag-pan */
        android_inject_button(0, x, y, SDL_BUTTON_MIDDLE, 1);
        return;
    }

    if(type == 1) {
        float dx = x - s_last_x;
        float dy = y - s_last_y;
        if(fabsf(dx) + fabsf(dy) > 2.0f)
            s_touch_moved = true;
        android_inject_motion(x, y, dx, dy);
        s_last_x = x;
        s_last_y = y;
        return;
    }

    if(type == 2) {
        android_inject_button(2, x, y, SDL_BUTTON_MIDDLE, 1);
        float dx = x - s_down_x;
        float dy = y - s_down_y;
        if(!s_touch_moved && fabsf(dx) + fabsf(dy) <= tap_slop) {
            enum selection_type st;
            const vec_entity_t *sel = G_Sel_Get(&st);
            bool have_sel = sel && vec_size(sel) > 0;
            if(have_sel) {
                /* Tap with selection → right-click move */
                android_inject_button(0, x, y, SDL_BUTTON_RIGHT, 1);
                android_inject_button(2, x, y, SDL_BUTTON_RIGHT, 1);
            }else{
                /* Tap with no selection → left-click select */
                android_inject_button(0, x, y, SDL_BUTTON_LEFT, 1);
                android_inject_button(2, x, y, SDL_BUTTON_LEFT, 1);
            }
        }
        s_last_x = x;
        s_last_y = y;
    }
#else
    if(type == 0) {
        s_last_x = x;
        s_last_y = y;
    }else if(type == 1) {
        s_last_x = x;
        s_last_y = y;
    }else if(type == 2) {
        LOGI("engine touch up %.0f,%.0f", x, y);
    }
#endif
}

void pf_android_engine_set_quality(int level)
{
    s_quality = level;
    s_water_fx = (level > 0);
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

bool pf_android_engine_is_playing(void)
{
    return s_playing;
}
