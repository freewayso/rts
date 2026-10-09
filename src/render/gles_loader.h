/*
 * Minimal GLES3 loader / compatibility shims for Android.
 * Prefer NDK <GLES3/gl3.h>; provide GLEW-like macros used by desktop code paths.
 */
#ifndef GLES_LOADER_H
#define GLES_LOADER_H

#if defined(__ANDROID__) || defined(PF_GLES)

#include <GLES3/gl3.h>
#include <stdbool.h>

#ifndef GLEW_OK
#define GLEW_OK 0
#endif

/* Desktop capability gates → false / conservative on ES */
#ifndef GLEW_VERSION_3_3
#define GLEW_VERSION_3_3 1
#endif
#ifndef GLEW_VERSION_4_3
#define GLEW_VERSION_4_3 0
#endif
#ifndef GLEW_KHR_debug
#define GLEW_KHR_debug 0
#endif
#ifndef GLEW_ARB_buffer_storage
#define GLEW_ARB_buffer_storage 0
#endif
#ifndef GLEW_ARB_copy_image
#define GLEW_ARB_copy_image 0
#endif
#ifndef GLEW_ARB_compute_shader
#define GLEW_ARB_compute_shader 0
#endif
#ifndef GLEW_ARB_shader_storage_buffer_object
#define GLEW_ARB_shader_storage_buffer_object 0
#endif
#ifndef GLEW_ARB_pipeline_statistics_query
#define GLEW_ARB_pipeline_statistics_query 0
#endif
#ifndef GLEW_NVX_gpu_memory_info
#define GLEW_NVX_gpu_memory_info 0
#endif

static inline int glewInit(void) { return GLEW_OK; }

bool GLES_Loader_Init(void);
void GLES_Loader_Shutdown(void);

#else
/* Desktop: use real GLEW */
#include <GL/glew.h>
#endif

#endif
