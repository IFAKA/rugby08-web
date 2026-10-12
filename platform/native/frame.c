/* platform/native/frame.c -- the swap, frame pacing (RT_FPS) and the once-a-second perf line. The web
 * build presents through the browser instead (platform/web/platform.c). */
#include "../sdl/sdl_int.h"

/* Present at most 60 frames per second (RT_FPS=N, 0 = uncapped), as on the 60 Hz displays the game
   was made for. Vsync alone runs it at the host's refresh rate (120 Hz on ProMotion Macs), and the
   game's per-frame steering loses turns at a quarter of the time step it was tuned for: sprinting
   players could not change direction. */
static double g_wait_ticks;   /* time spent sleeping in frame_pace (not game work) */

static void frame_pace(void) {
    static double period = -1, next;
    if (period < 0) {
        const char *e = getenv("RT_FPS");
        int fps = e ? atoi(e) : 60;
        period = fps > 0 ? (double)SDL_GetPerformanceFrequency() / fps : 0;
        next = 0;
    }
    if (period <= 0) return;
    double now = (double)SDL_GetPerformanceCounter();
    if (next == 0 || now - next > 4 * period) next = now;   /* first frame, or fell far behind: resync */
    next += period;
    double wait = next - now;
    if (wait > 0) {
        uint32_t ms = (uint32_t)(wait * 1000.0 / (double)SDL_GetPerformanceFrequency());
        if (ms > 1) SDL_Delay(ms - 1);
        while ((double)SDL_GetPerformanceCounter() < next) {}
        g_wait_ticks += wait;
    }
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
        if (dt > f / 60 * 1.25) over++;
        n++;
    }
    last = now;
    swap_sum += swap_ticks;
    if (!t0) t0 = now;
    if (now - t0 >= f && n) {
        double span = now - t0;
        rt_trace("perf: %d fps, avg %.1f ms, worst %.1f ms, %d frames >20 ms, swap %.1f ms, paced-wait %.1f ms",
                 (int)(n * f / span + 0.5), span * 1000 / f / n, worst * 1000 / f, (int)over, swap_sum * 1000 / f / n,
                 g_wait_ticks * 1000 / f / n);
#ifdef __APPLE__
        /* Three slow seconds in a row: `sample` this process (5 s of stacks) into logs/, at most 3 times a
           run, so a slow scene explains itself without anyone attaching a profiler. */
        static int slow, taken;
        static int thr;
        if (!thr) {
            const char *e = getenv("RT_SLOW_FPS");
            thr = e ? atoi(e) : 24;
        }
        slow = n * f / span < thr ? slow + 1 : 0;
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
    }
}

void plat_gl_swap(void) {
    double s0 = (double)SDL_GetPerformanceCounter();
    if (g_win && g_gl) SDL_GL_SwapWindow(g_win);
    double sw = (double)SDL_GetPerformanceCounter() - s0;
    frame_pace();
    frame_stats(sw);
}
