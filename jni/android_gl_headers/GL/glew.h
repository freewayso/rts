/*
 * Android / PF_GLES stand-in for <GL/glew.h>.
 * Maps desktop GLEW includes to GLES3 + stubbed desktop-only entry points
 * so engine sources can compile into libpermafrost.so. Runtime paths that
 * hit stubs must stay disabled (quality / PF_GLES_* / extension flags = 0).
 */
#ifndef PF_ANDROID_FAKE_GLEW_H
#define PF_ANDROID_FAKE_GLEW_H

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef GLEW_OK
#define GLEW_OK 0
#endif
#ifndef GLEW_VERSION_3_3
#define GLEW_VERSION_3_3 1
#endif
#ifndef GLEW_VERSION_4_3
#define GLEW_VERSION_4_3 0
#endif

/* ---- Extension capability flags (runtime GLEW style) ---- */
#define PF_GLES_EXT0 0
#ifndef GLEW_ARB_multi_draw_indirect
#define GLEW_ARB_multi_draw_indirect PF_GLES_EXT0
#endif
#ifndef GL_ARB_multi_draw_indirect
#define GL_ARB_multi_draw_indirect PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_pipeline_statistics_query
#define GLEW_ARB_pipeline_statistics_query PF_GLES_EXT0
#endif
#ifndef GL_ARB_pipeline_statistics_query
#define GL_ARB_pipeline_statistics_query PF_GLES_EXT0
#endif
#ifndef GLEW_NVX_gpu_memory_info
#define GLEW_NVX_gpu_memory_info PF_GLES_EXT0
#endif
#ifndef GL_NVX_gpu_memory_info
#define GL_NVX_gpu_memory_info PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_shader_storage_buffer_object
#define GLEW_ARB_shader_storage_buffer_object PF_GLES_EXT0
#endif
#ifndef GL_ARB_shader_storage_buffer_object
#define GL_ARB_shader_storage_buffer_object PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_compute_shader
#define GLEW_ARB_compute_shader PF_GLES_EXT0
#endif
#ifndef GL_ARB_compute_shader
#define GL_ARB_compute_shader PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_clip_control
#define GLEW_ARB_clip_control PF_GLES_EXT0
#endif
#ifndef GL_ARB_clip_control
#define GL_ARB_clip_control PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_buffer_storage
#define GLEW_ARB_buffer_storage PF_GLES_EXT0
#endif
#ifndef GL_ARB_buffer_storage
#define GL_ARB_buffer_storage PF_GLES_EXT0
#endif
#ifndef GLEW_EXT_texture_filter_anisotropic
#define GLEW_EXT_texture_filter_anisotropic PF_GLES_EXT0
#endif
#ifndef GL_EXT_texture_filter_anisotropic
#define GL_EXT_texture_filter_anisotropic PF_GLES_EXT0
#endif
#ifndef GLEW_KHR_debug
#define GLEW_KHR_debug PF_GLES_EXT0
#endif
#ifndef GL_KHR_debug
#define GL_KHR_debug PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_provoking_vertex
#define GLEW_ARB_provoking_vertex PF_GLES_EXT0
#endif
#ifndef GL_ARB_provoking_vertex
#define GL_ARB_provoking_vertex PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_copy_image
#define GLEW_ARB_copy_image PF_GLES_EXT0
#endif
#ifndef GL_ARB_copy_image
#define GL_ARB_copy_image PF_GLES_EXT0
#endif
#ifndef GLEW_ARB_get_texture_sub_image
#define GLEW_ARB_get_texture_sub_image PF_GLES_EXT0
#endif
#ifndef GL_ARB_get_texture_sub_image
#define GL_ARB_get_texture_sub_image PF_GLES_EXT0
#endif

static inline int glewInit(void) { return GLEW_OK; }
static inline const char *glewGetErrorString(int e) { (void)e; return "ok"; }

