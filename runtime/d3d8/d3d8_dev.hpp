// d3d8_dev.hpp -- the IDirect3DDevice8 handlers' shared state (d3d8_device.cpp, d3d8_state.cpp,
// d3d8_dev_shaders.cpp). The renderer has few Get* methods, so the device shadows what the game sets
// and answers Get* from the shadow.
#pragma once
#include "d3d8_int.hpp"
#include "plat.h"
#include <map>

static_assert(sizeof(D3DMATRIX) == 64, "D3DMATRIX");
static_assert(sizeof(D3DLIGHT9) == 104, "D3DLIGHT8 == D3DLIGHT9");
static_assert(sizeof(D3DMATERIAL9) == 68, "D3DMATERIAL8 == D3DMATERIAL9");
static_assert(sizeof(D3DVIEWPORT9) == 24, "D3DVIEWPORT8 == D3DVIEWPORT9");

struct DevState {
    uint32_t rs[256] = {};
    uint32_t tss[8][32] = {};
    std::map<uint32_t, D3DMATRIX> xf;
    D3DVIEWPORT9 vp = {};
    D3DMATERIAL9 mat = {};
    std::map<uint32_t, D3DLIGHT9> lights;
    std::map<uint32_t, uint32_t> light_on;
    float clip[32][4] = {};
    uint32_t tex[8] = {}, stream[16] = {}, stride[16] = {}, ib = 0, base_vertex = 0;
    uint32_t vshader = 0, rt = 0, zs = 0, backbuffer = 0, focus = 0;
    uint32_t gamma[3 * 256] = {};
    float vsc[100][4] = {}; // vertex shader constants (D3D8: 96 registers; 96..99: local camera, d3d8_pov.cpp)
    bool progvs = false; // a programmable vertex shader is set
    float psc[8][4] = {}; // pixel shader constants (ps.1.x: 8 registers)
    uint32_t pshader = 0; // SetPixelShader handle
    uint32_t frame = 0;
    bool ff_drawn = false; // catching up (netplay): this frame is drawn all the same (ui_catchup shows it)
    bool skip = false;     // this draw is left out of this player's picture (draw_who.c: RT_DRAW_HIDE_VB)
    bool late = false;     // this frame steps the game but is not drawn: the device is behind (fps.skip)
};

bool d3d8_pov_pre(const uint32_t *a); // d3d8_pov.cpp: this player's camera around a draw
void d3d8_pov_post(const uint32_t *a, bool set);
bool d3d8_cursor_pre(
    const uint32_t *a);   // d3d8_pov.cpp: a party's menus' cursor at this screen's pointer (DrawPrimitiveUP)
void d3d8_cursor_post(const uint32_t *a, bool set);
bool d3d8_left_out(const uint32_t *a, int kind, uint32_t prims);   // d3d8_pov.cpp: not in this player's picture

static inline DevState *S(const uint32_t *a) { return d3d8_obj(a[0])->dev; }
static inline IDirect3DDevice9 *D(const uint32_t *a) { return dev9(d3d8_obj(a[0])); }
static inline bool hidden(const uint32_t *a) {   // catching up, late or left out of this player's picture
    return S(a)->skip || S(a)->late || (rt_ff && !S(a)->ff_drawn);
}
static inline void addref(uint32_t g) {
    if (g) {
        Obj *o = d3d8_obj(g);
        if (o) o->refs++;
    }
}
static inline void release(uint32_t g) {
    if (g) {
        uint32_t x = g;
        d3d8_IUnknown_Release(&x);
    }
}

extern int rt_trace_vsc; // runtime/debug/frames.cpp: RT_TRACE_DRAWS_FRAME is on for this frame
void d3d8_debug_present(uint32_t frame, int w, int h);
void d3d8_debug_pixels(uint32_t frame, int w, int h);   // RT_PIX: hash of the game's picture
extern "C" void rt_coverage_frame(uint32_t frame);   // RT_COVERAGE (runtime/debug)
void d3d8_interp_present(void);   // d3d8_interp.cpp (fps.interp): the step's frame, from its recording
void d3d8_overlay_draw(const uint8_t *img, int w, int h, int changed);   // the party's overlay, kept for refreshes
bool d3d8_interp_next(bool late);   // whether the next step is recorded; returns late
bool d3d8_debug_reads_frames(void);
void d3d8_debug_interp(
    float
        t); // RT_TRACE_INTERP: each frame's t and camera (fps_interp_check)   // a test reads the picture (screenshots, RT_PIX): every frame is drawn
void d3d8_debug_light(uint32_t index, const D3DLIGHT9 *l, uint32_t caller, uint32_t light_va);
