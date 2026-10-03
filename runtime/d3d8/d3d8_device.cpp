// d3d8_device.cpp -- IDirect3DDevice8 on d3d9-webgl. d3d9-webgl has few Get* methods, so the
// device shadows what the game sets and answers Get* from the shadow.
#include "d3d8_int.hpp"
#include "plat.h"
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>
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
    float vsc[96][4] = {};               // vertex shader constants (D3D8: 96 registers)
    uint32_t frame = 0;
};

static DevState *S(const uint32_t *a) { return d3d8_obj(a[0])->dev; }
static IDirect3DDevice9 *D(const uint32_t *a) { return dev9(d3d8_obj(a[0])); }
static void addref(uint32_t g) { if (g) { Obj *o = d3d8_obj(g); if (o) o->refs++; } }
static void release(uint32_t g) { if (g) { uint32_t x = g; d3d8_IUnknown_Release(&x); } }

// A guest surface for the device's own backbuffer / depth buffer (host surfaces from d3d9-webgl).
static uint32_t wrap_surface(IDirect3DSurface9 *s9, uint32_t w, uint32_t h, uint32_t fmt, uint32_t usage) {
    Obj *o = new Obj(K_SURFACE);
    o->host = s9; o->owns = false; o->w = w; o->h = h; o->fmt = fmt; o->usage = usage;
    return d3d8_new(o);
}

void d3d8_device_init(Obj *dev, uint32_t focus_hwnd) {
    DevState *s = dev->dev = new DevState();
    s->focus = focus_hwnd;
    IDirect3DSurface9 *bb = nullptr;
    dev9(dev)->GetRenderTarget(0, &bb);
    s->backbuffer = wrap_surface(bb, dev->w, dev->h, dev->fmt, 1);
    s->rt = s->backbuffer; addref(s->rt);
    // D3D defaults the game may read back before setting
    static const struct { uint32_t k, v; } rs0[] = { { 7, 1 }, { 8, 3 }, { 9, 2 }, { 14, 1 }, { 19, 2 }, { 20, 1 },
        { 22, 3 }, { 23, 4 }, { 25, 8 }, { 34, 0 }, { 60, 0xFFFFFFFF }, { 136, 1 }, { 137, 1 }, { 141, 1 },
        { 143, 0 }, { 145, 1 }, { 146, 2 }, { 168, 0xF } };
    for (auto &r : rs0) s->rs[r.k] = r.v;
    for (int i = 0; i < 8; i++) {
        uint32_t *t = s->tss[i];
        t[1] = i ? 1 : 4; t[2] = 2; t[3] = 1; t[4] = i ? 1 : 2; t[5] = 2; t[6] = 1; t[11] = (uint32_t)i;
        t[13] = t[14] = t[25] = 1; t[16] = t[17] = 1; t[18] = 0; t[21] = 1;
    }
    D3DMATRIX id = {};
    id._11 = id._22 = id._33 = id._44 = 1.f;
    for (uint32_t k : { 2u, 3u, 256u }) s->xf[k] = id;
    s->vp = { 0, 0, dev->w, dev->h, 0.f, 1.f };
    for (int i = 0; i < 256; i++) s->gamma[i] = s->gamma[256 + i] = s->gamma[512 + i] = (uint32_t)(i * 257);
}

extern "C" {

// ---- device lifetime / info -----------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, TestCooperativeLevel) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetAvailableTextureMem) { (void)a; return 512u << 20; }
HLE_D3D(IDirect3DDevice8, ResourceManagerDiscardBytes) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetDirect3D) { uint32_t g = d3d8_obj(a[0])->d3d; addref(g); wr32(a[1], g); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetDeviceCaps) { d3d8_fill_caps(a[1]); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, GetDisplayMode) {
    Obj *o = d3d8_obj(a[0]);
    wr32(a[1], o->w); wr32(a[1] + 4, o->h); wr32(a[1] + 8, 60); wr32(a[1] + 12, o->fmt ? o->fmt : D3DFMT_X8R8G8B8);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetCreationParameters) {   // {Adapter, DeviceType, hFocusWindow, BehaviorFlags}
    Obj *o = d3d8_obj(a[0]);
    wr32(a[1], 0); wr32(a[1] + 4, 1); wr32(a[1] + 8, o->dev->focus); wr32(a[1] + 12, o->behavior);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetCursorProperties) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetCursorPosition) { (void)a; return 0; }
