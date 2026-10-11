// d3d8_resources.cpp -- textures, surfaces, vertex/index buffers, and the device methods that
// create or copy them. The guest writes into shadows in guest memory (Lock returns guest VAs);
// Unlock uploads the shadow to d3d9-webgl, converting formats it does not handle.
#include <GLES3/gl3.h>
#include <vector>
#include "d3d8_int.hpp"
#include <algorithm>
#include <cstring>

static bool compressed(uint32_t f) { return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5; }

static uint32_t bpp(uint32_t f) {
    switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 32;
    case D3DFMT_R8G8B8: return 24;
    case D3DFMT_R5G6B5: case FMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case 30: case 51: return 16;
    case 28: case 50: case 41: return 8;   // A8, L8, P8
    }
    return 32;
}

// Formats d3d9-webgl uploads correctly as-is; the rest are converted to A8R8G8B8.
static uint32_t host_format(uint32_t f) {
    switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
    case D3DFMT_DXT1: case D3DFMT_DXT3: case D3DFMT_DXT5:
        return f;
    case D3DFMT_DXT2: return D3DFMT_DXT3;          // premultiplied variants: same block layout
    case D3DFMT_DXT4: return D3DFMT_DXT5;
    }
    return D3DFMT_A8R8G8B8;
}

static void level_geom(uint32_t fmt, uint32_t w, uint32_t h, uint32_t l, uint32_t *pitch, uint32_t *rows, uint32_t *size) {
    uint32_t mw = std::max(1u, w >> l), mh = std::max(1u, h >> l);
    if (compressed(fmt)) {
        uint32_t bs = fmt == D3DFMT_DXT1 ? 8 : 16;
        *pitch = (mw + 3) / 4 * bs; *rows = (mh + 3) / 4;
    } else {
        *pitch = (mw * bpp(fmt) + 7) / 8; *rows = mh;
    }
    *size = *pitch * *rows;
}

static uint32_t argb(uint32_t f, const uint8_t *p) {
    uint32_t v = p[0] | (bpp(f) >= 16 ? (uint32_t)p[1] << 8 : 0);
    switch (f) {
    case FMT_X1R5G5B5: {
        uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        return 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
    }
    case 30: {                                     // X4R4G4B4
        uint32_t r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        return 0xFF000000u | r * 17 << 16 | g * 17 << 8 | b * 17;
    }
    case 50: return 0xFF000000u | v * 0x010101u;                         // L8
    case 28: return v << 24 | 0xFFFFFFu;                                 // A8
    case 51: return (v >> 8) << 24 | (v & 0xFF) * 0x010101u;             // A8L8
    case D3DFMT_R8G8B8: return 0xFF000000u | p[2] << 16 | p[1] << 8 | p[0];
    }
    return 0xFFFF00FFu;                            // unknown: magenta, so it shows
}

// ---- textures ------------------------------------------------------------------------------
IDirect3DTexture9 *d3d8_texture9(Obj *t) { return static_cast<IDirect3DTexture9 *>(t->host); }

static void upload_level(Obj *t, uint32_t l) {
    if (l >= t->levels || !t->lvl_shadow[l] || !t->host) return;
    D3DLOCKED_RECT lr = {};
    if (d3d8_texture9(t)->LockRect(l, &lr, nullptr, 0) != D3D_OK) return;
    const uint8_t *src = (const uint8_t *)G2H(t->lvl_shadow[l]);
    uint32_t pitch, rows, size;
    level_geom(t->fmt, t->w, t->h, l, &pitch, &rows, &size);
    if (t->host_fmt == t->fmt || compressed(t->fmt)) {
        memcpy(lr.pBits, src, size);
    } else {
        uint32_t mw = std::max(1u, t->w >> l), b = bpp(t->fmt) / 8;
        uint32_t *dst = (uint32_t *)lr.pBits;
        for (uint32_t y = 0; y < rows; y++)
            for (uint32_t x = 0; x < mw; x++) dst[y * mw + x] = argb(t->fmt, src + y * pitch + x * b);
    }
    d3d8_texture9(t)->UnlockRect(l);
}

