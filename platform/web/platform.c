/* platform/web/platform.c -- browser host glue. Window, events and input: platform/sdl/. */
#include "rt.h"
#include "plat.h"
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
   browsers stretch to 4 ms when nested, twice a frame before. A hidden tab gets no animation
   frames: a timer keeps its game (and its friends' netplay) running. */
// clang-format off
EM_ASYNC_JS(void, js_frame_wait, (double ms), {
    if (!Module.rtYield) {
        const ch = new MessageChannel(), q = [];
        ch.port1.onmessage = () => q.shift()();
        Module.rtYield = () => new Promise(r => {
                                  q.push(r);
                                  ch.port2.postMessage(0);
                              });
    }
    if (ms < 1) {
        await Module.rtYield();
        return;
    }
    const due = performance.now() + ms;   /* (emscripten_get_now counts from another origin with threads) */
    if (document.hidden) {
        await new Promise(r => setTimeout(r, due - performance.now()));
        return;
    }
    while (performance.now() < due - 4) await new Promise(r => requestAnimationFrame(r));
});
// clang-format on
void plat_frame_yield(void) { js_frame_wait(0); }

/* The browser presents the canvas when control returns to it: count the frame for the page's fps
 * counter (shell Module.rtFrames), then wait for its slot: 60 frames per second of wall time; in
 * deterministic mode (netplay) the virtual time each Present advanced (det_period_ms). */
void plat_gl_swap(void) {
    EM_ASM(Module.rtFrames = (Module.rtFrames | 0) + 1);
    static double next;
    double now = emscripten_get_now(), period = 1000.0 / 60.0, dp = det_period_ms();
    if (next == 0 || now - next > 4 * period) next = now;   /* first frame, or fell far behind: resync */
    next += dp < 0 ? period : dp;
    js_frame_wait(next - now);
    /* perf: game work per frame (resume to Present) vs time handed to the browser, every 600 frames */
    static double last, work, worst, wait;
    static int n;
    double t1 = emscripten_get_now();
    if (last > 0) {
        double w = now - last;
        work += w;
        if (w > worst) worst = w;
        wait += t1 - now;
        n++;
    }
    last = t1;
    if (n == 600) {
        rt_trace("web perf: %.0f fps, work %.1f ms/frame (worst %.1f), browser %.1f ms/frame", 600000.0 / (work + wait),
                 work / n, worst, wait / n);
        work = worst = wait = 0;
        n = 0;
    }
}

/* wasm32: guest VAs wrap mod 2^32 through ADDR(); the first 1 GiB is backed. */
void *plat_guest_arena(uint32_t *size) {
    *size = 0x40000000u;
    void *arena = calloc(1, *size);
    if (!arena) rt_fatal("arena: cannot allocate %u bytes", *size);
    return arena;
}
