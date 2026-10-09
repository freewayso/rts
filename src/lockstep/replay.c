/*
 *  Command-frame recording and offline replay for determinism tests.
 */

#define MEM_FILE_SYS MEM_SYS_LOCKSTEP
#define MEM_FILE_SUB 0

#include "public/lockstep.h"
#include "../lib/public/pf_string.h"
#include "../mem.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define REPLAY_MAGIC 0x4C535250u /* 'LSRP' */
#define REPLAY_VER   1

struct replay_header{
    uint32_t magic;
    uint32_t version;
    uint64_t rng_seed;
    uint32_t sim_hz;
    uint32_t command_delay;
    uint32_t num_frames;
};

static FILE *s_record;
static bool  s_playing;
static struct ls_player_frame *s_frames;
static uint32_t s_frame_count;
static uint32_t s_frame_cap;
static uint32_t s_play_idx;
static struct ls_config s_replay_cfg;

bool LS_ReplayBeginRecord(const char *path)
{
    LS_ReplayEndRecord();
    if(!path)
        return false;
    s_record = fopen(path, "wb");
    if(!s_record)
        return false;
    struct replay_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REPLAY_MAGIC;
    hdr.version = REPLAY_VER;
    /* seed/hz filled at end or via Init — write placeholder, rewrite on end */
    fwrite(&hdr, sizeof(hdr), 1, s_record);
    return true;
}

void LS_ReplayRecordFrame(const struct ls_player_frame *frame)
{
    if(!s_record || !frame)
        return;
    fwrite(frame, sizeof(*frame), 1, s_record);
}

void LS_ReplayEndRecord(void)
{
    if(s_record) {
        fclose(s_record);
        s_record = NULL;
    }
}

bool LS_ReplayLoad(const char *path)
{
    FILE *f = fopen(path, "rb");
    if(!f)
        return false;
    struct replay_header hdr;
    if(fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != REPLAY_MAGIC) {
        fclose(f);
        return false;
    }
    free(s_frames);
    s_frames = NULL;
    s_frame_count = 0;
    s_frame_cap = 256;
    s_frames = calloc(s_frame_cap, sizeof(*s_frames));
    if(!s_frames) {
        fclose(f);
        return false;
    }
    struct ls_player_frame fr;
    while(fread(&fr, sizeof(fr), 1, f) == 1) {
        if(s_frame_count >= s_frame_cap) {
            s_frame_cap *= 2;
            struct ls_player_frame *n = realloc(s_frames, s_frame_cap * sizeof(*n));
            if(!n)
                break;
            s_frames = n;
        }
        s_frames[s_frame_count++] = fr;
    }
    fclose(f);
    s_playing = true;
    s_play_idx = 0;
    memset(&s_replay_cfg, 0, sizeof(s_replay_cfg));
    s_replay_cfg.rng_seed = hdr.rng_seed;
    s_replay_cfg.sim_hz = hdr.sim_hz ? hdr.sim_hz : LS_DEFAULT_HZ;
    s_replay_cfg.command_delay = hdr.command_delay ? hdr.command_delay : LS_DEFAULT_DELAY;
    s_replay_cfg.local_only = true;
    s_replay_cfg.num_players = 1;
    return true;
}

bool LS_ReplayPlaying(void)
{
    return s_playing;
}

bool LS_ReplayNextFrame(struct ls_player_frame *out)
{
    if(!s_playing || s_play_idx >= s_frame_count)
        return false;
    *out = s_frames[s_play_idx++];
    return true;
}

void LS_ReplayStop(void)
{
    s_playing = false;
    free(s_frames);
    s_frames = NULL;
    s_frame_count = 0;
}
