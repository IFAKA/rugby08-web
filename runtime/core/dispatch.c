/* dispatch.c -- recomp_lookup: guest VA -> lifted function, for every indirect call and jump.
 *
 * The generated table (gen/recomp/recomp_dispatch.c) is sorted by VA; a binary search over its ~60k
 * entries is ~16 scattered loads per call, and the game makes thousands of indirect calls per frame
 * (vtables): it was the third hottest function in a match. Built once on first use, an open-addressed
 * hash of those same entries answers in about one load. Read-only after the build, so every guest
 * thread shares it without a lock. */
#include "rt.h"
#include "aems.h"
#include <pthread.h>

#define HASH_BITS 17 /* 131072 slots for ~60k functions: under half full, short probes */
#define HASH_SIZE (1u << HASH_BITS)

typedef struct {
    uint32_t va; /* 0 = empty: no function lives at VA 0 */
    recomp_func_t fn;
} slot_t;

static slot_t g_slots[HASH_SIZE];
static int g_ready;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static inline uint32_t slot_of(uint32_t va) { return (va * 0x9E3779B1u) >> (32 - HASH_BITS); /* Fibonacci hashing */ }

static void build(void) {
    if (recomp_dispatch_count > HASH_SIZE / 2) rt_fatal("dispatch hash: %u functions", recomp_dispatch_count);
    for (uint32_t i = 0; i < recomp_dispatch_count; i++) {
        uint32_t va = recomp_dispatch_table[i].address, s = slot_of(va);
        while (g_slots[s].va && g_slots[s].va != va) s = (s + 1) & (HASH_SIZE - 1);
        g_slots[s] = (slot_t){ va, recomp_dispatch_table[i].func };
    }
    __atomic_store_n(&g_ready, 1, __ATOMIC_RELEASE);
}

recomp_func_t recomp_lookup(uint32_t va) {
    if (__builtin_expect(!__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE), 0)) pthread_once(&g_once, build);
    for (uint32_t s = slot_of(va);; s = (s + 1) & (HASH_SIZE - 1)) {
        if (g_slots[s].va == va && va) return g_slots[s].fn;
        if (!g_slots[s].va) break;
    }
    /* outside the image: run-time loaded sound-bank code (runtime/core/aems.c) */
    return (va < RT_IMAGE_BASE || va >= RT_IMAGE_END) ? aems_lookup(va) : NULL;
}