static uint32_t new_texture(Obj *dev, uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool, uint32_t *hr) {
    if (!levels) { levels = 1; for (uint32_t m = std::max(w, h); m > 1; m >>= 1) levels++; }
    if (compressed(fmt) && (w < 4 || h < 4)) { *hr = D3DERR_INVALIDCALL_; return 0; }
    uint32_t hf = host_format(fmt);
    IDirect3DTexture9 *t9 = nullptr;
    if (dev9(dev)->CreateTexture(w, h, levels, usage, (D3DFORMAT)hf, (D3DPOOL)pool, &t9, nullptr) != D3D_OK || !t9) {
        *hr = D3DERR_OUTOFVIDEOMEMORY_; return 0;
    }
    Obj *o = new Obj(K_TEXTURE);
    o->host = t9; o->owner = dev->guest; o->w = w; o->h = h; o->levels = levels; o->usage = usage;
    o->fmt = fmt; o->host_fmt = hf; o->pool = pool;
    o->lvl_shadow.assign(levels, 0); o->lvl_pitch.assign(levels, 0); o->lvl_size.assign(levels, 0); o->lvl_surf.assign(levels, 0);
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t rows;
        level_geom(fmt, w, h, l, &o->lvl_pitch[l], &rows, &o->lvl_size[l]);
    }
    *hr = D3D_OK_;
    return d3d8_new(o);
}

// Lock a texture level: the guest gets its shadow (allocated on first use).
// A render-target texture's pixels live on the GPU: copy them into the guest shadow so a lock reads
// what was drawn (D3D returns the rendered surface). The game renders each stadium's shadow map
// into a 256x256 target and reads it back; an empty read made every lit object fully shadowed.
// Rows need no flip: d3d9-webgl draws into targets Y-flipped, so GL row 0 is the D3D top row.
static void readback_level(Obj *t, uint32_t l) {
    if (!(t->usage & 1u) || !t->host || compressed(t->fmt)) return;      // D3DUSAGE_RENDERTARGET
    GLuint tex = d3d9w_texture_gl(d3d8_texture9(t));
    if (!tex) return;
    uint32_t w = std::max(1u, t->w >> l), h = std::max(1u, t->h >> l), b = bpp(t->fmt) / 8;
    GLint prev = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    GLuint fbo = 0; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, (GLint)l);
    std::vector<uint8_t> px((size_t)w * h * 4);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev); glDeleteFramebuffers(1, &fbo);
    if (!ok) return;
    uint8_t *dst = (uint8_t *)G2H(t->lvl_shadow[l]);
    uint32_t pitch = t->lvl_pitch[l];
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *p = &px[((size_t)y * w + x) * 4];
            uint8_t *d = dst + y * pitch + x * b;
            if (b == 4) { d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3]; }          // A8R8G8B8 / X8R8G8B8
            else if (b == 2) { uint16_t v = t->fmt == 23 ? (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3)        // R5G6B5
                                                         : (uint16_t)((p[3] >> 7) << 15 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3);
                               memcpy(d, &v, 2); }
        }
}

static uint32_t lock_level(Obj *t, uint32_t l, uint32_t lr, uint32_t rect) {
    if (l >= t->levels) return D3DERR_INVALIDCALL_;
    if (!t->lvl_shadow[l]) t->lvl_shadow[l] = gcalloc(t->lvl_size[l]);
    readback_level(t, l);
    uint32_t off = 0, pitch = t->lvl_pitch[l];
    if (rect) {
        uint32_t x = rd32(rect), y = rd32(rect + 4);
        off = compressed(t->fmt) ? y / 4 * pitch + x / 4 * (t->fmt == D3DFMT_DXT1 ? 8 : 16) : y * pitch + x * bpp(t->fmt) / 8;
    }
    wr32(lr, pitch); wr32(lr + 4, t->lvl_shadow[l] + off);
    return D3D_OK_;
}

static void level_desc(Obj *t, uint32_t l, uint32_t p) {   // D3DSURFACE_DESC8: Format Type Usage Pool Size MS W H
    wr32(p, t->fmt); wr32(p + 4, 1); wr32(p + 8, t->usage); wr32(p + 12, t->pool);
    wr32(p + 16, l < t->lvl_size.size() ? t->lvl_size[l] : 0); wr32(p + 20, 0);
    wr32(p + 24, std::max(1u, t->w >> l)); wr32(p + 28, std::max(1u, t->h >> l));
}