HLE_D3D(IDirect3DDevice8, ShowCursor) { (void)a; return 0; }

HLE_D3D(IDirect3DDevice8, Reset) {
    Obj *o = d3d8_obj(a[0]);
    D3DPRESENT_PARAMETERS pp;
    d3d8_present_params(a[1], &pp);
    rt_trace("IDirect3DDevice8::Reset %ux%u", pp.BackBufferWidth, pp.BackBufferHeight);
    dev9(o)->Reset(&pp);
    o->w = pp.BackBufferWidth; o->h = pp.BackBufferHeight;
    o->dev->vp = { 0, 0, o->w, o->h, 0.f, 1.f };
    return D3D_OK_;
}

// Coarse 32x24 luma grid of the back buffer, to detect that an injected input changed the screen.
static std::vector<float> screen_grid(int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<float> g(32 * 24, 0.f);
    for (int gy = 0; gy < 24; gy++)
        for (int gx = 0; gx < 32; gx++) {
            double sum = 0; int n = 0;
            for (int y = gy * h / 24; y < (gy + 1) * h / 24; y += 4)
                for (int x = gx * w / 32; x < (gx + 1) * w / 32; x += 4) {
                    const uint8_t *p = &px[((size_t)y * w + x) * 4];
                    sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; n++;
                }
            g[(size_t)gy * 32 + gx] = n ? (float)(sum / n) : 0.f;
        }
    return g;
}

static void menu_change_probe(uint32_t frame, int w, int h) {
    static uint32_t tracked; static bool armed; static std::vector<float> base;
    uint32_t le = plat_replay_last_event();
    if (le != tracked) { tracked = le; base = screen_grid(w, h); armed = true; return; }
    if (armed && frame >= le + 90) {
        std::vector<float> cur = screen_grid(w, h);
        double d = 0; for (size_t i = 0; i < cur.size(); i++) d += fabs(cur[i] - base[i]);
        d /= (double)cur.size();
        if (d > 8.0) rt_trace("menu state changed (input at frame %u, screen diff %.1f)", le, d);
        armed = false;
    }
}

HLE_D3D(IDirect3DDevice8, Present) {   // (this, src, dst, hwnd, dirty)
    Obj *o = d3d8_obj(a[0]);
    DevState *s = o->dev;
    dev9(o)->Present(nullptr, nullptr, nullptr, nullptr);
    s->frame++;
    plat_replay_tick(s->frame);
    if (rt_replay_path[0]) menu_change_probe(s->frame, (int)o->w, (int)o->h);
    if (s->frame == 1 || s->frame % 60 == 0 || (int)s->frame == rt_frame_limit) rt_trace("frame %u", s->frame);
    if (rt_frame_limit > 0 && (int)s->frame >= rt_frame_limit) {
        if (rt_screenshot_path[0]) {
            int w = (int)o->w, h = (int)o->h;
            std::vector<uint8_t> px((size_t)w * h * 4), flip(px.size());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < h; y++) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
            int rc = rt_png_write(rt_screenshot_path, w, h, flip.data());
            rt_trace("screenshot %s %dx%d (%s)", rt_screenshot_path, w, h, rc == 0 ? "ok" : "failed");
        }
        rt_trace("exit: frame limit %d reached", rt_frame_limit);
        exit(0);
    }
    plat_gl_swap();
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetBackBuffer) {   // (this, index, type, ppSurface)
    DevState *s = S(a);
    addref(s->backbuffer);
    wr32(a[3], s->backbuffer);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRasterStatus) { wr32(a[1], 0); wr32(a[1] + 4, 0); return D3D_OK_; }   // {InVBlank, ScanLine}
