/* platform/web/platform.c -- browser host glue. Window, events and input: platform/sdl/. */
#include "rt.h"
#include "plat.h"
#include "../sdl/sdl_int.h"
#include <emscripten.h>
#include <stdlib.h>

void rt_platform_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

/* The browser shows a frame only once the guest gives control back (JSPI suspends it here) and
   resumes it for the next one. ms > 0: time left to this frame's 60 Hz slot. Ahead of
   it, wait for the display's next refresh at or near the slot (requestAnimationFrame: the frame is
   drawn and composited in step with the screen, at 60 Hz on 120 Hz screens too). Behind it (or
   ms <= 0), come back at once through a MessageChannel: emscripten_sleep(0) is a setTimeout, which
   browsers stretch to 4 ms when nested, twice a frame before. A hidden tab (another tab in front, a
   minimized window) gets no animation frames and its timers fire once a second or less: a worker's
   timer paces it instead (browsers throttle neither a worker's timers nor its messages), so the game
   and a party's match keep their pace with the page out of view. */
// clang-format off
EM_ASYNC_JS(void, js_frame_wait, (double ms), {
    if (!Module.rtYield) {
        const ch = new MessageChannel(), q = [];
        ch.port1.onmessage = () => q.shift()();
        Module.rtYield = () => new Promise(r => {
                                  q.push(r);
                                  ch.port2.postMessage(0);
                              });
        const src = 'onmessage = e => setTimeout(() => postMessage(0), e.data)', tq = [];
        const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
        w.onmessage = () => tq.shift()();
        Module.rtTimer = ms => new Promise(r => {
                                   tq.push(r);
                                   w.postMessage(ms);
                               });
    }
    if (ms < 1) {
        await Module.rtYield();
        return;
    }
    const due = performance.now() + ms;   /* (emscripten_get_now counts from another origin with threads) */
    if (document.hidden) {
        await Module.rtTimer(due - performance.now());
        return;
    }
    while (performance.now() < due - 4) await new Promise(r => requestAnimationFrame(r));
});
// clang-format on
void plat_frame_yield(void) { js_frame_wait(0); }
/* fps.interp: one display refresh (0: the page is hidden, none comes); the display's refresh rate,
   counted from the browser's animation frames while the page is in view. */
// clang-format off
EM_ASYNC_JS(int, js_refresh_wait, (double due), {
    if (document.hidden) return 0;
    await new Promise(r => requestAnimationFrame(r));
    return 1;
});
EM_JS(double, js_display_hz, (void), {
    if (!Module.rtHzOn) {
        Module.rtHzOn = 1, Module.rtHz = 60;
        let n = 0, t0 = 0;
        const f = t => {
            if (!t0) t0 = t;
            if (++n, t - t0 >= 1000) {
                if (!document.hidden) Module.rtHz = n * 1000 / (t - t0);
                n = 0, t0 = t;
            }
            requestAnimationFrame(f);
        };
        requestAnimationFrame(f);
    }
    return Module.rtHz;
});
// clang-format on
double plat_display_hz(void) { return js_display_hz(); }
static double clock_ms(void) { return emscripten_get_now(); }
static void swap_none(void) {}   /* (the browser shows the canvas at the next wait) */

/* RT_TEST_DRAW_MS=N (tests only, ?env=): every drawn frame costs N ms more, a slow GPU (fps_skip_check) */
static void test_draw_cost(void) {
    static double cost = -1;
    if (cost < 0) cost = getenv("RT_TEST_DRAW_MS") ? atof(getenv("RT_TEST_DRAW_MS")) : 0;
    double end = emscripten_get_now() + cost;
    while (cost > 0 && emscripten_get_now() < end) {}
}

static double g_next, g_end;   /* the next frame's slot; when the last one came back from the browser */

/* A frame's slot: 60 frames per second of wall time; in deterministic mode (netplay) the virtual time
 * each Present advanced (det_period_ms). Drawn: the browser presents the canvas when control returns
 * to it (count the frame for the page's fps counter, shell Module.rtFrames). Not drawn (fps.skip):
 * control stays with the game, unless the slot is still ahead. What the frame cost (sdl_skip.c): its
 * work since the last one came back, and the browser's time past the slot (a slow GPU's backlog). */
static void frame_slot(int drawn) {
    double now = emscripten_get_now(), period = 1000.0 / 60.0, dp = det_period_ms();
    if (rt_ff) {   /* catching up: the joining screen, shown but never paced */
        g_next = g_end = 0;
        skip_reset();
        if (drawn) js_frame_wait(0);
        return;
    }
    if (g_next == 0 || now - g_next > 4 * period) g_next = now;   /* first frame, or fell far behind: resync */
    g_next += dp < 0 ? period : dp;
    if (drawn) interp_fill(now, g_next, dp < 0 ? period : dp, clock_ms, js_refresh_wait, swap_none); /* fps.interp */
    double back = emscripten_get_now();
    if (drawn || g_next > back + 1) js_frame_wait(g_next - back);
    double t1 = emscripten_get_now(), late = t1 - (g_next > now ? g_next : now);
    if (g_end > 0) skip_frame_done(drawn, now - g_end + (late > 0 ? late : 0), dp < 0 ? period : dp);
    g_end = t1;
    /* perf: game work per frame (resume to Present) vs time handed to the browser, every 600 frames */
    static double last, work, worst, wait;
    static int n, undrawn;
    if (last > 0) {
        double w = now - last;
        work += w;
        if (w > worst) worst = w;
        wait += t1 - now;
        n++;
        undrawn += !drawn;
    }
    last = t1;
    if (n == 600) {
        rt_trace(
            "web perf: %.0f fps (%d of 600 steps not drawn), work %.1f ms/frame (worst %.1f), browser %.1f ms/frame",
            600000.0 / (work + wait) * (600 - undrawn) / 600, undrawn, work / n, worst, wait / n);
        work = worst = wait = 0;
        n = undrawn = 0;
    }
}

void plat_gl_swap(void) {
    EM_ASM(Module.rtFrames = (Module.rtFrames | 0) + 1);
    test_draw_cost();
    frame_slot(1);
}

int plat_frame_late(void) { return !rt_ff && skip_next(); }

void plat_frame_skip(void) { frame_slot(0); }

/* wasm32: guest VAs wrap mod 2^32 through ADDR(); the first 1 GiB is backed. */
void *plat_guest_arena(uint32_t *size) {
    *size = 0x40000000u;
    void *arena = calloc(1, *size);
    if (!arena) rt_fatal("arena: cannot allocate %u bytes", *size);
    return arena;
}

/* The render thread is native only for now (the browser's needs an OffscreenCanvas: docs/plans/2026-10-21-render-thread.md) */
void plat_render_start(void) {}
int render_threaded(void) { return 0; }
void render_swapped(double slot_ms) { (void)slot_ms; }
int render_defer_size(int w, int h) {
    (void)w, (void)h;
    return 0;
}
void render_pump(void) {}
