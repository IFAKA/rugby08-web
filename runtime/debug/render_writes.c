/* render_writes.c -- RT_RENDER_WRITES=F (native diagnosis): which guest memory the drawing part of frame
 * F writes (from the camera step to the Present), as ranges "dbg: render wrote ADDR LEN" -- to find match
 * state the game derives from the camera while drawing (own_camera.c: what must not differ per player). */
#include "rt.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>

uint32_t rt_heap_top(void);
static long g_at = -2;
static uint8_t *g_snap;
static uint32_t g_lo, g_hi;

void dbg_render_writes_begin(uint32_t frame) {
    if (g_at == -2) g_at = getenv("RT_RENDER_WRITES") ? atol(getenv("RT_RENDER_WRITES")) : -1;
    if ((long)frame != g_at || g_snap) return;
    g_lo = RT_IMAGE_BASE, g_hi = rt_heap_top();
    g_snap = malloc(g_hi - g_lo);
    if (g_snap) memcpy(g_snap, G2H(g_lo), g_hi - g_lo);
}

/* The players' objects (CRugby [0xA31988] +0x68 teams, CRugbyTeam +0xD4 players; dlmalloc chunk sizes),
   to tell their writes from the drawing's buffers. */
static int in_player(uint32_t a) {
    uint32_t r = rd32(0x00A31988u);
    for (int t = 0; r && t < 2; t++)
        for (int i = 0; i < 15; i++) {
            uint32_t tm = rd32(r + 0x68 + 4u * (uint32_t)t), p = tm ? rd32(tm + 0xD4 + 4u * (uint32_t)i) : 0;
            if (p && a >= p && a < p + (rd32(p - 4) & ~7u)) return t * 15 + i;
        }
    return -1;
}

void dbg_render_writes_end(uint32_t frame) {
    if (!g_snap || (long)frame < g_at) return;
    const uint8_t *now = (const uint8_t *)G2H(g_lo);
    uint32_t n = 0, start = 0, last = 0, total = 0;
    for (uint32_t i = 0; i < g_hi - g_lo; i++) {
        if (now[i] == g_snap[i]) continue;
        if (start && i - last > 16) {
            uint32_t a = g_lo + start - 1;
            rt_trace("dbg: render wrote %08X %u player %d", a, last - start + 2, in_player(a));
            n++;
            start = 0;
        }
        if (!start) start = i + 1;
        last = i;
        total++;
    }
    if (start) rt_trace("dbg: render wrote %08X %u", g_lo + start - 1, last - start + 2), n++;
    rt_trace("dbg: render wrote %u ranges, %u bytes, frame %u", n, total, frame);
    free(g_snap);
    g_snap = NULL;
    g_at = -1;
}
