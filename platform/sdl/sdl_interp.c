/* sdl_interp.c -- fps.interp: a display faster than the game's 60 steps a second shows a real frame at
 * every refresh between them: the last step's draws, their transforms blended from the step before by
 * the time since its frame was shown (render/webgl/interp.cpp; one step behind, as RT64 does it). Both
 * pacers fill the wait for the next step with these refreshes while one more still fits before the
 * step is due, so the game keeps its 60 steps a second whatever the display. Never together with
 * fps.skip (a device that cannot draw every step has no time for more). Tests: RT_TEST_HZ=N emulates
 * an N Hz display; other automated runs are 60 Hz, as their checks expect. */
#include "sdl_int.h"

static double g_hz = -1;   /* the display's refresh rate */
static double g_cost = 1;  /* what a refresh costs (ms), smoothed */
static int g_frames;       /* refreshes drawn since interp_frames_take */

static double hz(void) {
    static int n;
    if (g_hz < 0 || (++n % 120 == 0 && !getenv("RT_TEST_HZ"))) {   /* (the window may move to another display) */
        const char *e = getenv("RT_TEST_HZ");
        g_hz = e ? atof(e) : automated_run() ? 60 : plat_display_hz();
    }
    return g_hz;
}

int plat_interp_wanted(void) { return hz() > 65 && !skip_active(); }

/* After a step's frame was shown at `shown` (ms): until `next`, the next step's slot, one frame per
 * refresh. `slot` is the step's length (ms); wait() waits for the next refresh at `due` (0: none comes,
 * a hidden page); swap() shows the frame drawn (the web shows it at the next wait). */
void interp_fill(double shown, double next, double slot, double (*clock)(void), int (*wait)(double due),
                 void (*swap)(void)) {
    if (!plat_interp_wanted() || slot <= 0) return;
    double per = 1000.0 / g_hz, due = shown + per;
    while (due + g_cost < next) {
        if (!wait(due)) return;
        double t0 = clock(), t = (t0 - shown) / slot;
        if (!rt_interp_frame((float)(t < 0 ? 0 : t > 0.999 ? 0.999 : t))) return;
        swap();
        rt_interp_frame_end();
        double now = clock();
        g_cost += (now - t0 - g_cost) / 8;
        g_frames++;
        due += per;
        if (due < now) due = now + per;   /* (a refresh missed: the next one) */
    }
}

int interp_frames_take(void) {
    int n = g_frames;
    g_frames = 0;
    return n;
}
