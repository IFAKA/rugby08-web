// frames.cpp -- what automated runs and debugging do at each Present (before the swap): the "frame N"
// trace line, the --frames exit with its --screenshot, replay `shot` PNGs, the menu-change probe of
// replays, and RT_TRACE_DRAWS_FRAME (draw trace of chosen frames, with a PNG of each).
#include "rt.h"
#include "plat.h"
#include "d3d9.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int rt_trace_vsc;   // RT_TRACE_DRAWS_FRAME is on for this frame: d3d8_device prints vertex shader constants

// Coarse 32x24 luma grid of the back buffer, to detect that an injected input changed the screen.
static std::vector<float> screen_grid(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<float> g(32 * 24, 0.f);
    for (int gy = 0; gy < 24; gy++)
        for (int gx = 0; gx < 32; gx++) {
            double sum = 0; int n = 0;
            for (int y = gy * h / 24; y < (gy + 1) * h / 24; y += 4)
                for (int x = gx * w / 32; x < (gx + 1) * w / 32; x += 4) {
                    const uint8_t *p = &px[((size_t)y * w + x) * 4];
                    sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; n++;
                }
            g[(size_t)gy * 32 + gx] = n ? (float)(sum / n) : 0.f;
        }
    return g;
}

static void menu_change_probe(uint32_t frame, int w, int h) {
    static uint32_t tracked; static bool armed; static std::vector<float> base;
    uint32_t le = plat_replay_last_event();
    if (le != tracked) { tracked = le; base = screen_grid(w, h); armed = true; return; }
    if (armed && frame >= le + 90) {
        std::vector<float> cur = screen_grid(w, h);
        double d = 0; for (size_t i = 0; i < cur.size(); i++) d += fabs(cur[i] - base[i]);
        d /= (double)cur.size();
        if (d > 8.0) rt_trace("menu state changed (input at frame %u, screen diff %.1f)", le, d);
        armed = false;
    }
}


// The presented frame (still in the back buffer, before the swap) as a PNG: --screenshot, replay shots.
static void save_frame_png(int w, int h, const char *path) {
    std::vector<uint8_t> px((size_t)w * h * 4), flip(px.size());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    int rc = rt_png_write(path, w, h, flip.data());
    rt_trace("screenshot %s %dx%d (%s)", path, w, h, rc == 0 ? "ok" : "failed");
}

void d3d8_debug_present(uint32_t frame, int w, int h) {
    if (rt_replay_path[0]) menu_change_probe(frame, w, h);
    if (frame == 1 || frame % 60 == 0 || (int)frame == rt_frame_limit) rt_trace("frame %u", frame);
    if (rt_frame_limit > 0 && (int)frame >= rt_frame_limit) {
        if (rt_screenshot_path[0]) save_frame_png(w, h, rt_screenshot_path);
        rt_trace("exit: frame limit %d reached", rt_frame_limit);
        exit(0);
    }
    if (rt_shot_path[0]) { save_frame_png(w, h, rt_shot_path); rt_shot_path[0] = 0; }
    {   // RT_TRACE_DRAWS_FRAME=N or A-B/S: the backend prints every draw of frame N (the one after this
        // Present), or of every S-th frame from A to B, each with a screenshot logs/trace_frames/fN.png
        static long from = -2, to, step = 1;
        if (from == -2) {
            const char *e = getenv("RT_TRACE_DRAWS_FRAME");
            from = -1;
            if (e && sscanf(e, "%ld-%ld/%ld", &from, &to, &step) < 2) { to = from; step = 1; }
            if (step < 1) step = 1;
        }
        long f = (long)frame;
        if (from >= 0 && f >= from && f <= to && (f - from) % step == 0 && from != to) {
            char p[64]; snprintf(p, sizeof p, "logs/trace_frames/f%ld.png", f);
            save_frame_png(w, h, p);
        }
        long next = f + 1;
        int on = from >= 0 && next >= from && next <= to && (next - from) % step == 0;
        if (on) printf("trace frame %ld\n", next);
        if (from >= 0) d3d9w_trace_draws(on);
        rt_trace_vsc = on;
    }
}

// RT_LOG_LIGHTS=F: the first 64 SetLight calls from presented frame F with their colours and the
// caller's return address, and the scene lighting object the game copies light 0 from (0x772CA6).
void d3d8_debug_light(uint32_t index, const D3DLIGHT9 *lp, uint32_t caller, uint32_t light_va) {
    const D3DLIGHT9 &l = *lp;
    static int n; static long from = getenv("RT_LOG_LIGHTS") ? atol(getenv("RT_LOG_LIGHTS")) : -1;
    int on = from >= 0 && (long)rt_present_frame >= from;
    if (on && n++ < 64)
        rt_log("SetLight %u type %d diffuse %.3f,%.3f,%.3f ambient %.3f,%.3f,%.3f dir %.2f,%.2f,%.2f from 0x%08X (light at 0x%08X)",
               index, (int)l.Type, l.Diffuse.r, l.Diffuse.g, l.Diffuse.b, l.Ambient.r, l.Ambient.g, l.Ambient.b,
               l.Direction.x, l.Direction.y, l.Direction.z, caller, light_va);
    if (on && index == 0 && n < 64) {   // the scene lighting object the game copies them from (0x772CA6)
        uint32_t list = rd32(0xA619C4), cur = rd32(0xA619C0), idx = cur ? rd32(cur + 4) : 0;
        uint32_t obj = list ? rd32(list + idx * 8 + 4) : 0;
        char buf[1024]; int k = 0;
        for (uint32_t o = 0; obj && o < 0x80; o += 4) {
            float f; uint32_t v = rd32(obj + o); memcpy(&f, &v, 4);
            k += snprintf(buf + k, sizeof buf - k, " +%02X=%g", o, f);
        }
        rt_log("  lighting object list=0x%08X cur=0x%08X idx=%u obj=0x%08X:%s", list, cur, idx, obj, buf);
    }
}
