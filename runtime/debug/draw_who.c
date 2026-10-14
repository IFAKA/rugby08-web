/* draw_who.c -- RT_DRAW_WHO=A-B (diagnosis): every draw of presented frames A..B, with the guest code that
 * issued it: "draw: f F #N kind K prims P vb ib tex vs (the device's handles) recent V V V ..." (V: the
 * functions entered last on this thread, newest first; the engine draws from its render lists, so the
 * handles tell a HUD part, not V). To find which game function draws a
 * HUD part (own_hud.c: what one player's screen leaves out). Arms the entry ring (trace.c). */
#include "rt.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>

static long g_from = -2, g_to;
static uint32_t g_frame = ~0u, g_n;

int dbg_draw_who_on(void) {
    if (g_from == -2) {
        const char *e = getenv("RT_DRAW_WHO");
        g_from = -1;
        if (e && sscanf(e, "%ld-%ld", &g_from, &g_to) < 2) g_to = g_from;
    }
    return g_from >= 0;
}

/* RT_DRAW_HIDE_VB=V,V..: draws from those vertex buffers are left out (which HUD part is which). */
static int hide(uint32_t vb) {
    static uint32_t v[8];
    static int n = -1;
    if (n < 0) {
        n = 0;
        for (const char *e = getenv("RT_DRAW_HIDE_VB"); e && *e && n < 8;) {
            char *end;
            v[n++] = (uint32_t)strtoul(e, &end, 16);
            e = *end == ',' ? end + 1 : "";
        }
    }
    for (int i = 0; i < n; i++)
        if (v[i] == vb) return 1;
    return 0;
}

int dbg_draw_who(const uint32_t *a, int kind, uint32_t prims, uint32_t vb, uint32_t ib, uint32_t tex, uint32_t vs) {
    int h = hide(vb);
    if (!dbg_draw_who_on() || (long)rt_present_frame < g_from || (long)rt_present_frame > g_to) return h;
    if (g_frame != rt_present_frame) g_frame = rt_present_frame, g_n = 0;
    uint32_t v[256];
    int n = rt_enter_since(rt_enter_mark() - 256, v, 256);
    char line[400];
    int o = snprintf(line, sizeof line, "draw: f %u #%u kind %d prims %u vb %X ib %X tex %X vs %X recent",
                     rt_present_frame, g_n++, kind, prims, vb, ib, tex, vs);
    for (int i = n - 1, k = 0; i >= 0 && k < 24 && o < (int)sizeof line - 10; i--, k++)
        o += snprintf(line + o, sizeof line - (size_t)o, " %X", v[i]);
    rt_trace("%s", line);
    return h;
}

/* With RT_DRAW_WHO: every vertex / index buffer the game makes ("buffer: vb G len usage fvf pool"). */
void dbg_buffer_made(uint32_t buf, int vertex, uint32_t len, uint32_t usage, uint32_t fvf, uint32_t pool) {
    if (dbg_draw_who_on())
        rt_trace("buffer: %s %X len %u usage %X fvf %X pool %u at frame %u", vertex ? "vb" : "ib", buf, len, usage, fvf,
                 pool, rt_present_frame);
}

/* RT_PTR_FIND=FROM,TO,F (diagnosis): at frame F, the pointer paths (up to 4 steps, 0x400 bytes of each
   object) from guest address FROM to a word equal to TO: "ptr: FROM+o -> X+o -> ... = TO". To find how a
   game object (a marker) reaches the device's buffer it draws from. */
uint32_t rt_heap_top(void);
static void ptr_find(uint32_t at, uint32_t to, int depth, char *path, int len) {
    for (uint32_t o = 0; o < 0x400; o += 4) {
        uint32_t w = rd32(at + o);
        int n = snprintf(path + len, 400 - (size_t)len, " %X+%X", at, o);
        if (w == to) rt_trace("ptr:%s = %X", path, to);
        else if (depth > 0 && w >= 0x01465000u && w < rt_heap_top() && w != at)
            ptr_find(w, to, depth - 1, path, len + n);
        path[len] = 0;
    }
}

void dbg_ptr_find(uint32_t frame) {
    static uint32_t from, to, at = ~0u;
    static int parsed;
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("RT_PTR_FIND");
        if (!e || sscanf(e, "%x,%x,%u", &from, &to, &at) != 3) at = ~0u;
    }
    if (frame != at) return;
    char path[400] = "";
    ptr_find(from, to, 3, path, 0);
    rt_trace("ptr: search from %X for %X done", from, to);
}
