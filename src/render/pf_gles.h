/*
 * GLES / Android compatibility helpers for the render stack.
 */
#ifndef PF_GLES_H
#define PF_GLES_H

#if defined(__ANDROID__) || defined(PF_GLES)

#ifndef PF_GLES
#define PF_GLES 1
#endif

/* Clip planes / geometry / compute not available the same way as desktop GL */
#ifndef PF_GLES_HAS_GEOMETRY
#define PF_GLES_HAS_GEOMETRY 0
#endif
#ifndef PF_GLES_HAS_COMPUTE
#define PF_GLES_HAS_COMPUTE 0
#endif
/* Water reflection/refraction RTs: 0=off (default mobile), 1=allow */
#ifndef PF_GLES_WATER_FX
#define PF_GLES_WATER_FX 0
#endif

/* Prefer ES shader tree packed into APK filesDir */
#define PF_SHADER_ROOT_PREFIX "shaders_es"

#else

#define PF_GLES_HAS_GEOMETRY 1
#define PF_GLES_HAS_COMPUTE 1
#define PF_GLES_WATER_FX 1
#define PF_SHADER_ROOT_PREFIX "shaders"

#endif

#endif /* PF_GLES_H */
