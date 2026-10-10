/*
 * Public C API for libpermafrost.so on Android.
 * Activity / SDL host calls these; no Python required for bootstrap.
 */
#ifndef PF_ANDROID_H
#define PF_ANDROID_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle ---------------------------------------------------------------- */
bool pf_android_init(const char *files_dir, int width, int height);
void pf_android_resize(int width, int height);
void pf_android_frame(void);
void pf_android_pause(void);
void pf_android_resume(void);
void pf_android_shutdown(void);

/* Touch: type 0=down 1=move 2=up; coords in pixels --------------------- */
void pf_android_touch(int type, float x, float y, int pointer_id);

/* Quality presets: 0=low 1=medium 2=high -------------------------------- */
void pf_android_set_quality(int level);
int  pf_android_get_quality(void);

/* Drawable size for Engine_WinDrawableSize (engine-link builds) --------- */
void pf_android_set_drawable_size(int w, int h);
void pf_android_get_drawable_size(int *out_w, int *out_h);

/* Optional: run blocking SDL_main-style loop (returns on quit) ---------- */
int  pf_android_main(int argc, char **argv);

/* Multiplayer (LAN P2P) — see android_mp.h; call on GL thread ----------- */
#include "android_mp.h"

#ifdef __cplusplus
}
#endif

#endif