static uint32_t level_surface(Obj *t, uint32_t l) {
    if (l >= t->levels) return 0;
    if (!t->lvl_surf[l]) {
        Obj *s = new Obj(K_SURFACE);
        s->parent = t; s->level = l; s->owner = t->owner; s->fmt = t->fmt; s->usage = t->usage; s->pool = t->pool;
        s->w = std::max(1u, t->w >> l); s->h = std::max(1u, t->h >> l);
        t->refs++;                                 // the surface holds its texture
        t->lvl_surf[l] = d3d8_new(s);
    } else {
        d3d8_obj(t->lvl_surf[l])->refs++;
    }
    return t->lvl_surf[l];
}

IDirect3DSurface9 *d3d8_surface9(Obj *s) {
    if (!s) return nullptr;
    if (!s->host && s->parent && s->parent->host) {
        IDirect3DSurface9 *s9 = nullptr;
        d3d8_texture9(s->parent)->GetSurfaceLevel(s->level, &s9);
        s->host = s9; s->owns = false;
    }
    return static_cast<IDirect3DSurface9 *>(s->host);
}

extern "C" {

HLE_D3D(IDirect3DDevice8, CreateTexture) {   // (this, W, H, Levels, Usage, Format, Pool, ppTexture)
    uint32_t hr;
    uint32_t g = new_texture(d3d8_obj(a[0]), a[1], a[2], a[3], a[4], a[5], a[6], &hr);
    wr32(a[7], g);
    if (!g) rt_log("CreateTexture %ux%u fmt 0x%X usage 0x%X failed 0x%08X", a[1], a[2], a[5], a[4], hr);
    return hr;
}

HLE_D3D(IDirect3DResource8, GetDevice) { Obj *o = d3d8_obj(a[0]); Obj *d = d3d8_obj(o->owner); if (d) d->refs++; wr32(a[1], o->owner); return D3D_OK_; }
HLE_D3D(IDirect3DResource8, SetPrivateData) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DResource8, GetPrivateData) { (void)a; return 0x88760870u; }   // D3DERR_NOTFOUND
HLE_D3D(IDirect3DResource8, FreePrivateData) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DResource8, SetPriority) { (void)a; return 0; }
HLE_D3D(IDirect3DResource8, GetPriority) { (void)a; return 0; }
HLE_D3D(IDirect3DResource8, PreLoad) { (void)a; return 0; }
HLE_D3D(IDirect3DResource8, GetType) {
    switch (d3d8_obj(a[0])->kind) {
    case K_TEXTURE: return 3; case K_VOLTEX: return 4; case K_CUBE: return 5; case K_VB: return 6; case K_IB: return 7;
    default: return 1;
    }
}
HLE_D3D(IDirect3DBaseTexture8, SetLOD) { (void)a; return 0; }
HLE_D3D(IDirect3DBaseTexture8, GetLOD) { (void)a; return 0; }
HLE_D3D(IDirect3DBaseTexture8, GetLevelCount) { return d3d8_obj(a[0])->levels; }

HLE_D3D(IDirect3DTexture8, GetLevelDesc) { Obj *t = d3d8_obj(a[0]); if (a[1] >= t->levels) return D3DERR_INVALIDCALL_; level_desc(t, a[1], a[2]); return D3D_OK_; }
HLE_D3D(IDirect3DTexture8, GetSurfaceLevel) { uint32_t s = level_surface(d3d8_obj(a[0]), a[1]); wr32(a[2], s); return s ? D3D_OK_ : D3DERR_INVALIDCALL_; }
HLE_D3D(IDirect3DTexture8, LockRect) { return lock_level(d3d8_obj(a[0]), a[1], a[2], a[3]); }
HLE_D3D(IDirect3DTexture8, UnlockRect) { upload_level(d3d8_obj(a[0]), a[1]); return D3D_OK_; }
HLE_D3D(IDirect3DTexture8, AddDirtyRect) { (void)a; return D3D_OK_; }

// ---- surfaces --------------------------------------------------------------------------------
HLE_D3D(IDirect3DSurface8, GetDevice) { Obj *o = d3d8_obj(a[0]); Obj *d = d3d8_obj(o->owner); if (d) d->refs++; wr32(a[1], o->owner); return D3D_OK_; }
HLE_D3D(IDirect3DSurface8, SetPrivateData) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DSurface8, GetPrivateData) { (void)a; return 0x88760870u; }
HLE_D3D(IDirect3DSurface8, FreePrivateData) { (void)a; return D3D_OK_; }

