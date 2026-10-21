/* platform/native/render_thread.c -- the render thread (docs/plans/2026-10-21-render-thread.md). It owns
 * the GL context and runs the device calls the game's thread posts (render/webgl/queue.cpp), a step's
 * batch at a time; after a step's frame is shown it fills the wait for the next step with in-between
 * frames (interp_fill), beside the game instead of after it. The game's thread keeps the window and
 * its events: a resize asked from here is done at its next pump. */
#include "../sdl/sdl_int.h"
#include <OpenGL/gl3.h>   /* glFinish */

void d3d9w_rq_start(void);
double d3d9w_rq_run(double ms);

static SDL_atomic_t g_on;
static SDL_sem *g_up;
static SDL_threadID g_tid;
static SDL_atomic_t g_size;   /* a window size asked from the render thread (w << 16 | h), 0 = none */
static double r_shown, r_slot;   /* the last step's frame: when shown, its length (ms) */
static int r_swapped, r_swaps;   /* a step's frame is shown and no step is open; frames shown */
static double r_busy;            /* the open step's drawing so far (ms) */
static SDL_atomic_t g_drawn_us;  /* what the last step's frame cost this thread (us): the frame skipper's */

int render_threaded(void) { return SDL_AtomicGet(&g_on); }

static double clock_ms(void) {
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}
static void wait_until(double due) {
    double ms = due - clock_ms();
    if (ms > 1.5) SDL_Delay((uint32_t)(ms - 1));
    while (clock_ms() < due) {}
}
static void swap_raw(void) { SDL_GL_SwapWindow(g_win), glFinish(); }

void render_swapped(double slot_ms) {   /* (render thread) the step's frame is shown */
    r_shown = clock_ms(), r_slot = slot_ms, r_swapped = 1, r_swaps++;
}

/* A batch: a step's calls up to its frame, or part of one (the game waits for a readback). Until the
   step's frame, no refresh: a replay of the step before would change the state the rest of it starts
   from, and so what a later readback reads. */
static int run_wait(double ms) {
    int swaps = r_swaps;
    double ran = d3d9w_rq_run(ms);
    if (ran < 0) return 0;
    r_busy += ran;
    if (r_swaps == swaps) r_swapped = 0;
    else SDL_AtomicSet(&g_drawn_us, (int)(r_busy * 1000)), r_busy = 0;
    return 1;
}

double render_drawn_ms(void) { return SDL_AtomicGet(&g_drawn_us) / 1000.0; }

static int render_main(void *unused) {
    (void)unused;
    SDL_GL_MakeCurrent(g_win, g_gl);
    d3d9w_rq_start();
    SDL_SemPost(g_up);
    /* The batches as they come; and as a display does, at each refresh without a new step's frame the
       frame between the last step and the one before it at that time (RT64's way). */
    double grid = clock_ms();
    for (;;) {
        double per = interp_period();
        if (per <= 0) {
            run_wait(100);
            grid = clock_ms();
            continue;
        }
        grid += per;
        if (grid < clock_ms() - per) grid = clock_ms();   /* (fell behind: from now) */
        int frames = r_swaps;   /* until the refresh, what comes at once (the game may be waiting) */
        for (double ms; (ms = grid - clock_ms()) > 0.5;) run_wait(ms - 0.5);
        wait_until(grid);
        if (r_swaps == frames && r_swapped) interp_refresh(r_shown, r_slot, clock_ms(), swap_raw);
    }
    return 0;
}

/* After the device exists (render/webgl/factory.cpp). RT_RENDER_THREAD=0 (tests): all on one thread. */
void plat_render_start(void) {
    const char *e = getenv("RT_RENDER_THREAD");
    if ((e && !atoi(e)) || !g_win || !g_gl || render_threaded()) return;
    g_tid = SDL_ThreadID();
    /* SDL's swap updates a resized context on the main thread; waiting for it there deadlocks while the
       game's thread (the main one) waits for this one, so it is queued instead */
    SDL_SetHint("SDL_MAC_OPENGL_ASYNC_DISPATCH", "1");
    SDL_GL_MakeCurrent(g_win, NULL);
    g_up = SDL_CreateSemaphore(0);
    SDL_DetachThread(SDL_CreateThread(render_main, "render", NULL));
    SDL_SemWait(g_up);
    SDL_AtomicSet(&g_on, 1);
    rt_log("plat: render thread on");
}

int render_defer_size(int w, int h) {   /* 1: asked off the window's thread, done at its next pump */
    if (!render_threaded() || SDL_ThreadID() == g_tid) return 0;
    SDL_AtomicSet(&g_size, w << 16 | h);
    return 1;
}
void render_pump(void) {   /* (the window's thread) */
    int s = SDL_AtomicSet(&g_size, 0);
    if (s) plat_gl_set_size(s >> 16, s & 0xffff);
}
