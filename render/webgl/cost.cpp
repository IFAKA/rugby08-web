// cost.cpp -- where a frame's time goes in the renderer (stutter hunting): shader programs built,
// texture and buffer uploads, draw submission (inclusive: a draw's first use of a program builds it, and
// Apple's GL finishes compiling at that first draw). The pacers take the totals after each frame and
// name them on a slow one (platform/native/frame.c). Always on: one clock read per call.
#include "internal.h"
#include <chrono>

double g_cost_ms[COST_N];
int g_cost_n[COST_N];

double cost_clock() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

extern "C" void d3d9w_cost_take(double *ms, int *n) {
    for (int k = 0; k < COST_N; k++) ms[k] = g_cost_ms[k], n[k] = g_cost_n[k], g_cost_ms[k] = 0, g_cost_n[k] = 0;
}