HLE_D3D(IDirect3DSurface8, GetContainer) {   // (this, riid, ppContainer)
    Obj *s = d3d8_obj(a[0]);
    if (!s->parent) { wr32(a[2], 0); return E_NOINTERFACE_; }
    s->parent->refs++;
    wr32(a[2], s->parent->guest);
    return D3D_OK_;
}

HLE_D3D(IDirect3DSurface8, GetDesc) {
    Obj *s = d3d8_obj(a[0]);
    if (s->parent) { level_desc(s->parent, s->level, a[1]); return D3D_OK_; }
    uint32_t pitch, rows, size;
    level_geom(s->fmt, s->w, s->h, 0, &pitch, &rows, &size);
    wr32(a[1], s->fmt); wr32(a[1] + 4, 1); wr32(a[1] + 8, s->usage); wr32(a[1] + 12, s->pool);
    wr32(a[1] + 16, size); wr32(a[1] + 20, 0); wr32(a[1] + 24, s->w); wr32(a[1] + 28, s->h);
    return D3D_OK_;
}

HLE_D3D(IDirect3DSurface8, LockRect) {   // (this, D3DLOCKED_RECT*, rect, flags)
    Obj *s = d3d8_obj(a[0]);
    if (s->parent) return lock_level(s->parent, s->level, a[1], a[2]);
    uint32_t pitch, rows, size;                  // standalone: an image surface, or a target we cannot read back
    level_geom(s->fmt, s->w, s->h, 0, &pitch, &rows, &size);
    if (!s->shadow) s->shadow = gcalloc(size);
    if (s->host) d3d8_unimpl("IDirect3DSurface8::LockRect(render target / depth)", 0);
    uint32_t off = 0;
    if (a[2]) off = compressed(s->fmt) ? rd32(a[2] + 4) / 4 * pitch + rd32(a[2]) / 4 * (s->fmt == D3DFMT_DXT1 ? 8 : 16)
                                       : rd32(a[2] + 4) * pitch + rd32(a[2]) * bpp(s->fmt) / 8;
    wr32(a[1], pitch); wr32(a[1] + 4, s->shadow + off);
    return D3D_OK_;
}

HLE_D3D(IDirect3DSurface8, UnlockRect) { Obj *s = d3d8_obj(a[0]); if (s->parent) upload_level(s->parent, s->level); return D3D_OK_; }

// Guest bytes behind a surface (texture level or image surface), allocating the shadow if needed.
static uint32_t surface_bits(Obj *s, uint32_t *pitch) {
    uint32_t rows, size;
    if (s->parent) {
        Obj *t = s->parent;
        if (!t->lvl_shadow[s->level]) t->lvl_shadow[s->level] = gcalloc(t->lvl_size[s->level]);
        *pitch = t->lvl_pitch[s->level];
        return t->lvl_shadow[s->level];
    }
    level_geom(s->fmt, s->w, s->h, 0, pitch, &rows, &size);
    if (!s->shadow) s->shadow = gcalloc(size);
    return s->shadow;
}

