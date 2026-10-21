/* platform/web/render_thread.c -- the browser's render thread (docs/plans/2026-10-21-render-thread.md):
 * a worker that owns the canvas (an OffscreenCanvas, transferred before the device makes its WebGL
 * context there) and runs the device calls the game posts (render/webgl/queue.cpp). A worker shows its
 * canvas only when it returns to its event loop, so it runs as a main loop of one tick a refresh: the
 * step batches that came, the open step to its end (never half a frame shown), and at a refresh without
 * a new step's frame the frame between the last step and the one before it. The game keeps the page
 * thread: its events, the canvas's size (asked from here, done at its next pump). Without
 * OffscreenCanvas, or RT_RENDER_THREAD=0 (tests), everything stays on the page thread, as before. */
#include "rt.h"
#include "plat.h"
#include "../sdl/sdl_int.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>

void d3d9w_rq_start(void);
double d3d9w_rq_run(double ms);
int d3d9w_context_create(void);

static atomic_int g_on, g_size, g_drawn_us;   /* g_size: w << 16 | h asked from the worker, 0 = none */
static pthread_t g_page;
static sem_t g_up;
static int g_made;   /* the worker made the canvas's WebGL context */
static double r_shown, r_slot, r_busy;   /* the last step's frame: when shown, its length; the open step's drawing */
static int r_swaps, r_open;              /* frames shown; a step's batches ran without its frame yet */

int render_threaded(void) { return atomic_load(&g_on); }
double render_drawn_ms(void) { return atomic_load(&g_drawn_us) / 1000.0; }

void render_swapped(double slot_ms) {   /* (worker) the step's frame is drawn: the browser shows it */
    r_shown = emscripten_get_now(), r_slot = slot_ms, r_swaps++;
}

static int run_wait(double ms) {
    int swaps = r_swaps;
    double ran = d3d9w_rq_run(ms);
    if (ran < 0) return 0;
    r_busy += ran;
    r_open = r_swaps == swaps;
    if (!r_open) atomic_store(&g_drawn_us, (int)(r_busy * 1000)), r_busy = 0;
    return 1;
}

static int r_between; /* the last tick drew a frame between steps: its overlay is undone at the next one */

static void tick(void) {   /* (one display refresh: the browser shows the canvas when it returns) */
    int frames = r_swaps;
    if (r_between) rt_interp_frame_end(), r_between = 0;
    while (r_swaps == frames && run_wait(0)) {}   /* (a step's frame a refresh: each one is shown) */
    for (double end = emscripten_get_now() + 100; r_open && emscripten_get_now() < end;) run_wait(5);
    if (r_swaps == frames && !r_open && interp_period() > 0)
        r_between = interp_refresh(r_shown, r_slot, emscripten_get_now(), NULL);
}

/* The refreshes: the browser's animation frames; RT_TEST_HZ=N (tests), an emulated display's, a timer to
   just before each one on a fixed grid and the rest waited out (a timer alone comes up to a ms late) */
static double r_hz, r_grid;
static EM_BOOL on_frame(double t, void *u) {
    (void)t, (void)u;
    tick();
    return EM_TRUE;
}
static void on_timer(void *u) {
    (void)u;
    while (emscripten_get_now() < r_grid) {}
    tick();
    double per = 1000.0 / r_hz, now = emscripten_get_now();
    r_grid = r_grid + per < now ? now + per : r_grid + per; /* (fell behind: from now) */
    emscripten_set_timeout(on_timer, r_grid - now - 1.5, NULL);
}

static void *render_main(void *unused) {
    (void)unused;
    d3d9w_rq_start();
    g_made = !d3d9w_context_create();
    sem_post(&g_up);
    r_hz = getenv("RT_TEST_HZ") ? atof(getenv("RT_TEST_HZ")) : 0;
    if (r_hz > 0) r_grid = emscripten_get_now(), on_timer(NULL);
    else emscripten_request_animation_frame_loop(on_frame, NULL);
    emscripten_exit_with_live_runtime();
}

// clang-format off
EM_JS(int, js_offscreen_ok, (void), {
    return typeof OffscreenCanvas !== 'undefined' && !!HTMLCanvasElement.prototype.transferControlToOffscreen;
});
// clang-format on

/* Before the device makes its WebGL context (render/webgl/factory.cpp): a context cannot move between
   threads, so the canvas goes to the worker first and the device is made there */
void plat_render_early(void) {
    const char *e = getenv("RT_RENDER_THREAD");
    if ((e && !atoi(e)) || render_threaded() || !js_offscreen_ok()) return;
    pthread_attr_t a;
    pthread_attr_init(&a);
    emscripten_pthread_attr_settransferredcanvases(&a, "#canvas");
    g_page = pthread_self();
    sem_init(&g_up, 0, 0);
    pthread_t t;
    if (pthread_create(&t, &a, render_main, NULL)) {
        rt_log("plat: no render thread");
        return;
    }
    pthread_detach(t);
    while (sem_trywait(&g_up)) plat_frame_yield(); /* (the browser makes a worker's context only with the page free) */
    if (!g_made) rt_fatal("plat: the render thread has no WebGL context");
    atomic_store(&g_on, 1);
    rt_log("plat: render thread on (OffscreenCanvas)");
}
void plat_render_start(void) {}
void plat_render_waiting(void) {}

int render_defer_size(int w, int h) { /* 1: asked from the worker, done at the page thread's next pump */
    if (!render_threaded() || pthread_equal(pthread_self(), g_page)) return 0;
    atomic_store(&g_size, w << 16 | h);
    return 1;
}
void render_pump(void) { /* (the page thread) */
    int s = atomic_exchange(&g_size, 0);
    if (s) plat_gl_set_size(s >> 16, s & 0xffff);
}
