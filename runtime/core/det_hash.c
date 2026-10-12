/* det_hash.c -- deterministic mode's state hash: 64-bit FNV over guest state (image, main stack,
 * heap) for synctests and desync checks, incremental per frame for netplay, and the per-frame
 * diagnosis switches (RT_DET_HASH, RT_DET_PAGES, RT_DET_DUMP). */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_range(uint64_t h, uint32_t lo, uint32_t hi) {
    const uint64_t *p = (const uint64_t *)G2H(lo);
    for (uint32_t n = (hi - lo) / 8; n; n--, p++) {
        h ^= *p;
        h *= 0x100000001B3ull;
        h ^= h >> 29;
    }
    return h;
}
uint32_t rt_heap_top(void); /* heap.c */
uint64_t det_hash(void) {
    uint64_t h = 0xCBF29CE484222325ull;
    h = hash_range(h, RT_IMAGE_BASE, RT_IMAGE_END);
    h = hash_range(h, RT_STACK0_LO, RT_STACK0_HI);
    h = hash_range(h, RT_HEAP_LO, rt_heap_top());
    return h;
}

/* Incremental hash (netplay): the guest state, as laid out at the cycle's first frame, is cut into
 * `parts` slices and slice frame % parts is hashed at that frame, so no frame pays for the whole
 * state. Returns 1 every `every` frames (parts is a multiple of it) with the hash of the slices of
 * those frames: a window of the state, the whole of it over a cycle. Same frames, same slices on
 * every instance: equal results iff the instances agree. (Browser: 120 parts cost 7% of a match
 * frame; 600 parts, hashed and exchanged every 120 frames, cost a fifth of that.) */
int det_hash_step(uint32_t frame, uint32_t parts, uint32_t every, uint64_t *out) {
    static uint64_t h;
    static uint32_t lo[3], hi[3], total;
    uint32_t k = frame % parts;
    if (k == 0 || (!total && k % every == 0)) { /* (the first window boundary: hashes from the start) */
        lo[0] = RT_IMAGE_BASE;
        hi[0] = RT_IMAGE_END;
        lo[1] = RT_STACK0_LO;
        hi[1] = RT_STACK0_HI;
        lo[2] = RT_HEAP_LO;
        hi[2] = rt_heap_top();
        total = 0;
        for (int r = 0; r < 3; r++) total += hi[r] - lo[r];
    } else if (!total) return 0; /* joined mid-cycle */
    if (k % every == 0) h = 0xCBF29CE484222325ull;
    uint32_t chunk = ((total + parts - 1) / parts + 7) & ~7u, from = k * chunk,
             to = from + chunk > total ? total : from + chunk;
    for (int r = 0, base = 0; r < 3 && from < to; base += (int)(hi[r] - lo[r]), r++) {
        uint32_t a = from > (uint32_t)base ? from - (uint32_t)base : 0, b = to - (uint32_t)base;
        uint32_t size = hi[r] - lo[r];
        if (a >= size || to <= (uint32_t)base) continue;
        if (b > size) b = size;
        h = hash_range(h, lo[r] + a, lo[r] + b);
    }
    if ((k + 1) % every == 0) {
        *out = h;
        return 1;
    }
    return 0;
}

/* RT_DET_HASH=N: "det: frame F hash H" every N frames. RT_DET_PAGES=F: page hashes at frame F
 * (one line per 64 KiB page) to locate the first difference between two runs. */
void det_hash_frame(uint32_t frame) {
    static int every = -1;
    static long pages_at = -1;
    if (every < 0) {
        const char *e = getenv("RT_DET_HASH"), *p = getenv("RT_DET_PAGES");
        every = e ? atoi(e) : 0;
        pages_at = p ? atol(p) : -1;
    }
    if (every > 0 && frame % (uint32_t)every == 0)
        rt_trace("det: frame %u hash %016llX vt %llu", frame, (unsigned long long)det_hash(),
                 (unsigned long long)det_now_ns());
    if ((long)frame == pages_at) {
        uint32_t ranges[3][2] = { { RT_IMAGE_BASE, RT_IMAGE_END },
                                  { RT_STACK0_LO, RT_STACK0_HI },
                                  { RT_HEAP_LO, rt_heap_top() } };
        for (int r = 0; r < 3; r++)
            for (uint32_t a = ranges[r][0]; a < ranges[r][1]; a += 0x10000) {
                uint32_t e = a + 0x10000 < ranges[r][1] ? a + 0x10000 : ranges[r][1];
                rt_log("det: page %08X %016llX", a, (unsigned long long)hash_range(0, a, e));
            }
        /* RT_DET_DUMP=ADDR[:ADDR...]: those 64 KiB pages' bytes in the log ("det: dump ADDR hex", 32
         * bytes a line, all-zero lines skipped), so two runs (native, browser) can be diffed */
        for (const char *d = getenv("RT_DET_DUMP"); d && *d; d = strchr(d, ':') ? strchr(d, ':') + 1 : "") {
            uint32_t a = (uint32_t)strtoul(d, NULL, 16) & ~0xFFFFu;
            for (uint32_t o = 0; o < 0x10000; o += 32) {
                char hex[65];
                int nz = 0;
                for (int i = 0; i < 32; i++) {
                    uint8_t b = rd8(a + o + (uint32_t)i);
                    nz |= b;
                    snprintf(hex + 2 * i, 3, "%02x", b);
                }
                if (nz) rt_log("det: dump %08X %s", a + o, hex);
            }
        }
    }
}
