/* platform/native/frame.c -- the swap, frame pacing (RT_FPS) and the once-a-second perf line. The web
 * build presents through the browser instead (platform/web/platform.c). */
#include "../sdl/sdl_int.h"
#include <OpenGL/gl3.h>   /* glFinish */

void d3d9w_rq_post(void (*f)(void *), void *arg);
void d3d9w_rq_hand(void);
int d3d9w_rq_syncs_take(void);
double d3d9w_rq_hand_waited_take(void); /* (with the lockstep waits: pacing, not work) */

/* Present at most 60 frames per second (RT_FPS=N, 0 = uncapped), as on the 60 Hz displays the game
   was made for. Vsync alone runs it at the host's refresh rate (120 Hz on ProMotion Macs), and the
   game's per-frame steering loses turns at a quarter of the time step it was tuned for: sprinting
   players could not change direction. */
static double g_wait_ticks;   /* time spent sleeping in frame_pace (not game work) */
static double g_period = -1, g_next;   /* a frame's length and the next frame's slot (ticks) */
static double g_end;                   /* when the last frame's wait ended: its work since (ticks) */
static int g_skipped;                  /* frames not drawn this second */

static double clock_ms(void) {
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}
static int wait_ms(double due) {
    double ms = due - clock_ms();
    if (ms > 1) SDL_Delay((uint32_t)ms - 1);
    while (clock_ms() < due) {}
    return 1;
}
static void swap_raw(void) {   /* a refresh between steps: shown, and its GPU work done (what it costs) */
    if (g_win && g_gl) SDL_GL_SwapWindow(g_win), glFinish();
}
double plat_display_hz(void) {
    SDL_DisplayMode m;
    return g_win && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(g_win), &m) == 0 && m.refresh_rate > 0
               ? m.refresh_rate
               : 60;
}

static void period_init(void) {
    if (g_period >= 0) return;
    const char *e = getenv("RT_FPS");
    int fps = e ? atoi(e) : 60;
    g_period = fps > 0 ? (double)SDL_GetPerformanceFrequency() / fps : 0;
    g_next = 0;
}

/* `dp`: in deterministic mode the virtual time this Present advanced (det_period_ms), else < 0 */
static void frame_pace(int drawn, double dp) {
    period_init();
    if (g_period <= 0) return;
    double now = (double)SDL_GetPerformanceCounter(), f = (double)SDL_GetPerformanceFrequency();
    if (drawn && render_threaded()) skip_render_done(render_drawn_ms());
    if (g_end > 0 && !rt_ff)
        skip_frame_done(drawn, (now - g_end) * 1000.0 / f - rt_net_waited_take() - d3d9w_rq_hand_waited_take(),
                        dp < 0 ? g_period * 1000.0 / f : dp);                       /* (its slot) */
    if (rt_ff) {   /* catching up (netplay): the joining screen, shown but never paced */
        g_next = g_end = 0;
        skip_reset();
        return;
    }
    if (g_next == 0 || now - g_next > 4 * g_period) g_next = now;   /* first frame, or fell far behind: resync */
    g_next += dp < 0 ? g_period : dp * (double)SDL_GetPerformanceFrequency() / 1000.0;
    if (drawn && !render_threaded())   /* (with a render thread, it fills them) */
        interp_fill(now * 1000.0 / f, g_next * 1000.0 / f, (dp < 0 ? g_period * 1000.0 / f : dp), clock_ms, wait_ms,
                    swap_raw);   /* (fps.interp: refreshes between the steps) */
    now = (double)SDL_GetPerformanceCounter();
    double wait = g_next - now;
    if (wait > 0) {
        uint32_t ms = (uint32_t)(wait * 1000.0 / (double)SDL_GetPerformanceFrequency());
        if (ms > 1) SDL_Delay(ms - 1);
        while ((double)SDL_GetPerformanceCounter() < g_next) {}
        g_wait_ticks += wait;
    }
    g_end = (double)SDL_GetPerformanceCounter();
}

int plat_frame_late(void) { return g_period > 0 && !rt_ff && skip_next(); }

void plat_frame_skip(void) {
    g_skipped++;
    frame_pace(0, det_period_ms());
}

/* RT_TEST_DRAW_MS=N (tests only): every drawn frame costs N ms more, a slow GPU's swap (fps_skip_check.sh) */
static void test_draw_cost(void) {
    static double cost = -1;
    if (cost < 0) {
        const char *e = getenv("RT_TEST_DRAW_MS");
        cost = e ? atof(e) * (double)SDL_GetPerformanceFrequency() / 1000.0 : 0;
    }
    double end = (double)SDL_GetPerformanceCounter() + cost;
    while (cost > 0 && (double)SDL_GetPerformanceCounter() < end) {}
}

