// d3d8_textures.cpp -- IDirect3DResource8/BaseTexture8/Texture8/Surface8 and the device methods that
// create or copy textures and surfaces (levels and formats: d3d8_levels.cpp).
#include "d3d8_res.hpp"
#include <algorithm>
#include <cstring>

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

}  // extern "C"