extern "C" HLE_D3D(IDirect3DDevice8, CopyRects) {   // (this, src, srcRects, count, dst, dstPoints)
    Obj *src = d3d8_obj(a[1]), *dst = d3d8_obj(a[4]);
    if (!src || !dst || src->fmt != dst->fmt) return D3DERR_INVALIDCALL_;
    if ((src->host && !src->parent) || (dst->host && !dst->parent)) { d3d8_unimpl("CopyRects(render target)", 0); return D3D_OK_; }
    uint32_t sp, dp, sb = surface_bits(src, &sp), db = surface_bits(dst, &dp);
    if (src->parent) readback_level(src->parent, src->level);   // a render-target texture: copy what was drawn
    bool c = compressed(src->fmt);
    uint32_t unit = c ? (src->fmt == D3DFMT_DXT1 ? 8 : 16) : bpp(src->fmt) / 8, div = c ? 4 : 1;
    uint32_t n = a[2] ? a[3] : 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = 0, t = 0, r = src->w, b = src->h, x, y;
        if (a[2]) { uint32_t p = a[2] + 16 * i; l = rd32(p); t = rd32(p + 4); r = rd32(p + 8); b = rd32(p + 12); }
        if (a[5]) { x = rd32(a[5] + 8 * i); y = rd32(a[5] + 8 * i + 4); } else { x = l; y = t; }
        uint32_t bytes = (r - l + div - 1) / div * unit;
        for (uint32_t row = t / div; row < (b + div - 1) / div; row++)
            memcpy(G2H(db + (y / div + row - t / div) * dp + x / div * unit), G2H(sb + row * sp + l / div * unit), bytes);
    }
    if (dst->parent) upload_level(dst->parent, dst->level);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, UpdateTexture) {   // (this, srcTex, dstTex): copy every level's shadow, upload
    Obj *s = d3d8_obj(a[1]), *d = d3d8_obj(a[2]);
    if (!s || !d || s->kind != K_TEXTURE || d->kind != K_TEXTURE || s->fmt != d->fmt) return D3DERR_INVALIDCALL_;
    for (uint32_t l = 0; l < std::min(s->levels, d->levels); l++) {
        if (!s->lvl_shadow[l]) continue;
        if (!d->lvl_shadow[l]) d->lvl_shadow[l] = gcalloc(d->lvl_size[l]);
        memcpy(G2H(d->lvl_shadow[l]), G2H(s->lvl_shadow[l]), std::min(s->lvl_size[l], d->lvl_size[l]));
        upload_level(d, l);
    }
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, CreateImageSurface) {   // (this, W, H, Format, ppSurface): system memory only
    Obj *s = new Obj(K_SURFACE);
    s->owner = a[0]; s->w = a[1]; s->h = a[2]; s->fmt = a[3]; s->pool = 2;   // D3DPOOL_SYSTEMMEM
    wr32(a[4], d3d8_new(s));
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, CreateRenderTarget) {   // (this, W, H, Format, MultiSample, Lockable, ppSurface)
    uint32_t hr;
    uint32_t tex = new_texture(d3d8_obj(a[0]), a[1], a[2], 1, 1, a[3], 0, &hr);
    if (!tex) { wr32(a[6], 0); return hr; }
    Obj *t = d3d8_obj(tex);
    uint32_t s = level_surface(t, 0);
    uint32_t x = tex; d3d8_IUnknown_Release(&x);   // the surface owns the texture now
    wr32(a[6], s);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, CreateDepthStencilSurface) {   // (this, W, H, Format, MultiSample, ppSurface)
    IDirect3DSurface9 *s9 = nullptr;
    if (dev9(d3d8_obj(a[0]))->CreateDepthStencilSurface(a[1], a[2], (D3DFORMAT)a[3], (D3DMULTISAMPLE_TYPE)0, 0, FALSE, &s9, nullptr) != D3D_OK) {
        wr32(a[5], 0); return D3DERR_OUTOFVIDEOMEMORY_;
    }
    Obj *s = new Obj(K_SURFACE);
    s->host = s9; s->owner = a[0]; s->w = a[1]; s->h = a[2]; s->fmt = a[3]; s->usage = 2;
    wr32(a[5], d3d8_new(s));
    return D3D_OK_;
}

// ---- vertex / index buffers -------------------------------------------------------------------
static uint32_t new_buffer(Kind k, Obj *dev, uint32_t len, uint32_t usage, uint32_t fvf_or_fmt, uint32_t pool, uint32_t *out) {
    Obj *o = new Obj(k);
    if (k == K_VB) {
        IDirect3DVertexBuffer9 *vb = nullptr;
        if (dev9(dev)->CreateVertexBuffer(len, usage, fvf_or_fmt, (D3DPOOL)pool, &vb, nullptr) != D3D_OK) { delete o; return D3DERR_OUTOFVIDEOMEMORY_; }
        o->host = vb; o->fvf = fvf_or_fmt; o->fmt = 100;   // D3DFMT_VERTEXDATA
    } else {
        IDirect3DIndexBuffer9 *ib = nullptr;
        if (dev9(dev)->CreateIndexBuffer(len, usage, (D3DFORMAT)fvf_or_fmt, (D3DPOOL)pool, &ib, nullptr) != D3D_OK) { delete o; return D3DERR_OUTOFVIDEOMEMORY_; }
        o->host = ib; o->fmt = fvf_or_fmt;
    }
    o->owner = dev->guest; o->size = len; o->usage = usage; o->pool = pool;
    o->shadow = gcalloc(len ? len : 1);
    *out = d3d8_new(o);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, CreateVertexBuffer) { uint32_t g = 0, hr = new_buffer(K_VB, d3d8_obj(a[0]), a[1], a[2], a[3], a[4], &g); wr32(a[5], g); return hr; }