/* ---- Desktop-only enum tokens ---- */
#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif
#ifndef GL_TEXTURE_BINDING_1D
#define GL_TEXTURE_BINDING_1D 0x8068
#endif
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#endif
#ifndef GL_TEXTURE_BUFFER_BINDING
#define GL_TEXTURE_BUFFER_BINDING 0x8C2C
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER_BINDING
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8F43
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif
#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_VERTICES_SUBMITTED
#define GL_VERTICES_SUBMITTED 0x82EE
#endif
#ifndef GL_PRIMITIVES_SUBMITTED
#define GL_PRIMITIVES_SUBMITTED 0x82EF
#endif
#ifndef GL_VERTEX_SHADER_INVOCATIONS
#define GL_VERTEX_SHADER_INVOCATIONS 0x82F0
#endif
#ifndef GL_CLIPPING_INPUT_PRIMITIVES
#define GL_CLIPPING_INPUT_PRIMITIVES 0x82F6
#endif
#ifndef GL_CLIPPING_OUTPUT_PRIMITIVES
#define GL_CLIPPING_OUTPUT_PRIMITIVES 0x82F7
#endif
#ifndef GL_FRAGMENT_SHADER_INVOCATIONS
#define GL_FRAGMENT_SHADER_INVOCATIONS 0x82F4
#endif
#ifndef GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#endif
#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX
#define GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX 0x904A
#endif
#ifndef GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX 0x904B
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_CLAMP_VERTEX_COLOR
#define GL_CLAMP_VERTEX_COLOR 0x891A
#endif
#ifndef GL_CLAMP_FRAGMENT_COLOR
#define GL_CLAMP_FRAGMENT_COLOR 0x891B
#endif
#ifndef GL_CLAMP_READ_COLOR
#define GL_CLAMP_READ_COLOR 0x891C
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#endif
#ifndef GL_CLIENT_STORAGE_BIT
#define GL_CLIENT_STORAGE_BIT 0x0200
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_COUNT
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE
#endif
#ifndef GL_FIRST_VERTEX_CONVENTION
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#endif
#ifndef GL_LAST_VERTEX_CONVENTION
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#endif
#ifndef GL_R8UI
#define GL_R8UI 0x8232
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif
#ifndef GL_DEBUG_SOURCE_API
#define GL_DEBUG_SOURCE_API 0x8246
#endif
#ifndef GL_DEBUG_SOURCE_WINDOW_SYSTEM
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#endif
#ifndef GL_DEBUG_SOURCE_SHADER_COMPILER
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#endif
#ifndef GL_DEBUG_SOURCE_THIRD_PARTY
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#endif
#ifndef GL_DEBUG_SOURCE_APPLICATION
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#endif
#ifndef GL_DEBUG_SOURCE_OTHER
#define GL_DEBUG_SOURCE_OTHER 0x824B
#endif
#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#endif
#ifndef GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#endif
#ifndef GL_DEBUG_TYPE_PORTABILITY
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif
#ifndef GL_DEBUG_TYPE_MARKER
#define GL_DEBUG_TYPE_MARKER 0x8268
#endif
#ifndef GL_DEBUG_TYPE_PUSH_GROUP
#define GL_DEBUG_TYPE_PUSH_GROUP 0x8269
#endif
#ifndef GL_DEBUG_TYPE_POP_GROUP
#define GL_DEBUG_TYPE_POP_GROUP 0x826A
#endif
#ifndef GL_DEBUG_SEVERITY_HIGH
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#endif
#ifndef GL_DEBUG_SEVERITY_MEDIUM
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#endif
#ifndef GL_DEBUG_SEVERITY_LOW
#define GL_DEBUG_SEVERITY_LOW 0x9148
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif
#ifndef GL_DEPTH_COMPONENT32
#define GL_DEPTH_COMPONENT32 0x81A7
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_FRONT_LEFT
#define GL_FRONT_LEFT 0x0400
#endif
#ifndef GL_FRONT_RIGHT
#define GL_FRONT_RIGHT 0x0401
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif
#ifndef GL_NONE
#define GL_NONE 0
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT 0x1001
#endif
#ifndef GL_TEXTURE_DEPTH
#define GL_TEXTURE_DEPTH 0x8071
#endif
#ifndef GL_TEXTURE_INTERNAL_FORMAT
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#endif

/* ---- Desktop entry points as no-ops / soft fallbacks ---- */
static inline void *glMapBuffer(GLenum target, GLenum access)
{
    (void)target; (void)access;
    return NULL;
}

static inline void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer)
{
    (void)target; (void)internalformat; (void)buffer;
}