HLE_D3D(IDirect3DDevice8, SetGammaRamp) {    // (this, flags, D3DGAMMARAMP*: 3 x 256 WORD)
    for (int i = 0; i < 768; i++) S(a)->gamma[i] = rd16(a[2] + 2u * (uint32_t)i);
    return 0;
}
HLE_D3D(IDirect3DDevice8, GetGammaRamp) { for (int i = 0; i < 768; i++) wr16(a[1] + 2u * (uint32_t)i, (uint16_t)S(a)->gamma[i]); return 0; }

// ---- render targets ------------------------------------------------------------------------

HLE_D3D(IDirect3DDevice8, SetRenderTarget) {   // (this, pRenderTarget, pNewZStencil)
    DevState *s = S(a);
    IDirect3DDevice9 *d = D(a);
    if (a[1]) {
        Obj *rt = d3d8_obj(a[1]);
        d->SetRenderTarget(0, d3d8_surface9(rt));
        addref(a[1]); release(s->rt); s->rt = a[1];
        D3DVIEWPORT9 vp = { 0, 0, rt->w, rt->h, 0.f, 1.f };     // D3D resets the viewport to the target
        s->vp = vp; d->SetViewport(&vp);
    }
    d->SetDepthStencilSurface(a[2] ? d3d8_surface9(d3d8_obj(a[2])) : nullptr);
    addref(a[2]); release(s->zs); s->zs = a[2];
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRenderTarget) { DevState *s = S(a); addref(s->rt); wr32(a[1], s->rt); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetDepthStencilSurface) {
    DevState *s = S(a);
    if (!s->zs) { wr32(a[1], 0); return 0x88760870u; }   // D3DERR_NOTFOUND
    addref(s->zs); wr32(a[1], s->zs); return D3D_OK_;
}

// ---- scene ----------------------------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, BeginScene) { return D(a)->BeginScene() == D3D_OK ? D3D_OK_ : D3DERR_INVALIDCALL_; }
HLE_D3D(IDirect3DDevice8, EndScene) { return D(a)->EndScene() == D3D_OK ? D3D_OK_ : D3DERR_INVALIDCALL_; }

HLE_D3D(IDirect3DDevice8, Clear) {   // (this, Count, D3DRECT*, Flags, Color, float Z, Stencil)
    std::vector<D3DRECT> r(a[1]);
    for (uint32_t i = 0; i < a[1] && a[2]; i++) {
        uint32_t p = a[2] + 16 * i;
        r[i] = { (LONG)(int32_t)rd32(p), (LONG)(int32_t)rd32(p + 4), (LONG)(int32_t)rd32(p + 8), (LONG)(int32_t)rd32(p + 12) };
    }
    float z; memcpy(&z, &a[5], 4);
    D(a)->Clear(a[2] ? a[1] : 0, a[2] && a[1] ? r.data() : nullptr, a[3], a[4], z, a[6]);
    return D3D_OK_;
}

// ---- transforms, viewport, material, lights, clip planes -------------------------------------
HLE_D3D(IDirect3DDevice8, SetTransform) {
    D3DMATRIX m; memcpy(&m, G2H(a[2]), 64);
    S(a)->xf[a[1]] = m;
    D(a)->SetTransform((D3DTRANSFORMSTATETYPE)a[1], &m);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetTransform) {
    DevState *s = S(a);
    auto it = s->xf.find(a[1]);
    D3DMATRIX m = {};
    if (it != s->xf.end()) m = it->second; else m._11 = m._22 = m._33 = m._44 = 1.f;
    memcpy(G2H(a[2]), &m, 64);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, MultiplyTransform) {   // state = M * state (D3D: pMatrix * current)
    DevState *s = S(a);
    D3DMATRIX m, c = {}, r = {};
    memcpy(&m, G2H(a[2]), 64);
    auto it = s->xf.find(a[1]);
    if (it != s->xf.end()) c = it->second; else c._11 = c._22 = c._33 = c._44 = 1.f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += m.m[i][k] * c.m[k][j];
            r.m[i][j] = v;
        }
    s->xf[a[1]] = r;
    D(a)->SetTransform((D3DTRANSFORMSTATETYPE)a[1], &r);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetViewport) { DevState *s = S(a); memcpy(&s->vp, G2H(a[1]), 24); D(a)->SetViewport(&s->vp); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetViewport) { memcpy(G2H(a[1]), &S(a)->vp, 24); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetMaterial) { DevState *s = S(a); memcpy(&s->mat, G2H(a[1]), 68); D(a)->SetMaterial(&s->mat); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetMaterial) { memcpy(G2H(a[1]), &S(a)->mat, 68); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetLight) {
    D3DLIGHT9 l; memcpy(&l, G2H(a[2]), 104);
    S(a)->lights[a[1]] = l;
    D(a)->SetLight(a[1], &l);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetLight) {
    auto &m = S(a)->lights;
    auto it = m.find(a[1]);
    if (it == m.end()) return D3DERR_INVALIDCALL_;
    memcpy(G2H(a[2]), &it->second, 104);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, LightEnable) { S(a)->light_on[a[1]] = a[2]; D(a)->LightEnable(a[1], (BOOL)a[2]); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetLightEnable) { wr32(a[2], S(a)->light_on[a[1]] ? 128 : 0); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetClipPlane) {
    if (a[1] >= 32) return D3DERR_INVALIDCALL_;
    memcpy(S(a)->clip[a[1]], G2H(a[2]), 16);
    D(a)->SetClipPlane(a[1], S(a)->clip[a[1]]);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetClipPlane) { if (a[1] >= 32) return D3DERR_INVALIDCALL_; memcpy(G2H(a[2]), S(a)->clip[a[1]], 16); return D3D_OK_; }

