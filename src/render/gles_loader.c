#include "gles_loader.h"

#if defined(__ANDROID__) || defined(PF_GLES)

#include <android/log.h>
#define LOG_TAG "GLESLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

bool GLES_Loader_Init(void)
{
    LOGI("GLES3 loader ready (NDK headers)");
    return true;
}

void GLES_Loader_Shutdown(void)
{
}

#endif
