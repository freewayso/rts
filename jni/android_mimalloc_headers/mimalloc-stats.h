#ifndef PF_ANDROID_MIMALLOC_STATS_STUB_H
#define PF_ANDROID_MIMALLOC_STATS_STUB_H
#include <stddef.h>
#include <string.h>

typedef struct mi_stats_s {
    size_t reserved;
} mi_stats_t;

static inline void mi_stats_get(size_t sz, mi_stats_t *out)
{
    (void)sz;
    if(out)
        memset(out, 0, sizeof(*out));
}

#endif