// ---- render / texture stage states ---------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, SetRenderState) {
    uint32_t st = a[1], v = a[2];
    if (st >= 256) return D3DERR_INVALIDCALL_;
    S(a)->rs[st] = v;
    IDirect3DDevice9 *d = D(a);
    switch (st) {
    case 10: case 30: case 40: case 164:          // LINEPATTERN, ZVISIBLE, EDGEANTIALIAS, PATCHSEGMENTS: no effect here
        return D3D_OK_;
    case 43: case 159:                            // numbers d3d9-webgl uses for other states (see below)
        return D3D_OK_;
    case 143: d->SetRenderState(D3DRS_NORMALIZENORMALS, v); return D3D_OK_;   // d3d9-webgl: 43
    case 154: d->SetRenderState(D3DRS_POINTSIZE, v); return D3D_OK_;          // d3d9-webgl: 159
    case 47: {                                    // ZBIAS 0..16 -> DEPTHBIAS (d3d8to9 CalcDepthBias, 24-bit z)
        float b = -(float)v / 500000.f; uint32_t u; memcpy(&u, &b, 4);
        d->SetRenderState((D3DRENDERSTATETYPE)195, u);
        return D3D_OK_;
    }
    case 153: return D3D_OK_;                     // SOFTWAREVERTEXPROCESSING
    }
    d->SetRenderState((D3DRENDERSTATETYPE)st, v);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRenderState) { if (a[1] >= 256) return D3DERR_INVALIDCALL_; wr32(a[2], S(a)->rs[a[1]]); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetTextureStageState) {   // (this, stage, type, value)
    uint32_t st = a[1], ty = a[2], v = a[3];
    if (st >= 8 || ty >= 32) return D3DERR_INVALIDCALL_;
    S(a)->tss[st][ty] = v;
    IDirect3DDevice9 *d = D(a);
    switch (ty) {                                  // D3D8 sampler states live in the stage states
    case 13: d->SetSamplerState(st, D3DSAMP_ADDRESSU, v); break;
    case 14: d->SetSamplerState(st, D3DSAMP_ADDRESSV, v); break;
    case 25: d->SetSamplerState(st, D3DSAMP_ADDRESSW, v); break;
    case 15: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)4, v); break;                 // BORDERCOLOR
    case 16: d->SetSamplerState(st, D3DSAMP_MAGFILTER, v > 2 ? 2 : v); break;         // cubic -> linear
    case 17: d->SetSamplerState(st, D3DSAMP_MINFILTER, v); break;
    case 18: d->SetSamplerState(st, D3DSAMP_MIPFILTER, v); break;
    case 19: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)8, v); break;                 // MIPMAPLODBIAS
    case 20: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)9, v); break;                 // MAXMIPLEVEL
    case 21: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)10, v); break;                // MAXANISOTROPY
    default: d->SetTextureStageState(st, (D3DTEXTURESTAGESTATETYPE)ty, v);
    }
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetTextureStageState) {
    if (a[1] >= 8 || a[2] >= 32) return D3DERR_INVALIDCALL_;
    wr32(a[3], S(a)->tss[a[1]][a[2]]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, ValidateDevice) { wr32(a[1], 1); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetInfo) { (void)a; return 1; }   // S_FALSE: no device info
HLE_D3D(IDirect3DDevice8, SetPaletteEntries) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetCurrentTexturePalette) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetCurrentTexturePalette) { wr32(a[1], 0); return D3D_OK_; }

