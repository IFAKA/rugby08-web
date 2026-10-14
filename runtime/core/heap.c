/* heap.c -- the guest heap: one dlmalloc mspace over [RT_HEAP_LO, rt_heap_hi) of the arena.
 * Backs HeapAlloc/VirtualAlloc/GlobalAlloc/LocalAlloc and the runtime's own guest objects. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
/* emscripten's copy aliases malloc & co. to itself under __EMSCRIPTEN__; we want a private
 * mspace-only allocator on every target, so compile it as a plain C library. */
#ifdef __EMSCRIPTEN__
#define RT_WAS_EMSCRIPTEN 1
#undef __EMSCRIPTEN__
#endif
#define ONLY_MSPACES    1
#define MSPACES         1
#define HAVE_MMAP       0
#define HAVE_MORECORE   0
#define USE_LOCKS       1
#define USE_SPIN_LOCKS  0
#define NO_MALLOC_STATS 1
#define NO_MALLINFO     1
#define DLMALLOC_EXPORT static
#define MALLOC_FAILURE_ACTION
/* dlmalloc seeds its heap-check "magic" (stored in the heap header, guest memory) from time(0): a
 * constant instead, so the guest heap does not depend on the second the game started (netplay and
 * deterministic replays compare whole-memory hashes between instances started at different times). */
static time_t rt_heap_no_time(time_t *t) {
    (void)t;
    return 0;
}
#define time(t) rt_heap_no_time(t)
#include "../third_party/dlmalloc/dlmalloc.c" // NOLINT(bugprone-suspicious-include): built here, as a private allocator
#undef time
#ifdef RT_WAS_EMSCRIPTEN
#define __EMSCRIPTEN__ 1
#endif

#include "rt.h"

static mspace g_ms;
uint32_t rt_heap_hi;

void rt_heap_init(uint32_t hi) {
    rt_heap_hi = hi;
    g_ms = create_mspace_with_base(G2H(RT_HEAP_LO), hi - RT_HEAP_LO, 1);
    if (!g_ms) rt_fatal("guest heap: create_mspace_with_base failed");
}

/* End of the used part of the heap (the top chunk's start, 8-aligned): det.c hashes [LO, top). */
uint32_t rt_heap_top(void) { return (H2G(((mstate)g_ms)->top) + 7u) & ~7u; }

static uint32_t h2g_or0(void *p) { return p ? H2G(p) : 0; }

/* One player's own guest work (det_local.c) must leave the shared heap alone: every copy's heap is the
   same only if every copy allocates the same. Told, not stopped (the check is the copies' hashes). */
static void local_note(const char *what) {
    static uint32_t said;
    if (t_det_local && said != rt_present_frame) {
        said = rt_present_frame;
        rt_trace("det: heap %s inside one player's own work at frame %u", what, rt_present_frame);
    }
}

uint32_t gmalloc(uint32_t size) { return local_note("alloc"), h2g_or0(mspace_malloc(g_ms, size ? size : 1)); }
uint32_t gcalloc(uint32_t size) { return local_note("alloc"), h2g_or0(mspace_calloc(g_ms, 1, size ? size : 1)); }
uint32_t gmemalign(uint32_t al, uint32_t size) {
    return local_note("alloc"), h2g_or0(mspace_memalign(g_ms, al, size ? size : 1));
}
void gfree(uint32_t va) {
    local_note("free");
    if (va) mspace_free(g_ms, G2H(va));
}
uint32_t gmsize(uint32_t va) { return va ? (uint32_t)mspace_usable_size(G2H(va)) : 0; }
uint32_t grealloc(uint32_t va, uint32_t size) {
    if (!va) return gmalloc(size);
    return h2g_or0(mspace_realloc(g_ms, G2H(va), size ? size : 1));
}
uint32_t gstrdup(const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1, va = gmalloc(n);
    if (va) memcpy(G2H(va), s, n);
    return va;
}
