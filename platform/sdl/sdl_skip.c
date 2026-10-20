/* sdl_skip.c -- which frames a device too slow for 60 drawn frames a second leaves undrawn (fps.skip).
 * The game steps 60 times a second everywhere (its steering is tuned to 1/60 s): a slow device draws
 * one step in N, the same N every time, so its frames come at an even 60/N a second (30, 20, 15) and
 * never as a mix of quick and stalled ones. N is the smallest that fits a second: one drawn step and
 * N-1 undrawn ones within N slots, from what each kind cost lately. At most 4: below 15 drawn frames
 * a second the game itself slows (a party waits for it). Both platforms' pacers report each frame. */
#include "sdl_int.h"

#define SKIP_MAX 4

static double g_drawn = -1, g_undrawn = -1;   /* what a drawn and an undrawn step cost lately (ms) */
static int g_every = 1, g_phase;              /* one step in g_every is drawn; steps since the last */

/* The fewest steps per drawn frame that fit: d + (n - 1) u <= n p. */
static int steps_needed(double d, double u, double p) {
    if (d <= p) return 1;
    if (u >= p) return SKIP_MAX;
    int n = (int)((d - u) / (p - u) + 0.999);
    return n < 1 ? 1 : n > SKIP_MAX ? SKIP_MAX : n;
}

/* After each frame: whether it was drawn and what it cost (its work, not the pacer's wait), and the
 * length of a step (ms). Costs are smoothed over about 16 frames; N grows at once when the costs
 * call for it and shrinks only with a 15% margin (no flicker between two rates). */
void skip_frame_done(int drawn, double cost_ms, double period_ms) {
    double *avg = drawn ? &g_drawn : &g_undrawn;
    *avg = *avg < 0 ? cost_ms : *avg + (cost_ms - *avg) / 16;
    double d = g_drawn < 0 ? 0 : g_drawn;
    double u = g_undrawn < 0 ? d / 2 : g_undrawn;   /* (none undrawn yet: about half, the drawing) */
    int up = steps_needed(d, u, period_ms), down = steps_needed(d * 1.15, u * 1.15, period_ms);
    if (up > g_every) g_every = up;
    else if (down < g_every) g_every = down;
    g_phase = drawn ? 0 : g_phase + 1;
}

/* The next frame is undrawn: it is not the one in N that is drawn. */
int skip_next(void) { return g_every > 1 && g_phase + 1 < g_every; }

int skip_active(void) { return g_every > 1; }

void skip_reset(void) { g_every = 1, g_phase = 0, g_drawn = g_undrawn = -1; }
