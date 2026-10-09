/*
 * Android bootstrap: no Python. GLES clear + triangle (Phase 0–1),
 * optional full-engine hook (Phase 2+).
 */
#include "pf_android.h"

#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define LOG_TAG "Permafrost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

enum pf_android_mode{
    PF_MODE_STUB = 0,
    PF_MODE_ENGINE
};

static bool              s_inited;
static bool              s_paused;
static int               s_width;
static int               s_height;
static int               s_quality = 1;
static char              s_files_dir[512];
static enum pf_android_mode s_mode = PF_MODE_STUB;
static uint64_t          s_frame;

static float s_touch_x, s_touch_y;
static bool  s_touch_down;
static int   s_pointer_id = -1;

#ifdef __ANDROID__
#include <GLES3/gl3.h>

static GLuint s_prog;
static GLuint s_vao;
static GLuint s_vbo;
static bool   s_gl_ready;

static const char *VERT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec3 aCol;\n"
    "out vec3 vCol;\n"
    "void main(){\n"
    "  vCol = aCol;\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 vCol;\n"
    "out vec4 FragColor;\n"
    "void main(){ FragColor = vec4(vCol, 1.0); }\n";

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok) {
        char log[256];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        LOGE("shader: %s", log);
    }
    return s;
}

static bool stub_gl_init(void)
{
    if(s_gl_ready)
        return true;

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VERT_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    /* interleaved: x y r g b */
    const float verts[] = {
        0.0f,  0.55f, 0.2f, 0.9f, 0.4f,
       -0.55f,-0.45f, 0.9f, 0.3f, 0.2f,
        0.55f,-0.45f, 0.2f, 0.4f, 0.9f
    };
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    s_gl_ready = true;
    LOGI("stub GLES triangle ready");
    return true;
}

static void stub_gl_shutdown(void)
{
    if(!s_gl_ready)
        return;
    glDeleteBuffers(1, &s_vbo);
    glDeleteVertexArrays(1, &s_vao);
    glDeleteProgram(s_prog);
    s_gl_ready = false;
}

static void stub_draw_frame(void)
{
    if(!s_gl_ready)
        stub_gl_init();

    float t = (float)(s_frame % 120) / 120.0f;
    float g = 0.12f + 0.08f * t;
    glViewport(0, 0, s_width > 0 ? s_width : 1, s_height > 0 ? s_height : 1);
    glClearColor(0.04f, g, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if(s_gl_ready) {
        glUseProgram(s_prog);
        glBindVertexArray(s_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    if((s_frame % 60) == 0) {
        LOGI("frame=%llu size=%dx%d quality=%d paused=%d touch=%d",
             (unsigned long long)s_frame, s_width, s_height, s_quality,
             (int)s_paused, (int)s_touch_down);
    }
}
#else
static void stub_draw_frame(void) {}
static void stub_gl_shutdown(void) {}
#endif

/* RTS slice (android_rts_slice.c) */
extern void pf_rts_slice_reset(void);
extern void pf_rts_slice_on_touch(int type, float x, float y, int pointer_id);
extern void pf_rts_slice_tick(void);

bool pf_android_init(const char *files_dir, int width, int height)
{
    if(s_inited)
        return true;

    memset(s_files_dir, 0, sizeof(s_files_dir));
    if(files_dir)
        snprintf(s_files_dir, sizeof(s_files_dir), "%s", files_dir);

    s_width = width > 0 ? width : 1280;
    s_height = height > 0 ? height : 720;
    s_paused = false;
    s_frame = 0;
    s_mode = PF_MODE_STUB;
    pf_rts_slice_reset();

#if defined(PF_ANDROID_FULL)
    extern bool pf_android_engine_init(const char *files_dir, int w, int h);
    if(pf_android_engine_init(s_files_dir, s_width, s_height)) {
        s_mode = PF_MODE_ENGINE;
        LOGI("full engine bootstrap OK (still draws stub until game/render linked)");
    }else{
        LOGE("full engine bootstrap failed; staying in stub mode");
    }
#endif

#ifdef __ANDROID__
    stub_gl_init();
#endif

    s_inited = true;
    LOGI("pf_android_init dir=%s %dx%d mode=%d", s_files_dir, s_width, s_height, (int)s_mode);
    return true;
}

void pf_android_resize(int width, int height)
{
    if(width > 0)
        s_width = width;
    if(height > 0)
        s_height = height;
    LOGI("resize %dx%d", s_width, s_height);
#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_resize(int w, int h);
        pf_android_engine_resize(s_width, s_height);
    }
#endif
}

void pf_android_frame(void)
{
    if(!s_inited || s_paused)
        return;

    pf_rts_slice_tick();

#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_frame(void);
        pf_android_engine_frame();
        /* Until full render is linked, keep GLES triangle visible */
        stub_draw_frame();
        s_frame++;
        return;
    }
#endif
    stub_draw_frame();
    s_frame++;
}

void pf_android_pause(void)
{
    s_paused = true;
    LOGI("pause");
#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_pause(void);
        pf_android_engine_pause();
    }
#endif
}

void pf_android_resume(void)
{
    s_paused = false;
    LOGI("resume");
#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_resume(void);
        pf_android_engine_resume();
    }
#endif
}

void pf_android_shutdown(void)
{
    if(!s_inited)
        return;
#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_shutdown(void);
        pf_android_engine_shutdown();
    }
#endif
    stub_gl_shutdown();
    s_inited = false;
    LOGI("shutdown");
}

void pf_android_touch(int type, float x, float y, int pointer_id)
{
    s_touch_x = x;
    s_touch_y = y;
    s_pointer_id = pointer_id;
    if(type == 0)
        s_touch_down = true;
    else if(type == 2)
        s_touch_down = false;

    pf_rts_slice_on_touch(type, x, y, pointer_id);

#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_touch(int type, float x, float y, int id);
        pf_android_engine_touch(type, x, y, pointer_id);
    }
#endif
}

void pf_android_set_quality(int level)
{
    if(level < 0)
        level = 0;
    if(level > 2)
        level = 2;
    s_quality = level;
    LOGI("quality=%d", s_quality);
#if defined(PF_ANDROID_FULL)
    if(s_mode == PF_MODE_ENGINE) {
        extern void pf_android_engine_set_quality(int level);
        pf_android_engine_set_quality(level);
    }
#endif
}

int pf_android_get_quality(void)
{
    return s_quality;
}

int pf_android_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    LOGI("pf_android_main: Activity frame pump is the default host");
    return 0;
}