static inline void glBufferStorage(GLenum target, GLsizeiptr size, const void *data,
                                   GLbitfield flags)
{
    /* Fallback: mutable buffer (ARB_buffer_storage path should be disabled). */
    (void)flags;
    glBufferData(target, size, data, GL_DYNAMIC_DRAW);
}

static inline void glMultiDrawArraysIndirect(GLenum mode, const void *indirect,
                                             GLsizei drawcount, GLsizei stride)
{
    (void)mode; (void)indirect; (void)drawcount; (void)stride;
}

static inline void glMultiDrawElementsIndirect(GLenum mode, GLenum type,
                                               const void *indirect,
                                               GLsizei drawcount, GLsizei stride)
{
    (void)mode; (void)type; (void)indirect; (void)drawcount; (void)stride;
}

static inline void glFramebufferTexture(GLenum target, GLenum attachment,
                                        GLuint texture, GLint level)
{
    (void)target; (void)attachment; (void)texture; (void)level;
}

static inline void glClampColor(GLenum target, GLenum clamp)
{
    (void)target; (void)clamp;
}

static inline void glPointSize(GLfloat size)
{
    (void)size;
}

static inline void glProvokingVertex(GLenum mode)
{
    (void)mode;
}

static inline void glDrawBuffer(GLenum mode)
{
    /* ES uses glDrawBuffers; single-buffer desktop call mapped. */
    GLenum bufs[1] = { mode };
    if(mode == 0)
        glDrawBuffers(0, NULL);
    else
        glDrawBuffers(1, bufs);
}

static inline void glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
    (void)id; (void)pname;
    if(params) *params = 0;
}

static inline void glGetQueryObjectui64v(GLuint id, GLenum pname, uint64_t *params)
{
    (void)id; (void)pname;
    if(params) *params = 0;
}

static inline void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y,
                                     GLuint num_groups_z)
{
    (void)num_groups_x; (void)num_groups_y; (void)num_groups_z;
}

static inline void glMemoryBarrier(GLbitfield barriers)
{
    (void)barriers;
}

static inline void glBindImageTexture(GLuint unit, GLuint texture, GLint level,
                                      GLboolean layered, GLint layer,
                                      GLenum access, GLenum format)
{
    (void)unit; (void)texture; (void)level; (void)layered;
    (void)layer; (void)access; (void)format;
}

static inline void glGetBufferSubData(GLenum target, GLintptr offset,
                                      GLsizeiptr size, void *data)
{
    (void)target; (void)offset;
    if(data && size > 0)
        memset(data, 0, (size_t)size);
}

static inline void glQueryCounter(GLuint id, GLenum target)
{
    (void)id; (void)target;
}

static inline void glPushDebugGroup(GLenum source, GLuint id, GLsizei length,
                                    const GLchar *message)
{
    (void)source; (void)id; (void)length; (void)message;
}

static inline void glPopDebugGroup(void)
{
}

static inline void glGetTexLevelParameteriv(GLenum target, GLint level,
                                            GLenum pname, GLint *params)
{
    (void)target; (void)level; (void)pname;
    if(params) *params = 0;
}

static inline void glGetTexImage(GLenum target, GLint level, GLenum format,
                                 GLenum type, void *pixels)
{
    (void)target; (void)level; (void)format; (void)type; (void)pixels;
}

static inline void glGetTextureSubImage(GLuint texture, GLint level,
                                        GLint xoffset, GLint yoffset, GLint zoffset,
                                        GLsizei width, GLsizei height, GLsizei depth,
                                        GLenum format, GLenum type,
                                        GLsizei bufSize, void *pixels)
{
    (void)texture; (void)level; (void)xoffset; (void)yoffset; (void)zoffset;
    (void)width; (void)height; (void)depth; (void)format; (void)type;
    (void)bufSize; (void)pixels;
}

static inline void glCopyImageSubData(GLuint srcName, GLenum srcTarget,
                                      GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ,
                                      GLuint dstName, GLenum dstTarget,
                                      GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
                                      GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{
    (void)srcName; (void)srcTarget; (void)srcLevel; (void)srcX; (void)srcY; (void)srcZ;
    (void)dstName; (void)dstTarget; (void)dstLevel; (void)dstX; (void)dstY; (void)dstZ;
    (void)srcWidth; (void)srcHeight; (void)srcDepth;
}

#endif
