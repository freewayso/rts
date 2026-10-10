/*
 * Minimal GLES3 loader / compatibility shims for Android.
 * On PF_GLES / Android, <GL/glew.h> is the fake header under
 * jni/android_gl_headers (via CMake include path).
 */
#ifndef GLES_LOADER_H
#define GLES_LOADER_H

#if defined(__ANDROID__) || defined(PF_GLES)

#include <GL/glew.h>
#include <stdbool.h>

bool GLES_Loader_Init(void);
void GLES_Loader_Shutdown(void);

#else
/* Desktop: use real GLEW */
#include <GL/glew.h>
#endif

#endif
