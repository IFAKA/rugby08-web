// d3d8_interp.cpp -- fps.interp's runtime side: which steps are recorded, and the frames between steps.
// While on, the renderer records each step instead of drawing it (render/webgl/interp.cpp); the step's
// own frame and every refresh after it come from replays, with the party's overlay over each.
#include "d3d8_int.hpp"
#include "d3d8_dev.hpp"

static bool g_on;   // the game's: the next step is recorded
// The drawing's side (the render thread's, when there is one): set by posted calls, in step order.
static bool r_on;
static std::vector<uint8_t> r_ov;   // the overlay over this step's frames (ui_present's image), or none
static int r_ow, r_oh;

struct Ov {
    std::vector<uint8_t> px;
    int w, h;
};
static void set_ov(void *p) {
    Ov *o = (Ov *)p;
    r_ov.swap(o->px), r_ow = o->w, r_oh = o->h;
    delete o;
}
static void set_on(void *p) { r_on = p != nullptr; }

static void own_frame(void *) {
    if (d3d9w_interp_draw(0)) d3d8_debug_interp(0);
}
void d3d8_interp_present(void) {   // the step's own frame: the step before it, blended at t = 0
    if (g_on) d3d9w_rq_post(own_frame, nullptr);
}

void d3d8_overlay_draw(const uint8_t *img, int w, int h, int changed) {
    static const uint8_t *last;
    static int lw, lh;
    bool same = img && !changed && img == last && w == lw && h == lh;
    last = img, lw = w, lh = h;
    if (same) return d3d9w_overlay_draw(img, w, h, 0);   // (the drawing's copy is still this image)
    Ov *o = new Ov{ img ? std::vector<uint8_t>(img, img + (size_t)w * h * 4) : std::vector<uint8_t>(), w, h };
    d3d9w_rq_post(set_ov, o);
    if (img) d3d9w_overlay_draw(img, w, h, changed);
}

// After Present: whether the next step is recorded (not while it is late, catching up, or a check
// reads its pixels). Returns `late` unchanged.
bool d3d8_interp_next(bool late) {
    g_on = !late && !rt_ff && plat_interp_wanted() && !d3d8_debug_reads_frames();
    d3d9w_interp_mode(g_on);
    d3d9w_rq_post(set_on, g_on ? &g_on : nullptr);
    return late;
}

extern "C" int rt_interp_frame(float t) {   // (the drawing's side)
    if (!r_on || !d3d9w_interp_draw(t)) return 0;
    d3d8_debug_interp(t);
    if (!r_ov.empty()) d3d9w_overlay_draw(r_ov.data(), r_ow, r_oh, 0);
    return 1;
}

extern "C" void rt_interp_frame_end(void) { d3d9w_overlay_undo(); }
