// d3d8_interp.cpp -- fps.interp's runtime side: which steps are recorded, and the frames between steps.
// While on, the renderer records each step instead of drawing it (render/webgl/interp.cpp); the step's
// own frame and every refresh after it come from replays, with the party's overlay over each.
#include "d3d8_int.hpp"
#include "d3d8_dev.hpp"

static bool g_on;
static const uint8_t *g_ov;   // the overlay over this step's frames (ui_present's image), or none
static int g_ow, g_oh;

void d3d8_interp_present(void) {   // the step's own frame: the step before it, blended at t = 0
    if (g_on && d3d9w_interp_draw(0)) d3d8_debug_interp(0);
}

void d3d8_overlay_draw(const uint8_t *img, int w, int h, int changed) {
    g_ov = img, g_ow = w, g_oh = h;
    if (img) d3d9w_overlay_draw(img, w, h, changed);
}

// After Present: whether the next step is recorded (not while it is late, catching up, or a check
// reads its pixels). Returns `late` unchanged.
bool d3d8_interp_next(bool late) {
    g_on = !late && !rt_ff && plat_interp_wanted() && !d3d8_debug_reads_frames();
    d3d9w_interp_mode(g_on);
    return late;
}

extern "C" int rt_interp_frame(float t) {
    if (!g_on || !d3d9w_interp_draw(t)) return 0;
    d3d8_debug_interp(t);
    if (g_ov) d3d9w_overlay_draw(g_ov, g_ow, g_oh, 0);
    return 1;
}

extern "C" void rt_interp_frame_end(void) { d3d9w_overlay_undo(); }
