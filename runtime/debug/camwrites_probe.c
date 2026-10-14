/* camwrites_probe.c -- RT_MS_CAMWRITES=N: what the camera writes (any game). Around the CameraManager's
 * event listener (every 0xAD, possession) and its step (every N-th frame), every 4 KiB page of the image,
 * the main stack and the heap is hashed before and after; the pages that changed are traced
 * ("camwrites: <what> at frame F: page ... "). One copy's own camera in a party (own_end.c) may only write
 * the camera's own objects: anything else it writes is the shared match seeing it. */
#include "rt.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t rt_heap_top(void);   /* heap.c */
#define PAGE 0x1000u
static uint64_t *g_before;
static uint32_t g_n, g_lo[3], g_hi[3];
static int g_armed;
static uint8_t g_img[0x10000];   /* image globals [IMG_LO, +64 KiB) before: which words changed */
#define IMG_LO 0x01400000u   /* (the globals the camera step writes: page 0x01409000, measured) */

static uint64_t page_hash(uint32_t a) {
    const uint64_t *p = (const uint64_t *)G2H(a);
    uint64_t h = 1469598103934665603ull;
    for (uint32_t k = 0; k < PAGE / 8; k++) h = (h ^ p[k]) * 1099511628211ull;
    return h;
}

static int on(uint32_t kind) {
    static long every = -2;
    if (every == -2) every = getenv("RT_MS_CAMWRITES") ? atol(getenv("RT_MS_CAMWRITES")) : -1;
    if (every <= 0) return 0;
    return kind == 0xAD || (kind == 0 && rt_present_frame % (uint32_t)every == 0);
}

void dbg_camwrites_begin(uint32_t kind) {
    g_armed = on(kind);
    if (!g_armed) return;
    g_lo[0] = RT_IMAGE_BASE, g_hi[0] = RT_IMAGE_END;
    g_lo[1] = RT_STACK0_LO, g_hi[1] = RT_STACK0_HI;
    g_lo[2] = RT_HEAP_LO, g_hi[2] = rt_heap_top();
    uint32_t n = 0;
    for (int r = 0; r < 3; r++) n += (g_hi[r] - g_lo[r] + PAGE - 1) / PAGE;
    if (n > g_n) free(g_before), g_before = (uint64_t *)malloc(n * sizeof *g_before), g_n = n;
    if (!g_before) g_armed = 0;
    if (g_armed) memcpy(g_img, G2H(IMG_LO), sizeof g_img);
    uint32_t i = 0;
    for (int r = 0; g_armed && r < 3; r++)
        for (uint32_t a = g_lo[r]; a < g_hi[r]; a += PAGE) g_before[i++] = page_hash(a);
}

void dbg_camwrites_end(uint32_t kind) {
    if (!g_armed) return;
    g_armed = 0;
    char out[512];
    int w = 0, n = 0;
    uint32_t i = 0;
    for (int r = 0; r < 3; r++)
        for (uint32_t a = g_lo[r]; a < g_hi[r]; a += PAGE, i++)
            if (page_hash(a) != g_before[i] && n++ < 40 && w < (int)sizeof out - 12)
                w += snprintf(out + w, sizeof out - (size_t)w, " %08X", a);
    rt_trace("camwrites: %s %X at frame %u: %d pages%s", kind ? "event" : "step", kind, rt_present_frame, n, out);
    w = 0, n = 0;   /* the image's globals: each changed word, before -> after */
    for (uint32_t o = 0; o < sizeof g_img; o += 4) {
        uint32_t was, now = rd32(IMG_LO + o);
        memcpy(&was, g_img + o, 4);
        if (was != now && n++ < 12 && w < (int)sizeof out - 40)
            w += snprintf(out + w, sizeof out - (size_t)w, " %08X:%08X>%08X", IMG_LO + o, was, now);
    }
    if (n) rt_trace("camwrites:  globals %d words%s", n, out);
}