/* A frame over 20 ms: where the renderer's part of it went (stutter hunting; render/webgl/cost.cpp).
   The rest is the game, the D3D8 layer (shader translation included) and the pacer's wait. */
static void slow_frame(double ms_frame, double ms_swap) {
    double ms[COST_N];
    int n[COST_N];
    d3d9w_cost_take(ms, n);
    rt_trace("perf: slow frame %.1f ms: programs %.1f ms (%d), textures %.1f (%d), buffers %.1f (%d), draws %.1f "
             "(%d), swap %.1f",
             ms_frame, ms[COST_PROGRAM], n[COST_PROGRAM], ms[COST_TEXTURE], n[COST_TEXTURE], ms[COST_BUFFER],
             n[COST_BUFFER], ms[COST_DRAW], n[COST_DRAW], ms_swap);
}

/* One "perf:" trace line per second: frames, average and worst frame time, the share of the frame
   spent in the swap itself and in the pacer's wait (the rest is the game and our translation). */
static void frame_stats(double swap_ticks) {
    static double last, t0, worst, swap_sum, over;
    static int n;
    double f = (double)SDL_GetPerformanceFrequency(), now = (double)SDL_GetPerformanceCounter();
    if (last) {
        double dt = now - last;
        if (dt > worst) worst = dt;
        if (dt > f / 60 * 1.25) over++, slow_frame(dt * 1000 / f, swap_ticks * 1000 / f);
        n++;
    }
    double ms[COST_N];
    int cn[COST_N];
    d3d9w_cost_take(ms, cn);   /* (this frame's renderer work starts now) */
    last = now;
    swap_sum += swap_ticks;
    if (!t0) t0 = now;
    if (now - t0 >= f && n) {
        double span = now - t0;
        rt_trace("perf: %d fps, %.1f steps/s (%d not drawn), avg %.1f ms, worst %.1f ms, %d frames >20 ms, swap %.1f "
                 "ms, paced-wait %.1f ms, %d frames between steps, %d queue waits",
                 (int)(n * f / span + 0.5), (n + g_skipped) * f / span, g_skipped, span * 1000 / f / n,
                 worst * 1000 / f, (int)over, swap_sum * 1000 / f / n, g_wait_ticks * 1000 / f / n,
                 interp_frames_take(), d3d9w_rq_syncs_take());
#ifdef __APPLE__
        /* Three slow seconds in a row: `sample` this process (5 s of stacks) into logs/, at most 3 times a
           run, so a slow scene explains itself without anyone attaching a profiler. */
        static int slow, taken;
        static int thr;
        if (!thr) {
            const char *e = getenv("RT_SLOW_FPS");
            thr = e ? atoi(e) : 24;
        }
        slow = (n + g_skipped) * f / span < thr ? slow + 1 : 0;   /* (the game's steps: drawn or not) */
        if (slow >= 3 && taken < 3) {
            char cmd[256];
            snprintf(cmd, sizeof cmd, "sample %d 5 -file logs/sample-auto-%d.txt >/dev/null 2>&1 &", (int)getpid(),
                     taken);
            if (system(cmd) == 0) // NOLINT(bugprone-command-processor): our own fixed `sample` command
                rt_trace("perf: slow scene, sampling into logs/sample-auto-%d.txt", taken);
            taken++;
            slow = -10;
        }
#endif
        t0 = now;
        n = 0;
        worst = swap_sum = over = g_wait_ticks = 0;
        g_skipped = 0;
    }
}

static void swap_posted(void *slot_us) {   /* (render thread) the step's frame */
    if (g_win && g_gl) SDL_GL_SwapWindow(g_win);
    test_draw_cost();
    render_swapped((double)(intptr_t)slot_us / 1000.0);
}

void plat_gl_swap(void) {
    double dp = det_period_ms();
    if (render_threaded()) {   /* the step's calls to the render thread, which shows them */
        period_init();
        double slot = dp >= 0 ? dp : g_period * 1000.0 / (double)SDL_GetPerformanceFrequency();
        d3d9w_rq_post(swap_posted, (void *)(intptr_t)(slot * 1000.0));
        d3d9w_rq_hand();
        frame_pace(1, dp);
        frame_stats(0);
        return;
    }
    double s0 = (double)SDL_GetPerformanceCounter();
    if (g_win && g_gl) SDL_GL_SwapWindow(g_win);
    test_draw_cost();
    double sw = (double)SDL_GetPerformanceCounter() - s0;
    frame_pace(1, dp);
    frame_stats(sw);
}

void plat_frame_yield(void) {}   /* the browser's JSPI yield; natively the OS schedules us */