HLE_D3D(IDirect3DDevice8, CreateIndexBuffer) { uint32_t g = 0, hr = new_buffer(K_IB, d3d8_obj(a[0]), a[1], a[2], a[3], a[4], &g); wr32(a[5], g); return hr; }

static uint32_t buf_lock(const uint32_t *a) {   // (this, offset, size, BYTE**, flags)
    Obj *b = d3d8_obj(a[0]);
    uint32_t off = a[1], sz = a[2] ? a[2] : b->size - std::min(a[1], b->size);
    if (off > b->size || off + sz > b->size) return D3DERR_INVALIDCALL_;
    if (!b->locks++) { b->lock_lo = off; b->lock_hi = off + sz; b->lock_flags = 0; }
    else { b->lock_lo = std::min(b->lock_lo, off); b->lock_hi = std::max(b->lock_hi, off + sz); }
    b->lock_flags |= a[4];
    wr32(a[3], b->shadow + off);
    return D3D_OK_;
}

static uint32_t buf_unlock(const uint32_t *a) {
    Obj *b = d3d8_obj(a[0]);
    if (!b->locks) return D3DERR_INVALIDCALL_;
    if (--b->locks) return D3D_OK_;
    uint32_t n = b->lock_hi - b->lock_lo;
    if (!n) return D3D_OK_;
    // Small buffers are cheaper to re-send whole as a fresh allocation than to patch in place: the
    // GL driver stalls on a SubData into storage the GPU still reads. NOOVERWRITE keeps its range.
    uint32_t fl = b->lock_flags & 0x3000;   // D3DLOCK_NOOVERWRITE | D3DLOCK_DISCARD
    if (!(fl & 0x1000) && b->size <= (256u << 10)) { b->lock_lo = 0; b->lock_hi = b->size; n = b->size; fl = 0x2000; }
    void *p = nullptr;
    HRESULT hr = b->kind == K_VB ? ((IDirect3DVertexBuffer9 *)b->host)->Lock(b->lock_lo, n, &p, fl)
                                 : ((IDirect3DIndexBuffer9 *)b->host)->Lock(b->lock_lo, n, &p, fl);
    if (hr != D3D_OK || !p) return D3DERR_INVALIDCALL_;
    memcpy(p, G2H(b->shadow + b->lock_lo), n);
    if (b->kind == K_VB) ((IDirect3DVertexBuffer9 *)b->host)->Unlock(); else ((IDirect3DIndexBuffer9 *)b->host)->Unlock();
    return D3D_OK_;
}

HLE_D3D(IDirect3DVertexBuffer8, Lock) { return buf_lock(a); }
HLE_D3D(IDirect3DVertexBuffer8, Unlock) { return buf_unlock(a); }
HLE_D3D(IDirect3DIndexBuffer8, Lock) { return buf_lock(a); }
HLE_D3D(IDirect3DIndexBuffer8, Unlock) { return buf_unlock(a); }

HLE_D3D(IDirect3DVertexBuffer8, GetDesc) {   // Format Type Usage Pool Size FVF
    Obj *b = d3d8_obj(a[0]);
    wr32(a[1], 100); wr32(a[1] + 4, 6); wr32(a[1] + 8, b->usage); wr32(a[1] + 12, b->pool); wr32(a[1] + 16, b->size); wr32(a[1] + 20, b->fvf);
    return D3D_OK_;
}
HLE_D3D(IDirect3DIndexBuffer8, GetDesc) {    // Format Type Usage Pool Size
    Obj *b = d3d8_obj(a[0]);
    wr32(a[1], b->fmt); wr32(a[1] + 4, 7); wr32(a[1] + 8, b->usage); wr32(a[1] + 12, b->pool); wr32(a[1] + 16, b->size);
    return D3D_OK_;
}

}  // extern "C"
