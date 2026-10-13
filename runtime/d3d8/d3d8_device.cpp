// d3d8_device.cpp -- IDirect3DDevice8: lifetime and info, Reset and Present, render targets, scene,
// draws. States: d3d8_state.cpp; shaders: d3d8_dev_shaders.cpp; shared state: d3d8_dev.hpp.
#include "d3d8_dev.hpp"
extern "C" void rt_coverage_frame(uint32_t frame);
#include "match_state.h"
#include "ui.h"
#include "plat.h"
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

// A guest surface for the device's own backbuffer / depth buffer (host surfaces from d3d9-webgl).
static uint32_t wrap_surface(IDirect3DSurface9 *s9, uint32_t w, uint32_t h, uint32_t fmt, uint32_t usage) {
    Obj *o = new Obj(K_SURFACE);
    o->host = s9;
    o->owns = false;
    o->w = w;
    o->h = h;
    o->fmt = fmt;
    o->usage = usage;
    return d3d8_new(o);
}

void d3d8_device_init(Obj *dev, uint32_t focus_hwnd) {
    DevState *s = dev->dev = new DevState();
    s->focus = focus_hwnd;
    IDirect3DSurface9 *bb = nullptr;
    dev9(dev)->GetRenderTarget(0, &bb);
    s->backbuffer = wrap_surface(bb, dev->w, dev->h, dev->fmt, 1);
    s->rt = s->backbuffer;
    addref(s->rt);
    // D3D defaults the game may read back before setting
    static const struct {
        uint32_t k, v;
    } rs0[] = { { 7, 1 },   { 8, 3 },   { 9, 2 },   { 14, 1 },  { 19, 2 },          { 20, 1 },
                { 22, 3 },  { 23, 4 },  { 25, 8 },  { 34, 0 },  { 60, 0xFFFFFFFF }, { 136, 1 },
                { 137, 1 }, { 141, 1 }, { 143, 0 }, { 145, 1 }, { 146, 2 },         { 168, 0xF } };
    for (auto &r : rs0) s->rs[r.k] = r.v;
    for (int i = 0; i < 8; i++) {
        uint32_t *t = s->tss[i];
        t[1] = i ? 1 : 4;
        t[2] = 2;
        t[3] = 1;
        t[4] = i ? 1 : 2;
        t[5] = 2;
        t[6] = 1;
        t[11] = (uint32_t)i;
        t[13] = t[14] = t[25] = 1;
        t[16] = t[17] = 1;
        t[18] = 0;
        t[21] = 1;
    }
    D3DMATRIX id = {};
    id._11 = id._22 = id._33 = id._44 = 1.f;
    for (uint32_t k : { 2u, 3u, 256u }) s->xf[k] = id;
    s->vp = { 0, 0, dev->w, dev->h, 0.f, 1.f };
    for (int i = 0; i < 256; i++) s->gamma[i] = s->gamma[256 + i] = s->gamma[512 + i] = (uint32_t)(i * 257);
}