// ---- textures, streams, indices, FVF ------------------------------------------------------------

HLE_D3D(IDirect3DDevice8, SetTexture) {      // (this, stage, IDirect3DBaseTexture8*)
    DevState *s = S(a);
    if (a[1] >= 8) return D3DERR_INVALIDCALL_;
    Obj *t = d3d8_obj(a[2]);
    D(a)->SetTexture(a[1], t && t->kind == K_TEXTURE ? d3d8_texture9(t) : nullptr);
    if (t && t->kind != K_TEXTURE) d3d8_unimpl("SetTexture(cube/volume)", 0);
    addref(a[2]); release(s->tex[a[1]]); s->tex[a[1]] = a[2];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetTexture) { uint32_t g = a[1] < 8 ? S(a)->tex[a[1]] : 0; addref(g); wr32(a[2], g); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetStreamSource) {  // (this, stream, VB, stride)
    DevState *s = S(a);
    if (a[1] >= 16) return D3DERR_INVALIDCALL_;
    Obj *vb = d3d8_obj(a[2]);
    D(a)->SetStreamSource(a[1], vb ? (IDirect3DVertexBuffer9 *)vb->host : nullptr, 0, a[3]);
    addref(a[2]); release(s->stream[a[1]]); s->stream[a[1]] = a[2]; s->stride[a[1]] = a[3];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetStreamSource) {
    DevState *s = S(a);
    if (a[1] >= 16) return D3DERR_INVALIDCALL_;
    addref(s->stream[a[1]]); wr32(a[2], s->stream[a[1]]); wr32(a[3], s->stride[a[1]]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetIndices) {       // (this, IB, BaseVertexIndex): D3D9 moves the base to the draw
    DevState *s = S(a);
    Obj *ib = d3d8_obj(a[1]);
    D(a)->SetIndices(ib ? (IDirect3DIndexBuffer9 *)ib->host : nullptr);
    addref(a[1]); release(s->ib); s->ib = a[1]; s->base_vertex = a[2];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetIndices) { DevState *s = S(a); addref(s->ib); wr32(a[1], s->ib); wr32(a[2], s->base_vertex); return D3D_OK_; }

// Vertex shaders. A declaration without function tokens that uses one stream in FVF order
// becomes that FVF; programmable ones are translated by d3d8_vshader.cpp (drawing them is not
// wired yet). Handles carry the high bit (FVF codes never set it here).
static std::vector<Vs8 *> g_vs;

static uint32_t decl_to_fvf(const Vs8 *v, bool *ok) {
    uint32_t fvf = 0, ntex = 0, texsize = 0;
    static const uint8_t comps[] = { 1, 2, 3, 4, 4, 4, 2, 4 };   // D3DVSDT_FLOAT1..SHORT4
    *ok = (v->streams & ~1u) == 0;
    uint32_t next = 0;
    for (const Vs8Elem &e : v->elems) {
        if (e.offset != next) *ok = false;                         // SKIP breaks FVF order
        next = e.offset + (e.type < 8 ? (uint32_t)(e.type <= 3 ? 4 * (e.type + 1) : e.type == 7 ? 8 : 4) : 0);
        switch (e.reg) {
        case 0: fvf |= e.type == 3 ? 0x004 : 0x002; break;         // POSITION: XYZ (XYZRHW if float4)
        case 3: fvf |= 0x010; break;                                // NORMAL
        case 4: fvf |= 0x020; break;                                // PSIZE
        case 5: fvf |= 0x040; break;                                // DIFFUSE
        case 6: fvf |= 0x080; break;                                // SPECULAR
        default:
            if (e.reg >= 7 && e.reg <= 14 && e.reg - 7u == ntex) {   // TEXCOORDn in order
                uint32_t c = e.type < 8 ? comps[e.type] : 2;
                texsize |= (c == 1 ? 3u : c == 3 ? 1u : c == 4 ? 2u : 0u) << (16 + 2 * ntex);
                ntex++;
            } else {
                *ok = false;
            }
        }
    }
    return fvf | ntex << 8 | texsize;
}

HLE_D3D(IDirect3DDevice8, CreateVertexShader) {   // (this, pDeclaration, pFunction, pHandle, Usage)
    Vs8 *v = new Vs8;
    int id = (int)g_vs.size();
    bool decl_ok = a[1] ? vs8_parse_decl(a[1], v) : false;
    v->fvf = decl_to_fvf(v, &v->fvf_ok);
    v->fvf_ok = v->fvf_ok && decl_ok;
    v->programmable = a[2] != 0;
    if (v->programmable) vs8_translate(a[2], v, id);
    else if (!v->fvf_ok) rt_log("CreateVertexShader #%d: declaration has no FVF equivalent (streams 0x%X)", id, v->streams);
    g_vs.push_back(v);
    wr32(a[3], 0x80000000u | (uint32_t)id);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DeleteVertexShader) { (void)a; return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetVertexShaderConstant) {   // (this, Register, pConstantData, ConstantCount)
    DevState *s = S(a);
    for (uint32_t i = 0; i < a[3] && a[1] + i < 96; i++)
        for (int k = 0; k < 4; k++) { uint32_t u = rd32(a[2] + 16 * i + 4 * (uint32_t)k); memcpy(&s->vsc[a[1] + i][k], &u, 4); }
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetVertexShader) {  // FVF codes, or a handle from CreateVertexShader (high bit)
    DevState *s = S(a);
    s->vshader = a[1];
    if (a[1] & 0x80000000u) {
        uint32_t i = a[1] & 0x7FFFFFFFu;
        if (i >= g_vs.size()) return D3DERR_INVALIDCALL_;
        Vs8 *v = g_vs[i];
        for (const Vs8Const &c : v->consts) if (c.reg < 96) memcpy(s->vsc[c.reg], c.v, sizeof c.v);
        if (v->programmable) { d3d8_unimpl("SetVertexShader(programmable)", 0); return D3D_OK_; }
        D(a)->SetFVF(v->fvf);
        return D3D_OK_;
    }
    D(a)->SetFVF(a[1]);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetVertexShader) { wr32(a[1], S(a)->vshader); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetPixelShader) { if (a[1]) d3d8_unimpl("SetPixelShader(nonzero)", 0); return D3D_OK_; }

// ---- draws -----------------------------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, DrawPrimitive) { D(a)->DrawPrimitive((D3DPRIMITIVETYPE)a[1], a[2], a[3]); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, DrawIndexedPrimitive) {   // (this, type, minIndex, numVertices, startIndex, primCount)
    D(a)->DrawIndexedPrimitive((D3DPRIMITIVETYPE)a[1], (INT)S(a)->base_vertex, a[2], a[3], a[4], a[5]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DrawPrimitiveUP) {        // (this, type, primCount, data, stride)
    DevState *s = S(a);
    D(a)->DrawPrimitiveUP((D3DPRIMITIVETYPE)a[1], a[2], G2H(a[3]), a[4]);
    release(s->stream[0]); s->stream[0] = 0; s->stride[0] = 0;   // D3D clears stream 0 after *UP
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DrawIndexedPrimitiveUP) { // (this, type, minIndex, numVerts, primCount, idx, idxFmt, verts, stride)
    DevState *s = S(a);
    D(a)->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)a[1], a[2], a[3], a[4], G2H(a[5]), (D3DFORMAT)a[6], G2H(a[7]), a[8]);
    release(s->stream[0]); s->stream[0] = 0; s->stride[0] = 0;
    release(s->ib); s->ib = 0;
    return D3D_OK_;
}

}  // extern "C"
