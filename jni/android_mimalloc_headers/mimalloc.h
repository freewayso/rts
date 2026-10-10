#ifndef PF_ANDROID_MIMALLOC_STUB_H
#define PF_ANDROID_MIMALLOC_STUB_H
/* Stub mimalloc API for Android until NDK mimalloc is linked. */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static inline void *mi_malloc(size_t n) { return malloc(n); }
static inline void *mi_calloc(size_t c, size_t n) { return calloc(c, n); }
static inline void *mi_realloc(void *p, size_t n) { return realloc(p, n); }
static inline void  mi_free(void *p) { free(p); }

typedef struct mi_heap_s mi_heap_t;
typedef struct mi_heap_area_s { size_t reserved; } mi_heap_area_t;
typedef bool (*mi_block_visit_fun)(const mi_heap_t*, const mi_heap_area_t*, void*, size_t, void*);

static inline mi_heap_t *mi_heap_get_default(void) { return NULL; }
static inline bool mi_heap_visit_blocks(mi_heap_t *h, bool b, mi_block_visit_fun f, void *a)
{
    (void)h; (void)b; (void)f; (void)a;
    return true;
}
static inline void mi_collect(bool force) { (void)force; }
static inline void mi_stats_merge(void) {}

#endif