extern "C" {

// ---- device lifetime / info -----------------------------------------------------------------

HLE_D3D(IDirect3DDevice8, TestCooperativeLevel) {
    (void)a;
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetAvailableTextureMem) {
    (void)a;
    return 512u << 20;
}
HLE_D3D(IDirect3DDevice8, ResourceManagerDiscardBytes) {
    (void)a;
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetDirect3D) {
    uint32_t g = d3d8_obj(a[0])->d3d;
    addref(g);
    wr32(a[1], g);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetDeviceCaps) {
    d3d8_fill_caps(a[1]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetDisplayMode) {
    Obj *o = d3d8_obj(a[0]);
    wr32(a[1], o->w);
    wr32(a[1] + 4, o->h);
    wr32(a[1] + 8, 60);
    wr32(a[1] + 12, o->fmt ? o->fmt : D3DFMT_X8R8G8B8);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetCreationParameters) {   // {Adapter, DeviceType, hFocusWindow, BehaviorFlags}
    Obj *o = d3d8_obj(a[0]);
    wr32(a[1], 0);
    wr32(a[1] + 4, 1);
    wr32(a[1] + 8, o->dev->focus);
    wr32(a[1] + 12, o->behavior);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetCursorProperties) {
    (void)a;
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, SetCursorPosition) {
    (void)a;
    return 0;
}
HLE_D3D(IDirect3DDevice8, ShowCursor) {
    (void)a;
    return 0;
}

HLE_D3D(IDirect3DDevice8, Reset) {
    Obj *o = d3d8_obj(a[0]);
    D3DPRESENT_PARAMETERS pp;
    d3d8_present_params(a[1], &pp);
    rt_trace("IDirect3DDevice8::Reset %ux%u", pp.BackBufferWidth, pp.BackBufferHeight);
    dev9(o)->Reset(&pp);
    o->w = pp.BackBufferWidth;
    o->h = pp.BackBufferHeight;
    DevState *s = o->dev;
    o->dev->vp = { 0, 0, o->w, o->h, 0.f, 1.f };
    // Reset makes the back buffer the render target again, at its new size
    if (Obj *bb = d3d8_obj(s->backbuffer)) {
        bb->w = o->w;
        bb->h = o->h;
    }
    if (s->rt != s->backbuffer) {
        addref(s->backbuffer);
        release(s->rt);
        s->rt = s->backbuffer;
    }
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, Present) {   // (this, src, dst, hwnd, dirty)
    Obj *o = d3d8_obj(a[0]);
    DevState *s = o->dev;
    if (!rt_ff) dev9(o)->Present(nullptr, nullptr, nullptr, nullptr);
    {   // the in-game Online screens (runtime/core/ui_online.c), before screenshots see the frame
        const uint8_t *img;
        int changed;
        if (!rt_ff && ui_present(s->frame, (int)o->w, (int)o->h, &img, &changed))
            d3d9w_overlay_draw(img, (int)o->w, (int)o->h, changed);
    }
    s->frame++;
    plat_replay_tick(s->frame);
    net_ha_present(s->frame);   // host-authoritative online play: friends' pads in, this player's out
    rt_pads_present(s->frame);  // pads: this machine's game controller, the party's; the game's controllers follow
    if (rt_det == 1) det_frame(s->frame);   // input for this frame reaches the guest, virtual time advances
    rt_coverage_frame(s->frame);
    ms_frame(s->frame);
    d3d8_debug_present(s->frame, (int)o->w, (int)o->h);   // runtime/debug/frames.cpp
    if (!rt_ff)
        plat_gl_swap(); // web: also yields the frame to the browser (platform/web); catching up: not shown nor paced
    else if (s->frame % 16 == 0) plat_frame_yield(); // catching up (netplay): let the page breathe (input, network)
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetBackBuffer) { // (this, index, type, ppSurface)
    DevState *s = S(a);
    addref(s->backbuffer);
    wr32(a[3], s->backbuffer);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRasterStatus) {
    wr32(a[1], 0);
    wr32(a[1] + 4, 0);
    return D3D_OK_;
} // {InVBlank, ScanLine}
HLE_D3D(IDirect3DDevice8, SetGammaRamp) { // (this, flags, D3DGAMMARAMP*: 3 x 256 WORD)
    for (int i = 0; i < 768; i++) S(a)->gamma[i] = rd16(a[2] + 2u * (uint32_t)i);
    uint16_t ramp[768];
    for (int i = 0; i < 768; i++) ramp[i] = (uint16_t)S(a)->gamma[i];
    plat_set_gamma(ramp);
    rt_trace("SetGammaRamp flags=%u r[128]=%u r[255]=%u", a[1], S(a)->gamma[128], S(a)->gamma[255]);
    return 0;
}
HLE_D3D(IDirect3DDevice8, GetGammaRamp) {
    for (int i = 0; i < 768; i++) wr16(a[1] + 2u * (uint32_t)i, (uint16_t)S(a)->gamma[i]);
    return 0;
}

// ---- render targets ------------------------------------------------------------------------

HLE_D3D(IDirect3DDevice8, SetRenderTarget) { // (this, pRenderTarget, pNewZStencil)
    DevState *s = S(a);
    IDirect3DDevice9 *d = D(a);
    if (a[1]) {
        Obj *rt = d3d8_obj(a[1]);
        d->SetRenderTarget(0, d3d8_surface9(rt));
        addref(a[1]);
        release(s->rt);
        s->rt = a[1];
        D3DVIEWPORT9 vp = { 0, 0, rt->w, rt->h, 0.f, 1.f }; // D3D resets the viewport to the target
        s->vp = vp;
        d->SetViewport(&vp);
    }
    d->SetDepthStencilSurface(a[2] ? d3d8_surface9(d3d8_obj(a[2])) : nullptr);
    addref(a[2]);
    release(s->zs);
    s->zs = a[2];
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRenderTarget) {
    DevState *s = S(a);
    addref(s->rt);
    wr32(a[1], s->rt);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetDepthStencilSurface) {
    DevState *s = S(a);
    if (!s->zs) {
        wr32(a[1], 0);
        return 0x88760870u;
    } // D3DERR_NOTFOUND
    addref(s->zs);
    wr32(a[1], s->zs);
    return D3D_OK_;
}

// ---- scene ----------------------------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, BeginScene) { return D(a)->BeginScene() == D3D_OK ? D3D_OK_ : D3DERR_INVALIDCALL_; }
HLE_D3D(IDirect3DDevice8, EndScene) { return D(a)->EndScene() == D3D_OK ? D3D_OK_ : D3DERR_INVALIDCALL_; }

HLE_D3D(IDirect3DDevice8, Clear) { // (this, Count, D3DRECT*, Flags, Color, float Z, Stencil)
    if (rt_ff) return D3D_OK_;
    std::vector<D3DRECT> r(a[1]);
    for (uint32_t i = 0; i < a[1] && a[2]; i++) {
        uint32_t p = a[2] + 16 * i;
        r[i] = { (LONG)(int32_t)rd32(p), (LONG)(int32_t)rd32(p + 4), (LONG)(int32_t)rd32(p + 8),
                 (LONG)(int32_t)rd32(p + 12) };
    }
    float z;
    memcpy(&z, &a[5], 4);
    D(a)->Clear(a[2] ? a[1] : 0, a[2] && a[1] ? r.data() : nullptr, a[3], a[4], z, a[6]);
    return D3D_OK_;
}

// ---- draws -----------------------------------------------------------------------------------
// This player's camera: d3d8_pov.cpp (d3d8_pov_pre / d3d8_pov_post around each draw).
HLE_D3D(IDirect3DDevice8, DrawPrimitive) {
    bool p = !rt_ff && d3d8_pov_pre(a); // catching up (netplay): nothing is drawn
    if (!rt_ff) D(a)->DrawPrimitive((D3DPRIMITIVETYPE)a[1], a[2], a[3]);
    d3d8_pov_post(a, p);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DrawIndexedPrimitive) { // (this, type, minIndex, numVertices, startIndex, primCount)
    bool p = !rt_ff && d3d8_pov_pre(a); // catching up (netplay): nothing is drawn
    if (!rt_ff) D(a)->DrawIndexedPrimitive((D3DPRIMITIVETYPE)a[1], (INT)S(a)->base_vertex, a[2], a[3], a[4], a[5]);
    d3d8_pov_post(a, p);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DrawPrimitiveUP) { // (this, type, primCount, data, stride)
    DevState *s = S(a);
    bool p = !rt_ff && d3d8_pov_pre(a); // catching up (netplay): nothing is drawn
    if (!rt_ff) D(a)->DrawPrimitiveUP((D3DPRIMITIVETYPE)a[1], a[2], G2H(a[3]), a[4]);
    d3d8_pov_post(a, p);
    release(s->stream[0]);
    s->stream[0] = 0;
    s->stride[0] = 0; // D3D clears stream 0 after *UP
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8,
        DrawIndexedPrimitiveUP) { // (this, type, minIndex, numVerts, primCount, idx, idxFmt, verts, stride)
    DevState *s = S(a);
    bool p = !rt_ff && d3d8_pov_pre(a); // catching up (netplay): nothing is drawn
    if (!rt_ff)
        D(a)->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)a[1], a[2], a[3], a[4], G2H(a[5]), (D3DFORMAT)a[6], G2H(a[7]),
                                     a[8]);
    d3d8_pov_post(a, p);
    release(s->stream[0]);
    s->stream[0] = 0;
    s->stride[0] = 0;
    release(s->ib);
    s->ib = 0;
    return D3D_OK_;
}

} // extern "C"
