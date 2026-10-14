// frames.cpp -- what automated runs and debugging do at each Present (before the swap): the "frame N"
// trace line, the --frames exit with its --screenshot, replay `shot` PNGs, the menu-change probe of
// replays, and RT_TRACE_DRAWS_FRAME (draw trace of chosen frames, with a PNG of each).
#include "rt.h"
#include "debug.h"
#include "plat.h"
#include "d3d9.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
            double sum = 0;
            int n = 0;
            for (int y = gy * h / 24; y < (gy + 1) * h / 24; y += 4)
                for (int x = gx * w / 32; x < (gx + 1) * w / 32; x += 4) {
                    const uint8_t *p = &px[((size_t)y * w + x) * 4];
                    sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
                    n++;
                }
            g[(size_t)gy * 32 + gx] = n ? (float)(sum / n) : 0.f;
        }
    return g;
}

static void menu_change_probe(uint32_t frame, int w, int h) {
    static uint32_t tracked;
    static bool armed;
    static std::vector<float> base;
    uint32_t le = plat_replay_last_event();
    if (le != tracked) {
        tracked = le;
        base = screen_grid(w, h);
        armed = true;
        return;
    }
    if (armed && frame >= le + 90) {
        std::vector<float> cur = screen_grid(w, h);
        double d = 0;
        for (size_t i = 0; i < cur.size(); i++) d += fabs(cur[i] - base[i]);
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

// RT_PIX=N: every N-th presented frame, "pix: frame F HASH" (FNV-1a of the game's picture, before the
// local overlays: badge, toasts): every player of a lockstep session must see the same picture.
void d3d8_debug_pixels(uint32_t frame, int w, int h) {
    static long every = -1;
    if (every < 0) every = getenv("RT_PIX") ? atol(getenv("RT_PIX")) : 0;
    if (every <= 0 || frame % every) return;
    std::vector<uint8_t> px((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t b : px) hash = (hash ^ b) * 1099511628211ull;
    uint32_t cam = 2166136261u;   // the game's camera (view, projection): game state, unlike the picture
    for (uint32_t va = 0xA60B68; va < 0xA60B68 + 64; va++) cam = (cam ^ rd8(va)) * 16777619u;
    for (uint32_t va = 0xA60C28; va < 0xA60C28 + 64; va++) cam = (cam ^ rd8(va)) * 16777619u;
    char blocks[16 * 12 * 4 + 1];   // 16x12 blocks, a 16-bit hash each: where two pictures differ
    for (int by = 0; by < 12; by++)
        for (int bx = 0; bx < 16; bx++) {
            uint32_t bh = 2166136261u;
            for (int y = by * h / 12; y < (by + 1) * h / 12; y++)
                for (int x = bx * w / 16 * 4; x < (bx + 1) * w / 16 * 4; x++)
                    bh = (bh ^ px[(size_t)y * w * 4 + x]) * 16777619u;
            snprintf(blocks + (by * 16 + bx) * 4, 5, "%04X", (bh ^ bh >> 16) & 0xFFFF);
        }
    rt_trace("pix: frame %u %016llX cam %08X blocks %s", frame, (unsigned long long)hash, cam, blocks);
    // RT_PIX_STRIP=1: with every picture, its top-left strip (blocks 0..5 of the top row: the HUD's score
    // and clock, where two games' pictures have parted for one frame) at half size, "pix: strip F W H HEX"
    static int strip = -1;
    if (strip < 0) strip = getenv("RT_PIX_STRIP") != NULL;
    if (strip) {
        static const char *hd = "0123456789ABCDEF";
        int x1 = w * 6 / 16, y0 = h * 11 / 12, sw = x1 / 2, sh = (h - y0) / 2;
        std::string hex = "";
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++) {
                const uint8_t *p = &px[((size_t)(y0 + 2 * y) * w + (size_t)(2 * x)) * 4];
                for (int c = 0; c < 3; c++) hex += hd[p[c] >> 4], hex += hd[p[c] & 15];
            }
        rt_trace("pix: strip %u %d %d %s", frame, sw, sh, hex.c_str());
    }
    // RT_PIX_DUMP=A-B[/S]: those frames also as "pix: dump F W H HEX" (RGB, bottom row first; 1/S of the
    // size, S 4 by default), to see a difference (tools/net/pix_dump.py turns them into PNGs)
    static long a = -2, b = -1, sc = 4;
    if (a == -2 && (a = -1, getenv("RT_PIX_DUMP"))) sscanf(getenv("RT_PIX_DUMP"), "%ld-%ld/%ld", &a, &b, &sc);
    if ((long)frame < a || (long)frame > b || sc < 1) return;
    int dw = w / (int)sc, dh = h / (int)sc;
    std::string hex = "";
    static const char *dig = "0123456789ABCDEF";
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            const uint8_t *p = &px[((size_t)(y * h / dh) * w + (size_t)(x * w / dw)) * 4];
            for (int c = 0; c < 3; c++) hex += dig[p[c] >> 4], hex += dig[p[c] & 15];
        }
    rt_trace("pix: dump %u %d %d %s", frame, dw, dh, hex.c_str());
}

void d3d8_debug_present(uint32_t frame, int w, int h) {
    ms_probe_frame(frame);   // match_probe.c: RT_MS_SCAN / RT_MS_DUMP / RT_MS_SNAP
    if (rt_replay_path[0]) menu_change_probe(frame, w, h);
    if (frame == 1 || frame % 60 == 0 || (int)frame == rt_frame_limit) rt_trace("frame %u", frame);
    if (rt_frame_limit > 0 && (int)frame >= rt_frame_limit) {
        if (rt_screenshot_path[0]) save_frame_png(w, h, rt_screenshot_path);
        rt_trace("exit: frame limit %d reached", rt_frame_limit);
        exit(0);
    }
    if (rt_shot_path[0]) {
        save_frame_png(w, h, rt_shot_path);
        rt_shot_path[0] = 0;
    }
    {   // RT_TRACE_DRAWS_FRAME=N or A-B/S: the backend prints every draw of frame N (the one after this
        // Present), or of every S-th frame from A to B, each with a screenshot logs/trace_frames/fN.png
        static long from = -2, to, step = 1;
        if (from == -2) {
            const char *e = getenv("RT_TRACE_DRAWS_FRAME");
            from = -1;
            if (e && sscanf(e, "%ld-%ld/%ld", &from, &to, &step) < 2) {
                to = from;
                step = 1;
            }
            if (step < 1) step = 1;
        }
        long f = (long)frame;
        if (from >= 0 && f >= from && f <= to && (f - from) % step == 0 && from != to) {
            char p[64];
            snprintf(p, sizeof p, "logs/trace_frames/f%ld.png", f);
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
    static int n;
    static long from = getenv("RT_LOG_LIGHTS") ? atol(getenv("RT_LOG_LIGHTS")) : -1;
    int on = from >= 0 && (long)rt_present_frame >= from;
    if (on && n++ < 64)
        rt_log("SetLight %u type %d diffuse %.3f,%.3f,%.3f ambient %.3f,%.3f,%.3f dir %.2f,%.2f,%.2f from 0x%08X "
               "(light at 0x%08X)",
               index, (int)l.Type, l.Diffuse.r, l.Diffuse.g, l.Diffuse.b, l.Ambient.r, l.Ambient.g, l.Ambient.b,
               l.Direction.x, l.Direction.y, l.Direction.z, caller, light_va);
    if (on && index == 0 && n < 64) {   // the scene lighting object the game copies them from (0x772CA6)
        uint32_t list = rd32(0xA619C4), cur = rd32(0xA619C0), idx = cur ? rd32(cur + 4) : 0;
        uint32_t obj = list ? rd32(list + idx * 8 + 4) : 0;
        char buf[1024];
        int k = 0;
        for (uint32_t o = 0; obj && o < 0x80; o += 4) {
            float f;
            uint32_t v = rd32(obj + o);
            memcpy(&f, &v, 4);
            k += snprintf(buf + k, sizeof buf - k, " +%02X=%g", o, f);
        }
        rt_log("  lighting object list=0x%08X cur=0x%08X idx=%u obj=0x%08X:%s", list, cur, idx, obj, buf);
    }
}
