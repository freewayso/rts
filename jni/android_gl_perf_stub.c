/*
 * No-op GL perf / VRAM / pipeline-stats for PF_GLES Android builds.
 * Desktop gl_perf.c uses ARB/NVX query extensions unavailable on ES3.
 */
#include "gl_perf.h"
#include "public/render.h"

#include <string.h>

bool g_trace_stalls = false;

void R_GL_PerfStallRecordWait(enum gl_stall_site site, GLenum first_result,
                              uint64_t elapsed_ticks)
{
    (void)site; (void)first_result; (void)elapsed_ticks;
}

void R_GL_PerfCallRecord(const char *expr, const char *file, int line,
                         uint64_t elapsed_ticks)
{
    (void)expr; (void)file; (void)line; (void)elapsed_ticks;
}

void R_GL_PerfStallFrameReport(void)
{
}

void R_GL_ReadVramStats(struct vram_stats *out)
{
    if(out)
        memset(out, 0, sizeof(*out));
}

void R_GL_PipelineStatsBegin(uint32_t *cookies)
{
    (void)cookies;
}

void R_GL_PipelineStatsEnd(void)
{
}

void R_GL_ResolvePipelineStats(uint32_t *cookies, struct gpu_frame_stats *out)
{
    (void)cookies;
    if(out)
        memset(out, 0, sizeof(*out));
}
