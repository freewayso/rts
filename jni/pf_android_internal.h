/*
 * Declarations shared by Android bootstrap / engine / RTS slice.
 */
#ifndef PF_ANDROID_INTERNAL_H
#define PF_ANDROID_INTERNAL_H

#include <stdbool.h>

bool pf_android_engine_init(const char *files_dir, int w, int h);
void pf_android_engine_resize(int w, int h);
void pf_android_engine_frame(void);
void pf_android_engine_pause(void);
void pf_android_engine_resume(void);
void pf_android_engine_shutdown(void);
void pf_android_engine_touch(int type, float x, float y, int id);
void pf_android_engine_set_quality(int level);
bool pf_android_engine_water_fx_enabled(void);
const char *pf_android_engine_map_path(void);

void pf_rts_slice_reset(void);
void pf_rts_slice_on_touch(int type, float x, float y, int pointer_id);
void pf_rts_slice_tick(void);
void pf_rts_slice_clear_selection(void);

void pf_rts_slice_resources_reset(void);
void pf_rts_slice_set_build_mode(bool on);
bool pf_rts_slice_try_build_at(float x, float y);
void pf_rts_slice_issue_attack(int attacker_uid, int target_uid);
void pf_rts_slice_get_resources(int *wood, int *stone, int *food);

#endif
