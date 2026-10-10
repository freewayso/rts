/*
 * Audio stubs when OpenAL Soft is not linked (PF_ANDROID_LINK_ENGINE batch 1).
 */
#include "../src/audio/public/audio.h"

#include <stddef.h>
#include <string.h>

bool Audio_Init(void) { return true; }
void Audio_Shutdown(void) {}
bool Audio_PlayMusic(const char *name) { (void)name; return true; }
void Audio_PlayMusicFirst(void) {}
bool Audio_PlayForegroundEffect(const char *name, bool interrupt, int channel)
{
    (void)name; (void)interrupt; (void)channel;
    return true;
}
size_t Audio_GetAllMusic(size_t maxout, const char *out[])
{
    (void)maxout; (void)out;
    return 0;
}
const char *Audio_CurrMusic(void) { return ""; }
bool Audio_Effect_Add(vec3_t pos, const char *track)
{
    (void)pos; (void)track;
    return true;
}
void Audio_Pause(void) {}
void Audio_Resume(unsigned int dt) { (void)dt; }
void Audio_ClearState(void) {}
bool Audio_SaveState(struct SDL_RWops *stream) { (void)stream; return true; }
bool Audio_LoadState(struct SDL_RWops *stream) { (void)stream; return true; }
