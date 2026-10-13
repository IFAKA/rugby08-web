// d3d8_levels.cpp -- texture formats and texture levels: each level is a guest shadow (Lock returns
// guest VAs) that UnlockRect uploads to the renderer, converting the formats it does not handle;
// render-target levels are read back on lock. The COM handlers: d3d8_textures.cpp.
#include "d3d8_res.hpp"
#include "plat.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstring>
#include <vector>

bool compressed(uint32_t f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

uint32_t bpp(uint32_t f) {
    switch (f) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: return 32;
    case D3DFMT_R8G8B8: return 24;
    case D3DFMT_R5G6B5:
    case FMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case 30:
    case 51: return 16;
    case 28:
    case 50:
    case 41: return 8;   // A8, L8, P8
    }
    return 32;
}

// Formats d3d9-webgl uploads correctly as-is; the rest are converted to A8R8G8B8.
static uint32_t host_format(uint32_t f) {
    switch (f) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_DXT1:
    case D3DFMT_DXT3:
    case D3DFMT_DXT5: return f;
    case D3DFMT_DXT2: return D3DFMT_DXT3;          // premultiplied variants: same block layout
    case D3DFMT_DXT4: return D3DFMT_DXT5;
    }
    return D3DFMT_A8R8G8B8;
}

void level_geom(uint32_t fmt, uint32_t w, uint32_t h, uint32_t l, uint32_t *pitch, uint32_t *rows, uint32_t *size) {
    uint32_t mw = std::max(1u, w >> l), mh = std::max(1u, h >> l);
    if (compressed(fmt)) {
        uint32_t bs = fmt == D3DFMT_DXT1 ? 8 : 16;
        *pitch = (mw + 3) / 4 * bs;
        *rows = (mh + 3) / 4;
    } else {
        *pitch = (mw * bpp(fmt) + 7) / 8;
        *rows = mh;
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

void upload_level(Obj *t, uint32_t l) {
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

uint32_t new_texture(Obj *dev, uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool,
                     uint32_t *hr) {
    if (!levels) {
        levels = 1;
        for (uint32_t m = std::max(w, h); m > 1; m >>= 1) levels++;
    }
    if (compressed(fmt) && (w < 4 || h < 4)) {
        *hr = D3DERR_INVALIDCALL_;
        return 0;
    }
    uint32_t hf = host_format(fmt);
    IDirect3DTexture9 *t9 = nullptr;
    if (dev9(dev)->CreateTexture(w, h, levels, usage, (D3DFORMAT)hf, (D3DPOOL)pool, &t9, nullptr) != D3D_OK || !t9) {
        *hr = D3DERR_OUTOFVIDEOMEMORY_;
        return 0;
    }
    Obj *o = new Obj(K_TEXTURE);
    o->host = t9;
    o->owner = dev->guest;
    o->w = w;
    o->h = h;
    o->levels = levels;
    o->usage = usage;
    o->fmt = fmt;
    o->host_fmt = hf;
    o->pool = pool;
    o->lvl_shadow.assign(levels, 0);
    o->lvl_pitch.assign(levels, 0);
    o->lvl_size.assign(levels, 0);
    o->lvl_surf.assign(levels, 0);
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
static void gpu_read(Obj *t, uint32_t l);
// Lockstep (runtime/core/net_external.c): one game reads its GPU, every game gets the same pixels.
void readback_level(Obj *t, uint32_t l) {
    if (!(t->usage & 1u) || !t->host || compressed(t->fmt)) return;      // D3DUSAGE_RENDERTARGET
    uint32_t w = std::max(1u, t->w >> l), h = std::max(1u, t->h >> l), b = bpp(t->fmt) / 8, pitch = t->lvl_pitch[l];
    uint8_t *dst = (uint8_t *)G2H(t->lvl_shadow[l]);
    std::vector<uint8_t> rows((size_t)w * h * b);
    if (rt_net_external_source()) {
        gpu_read(t, l);
        for (uint32_t y = 0; y < h; y++) memcpy(&rows[(size_t)y * w * b], dst + y * pitch, (size_t)w * b);
        rt_net_external_put(rows.data(), (uint32_t)rows.size());
        return;
    }
    rt_net_external_get(rows.data(), (uint32_t)rows.size());
    for (uint32_t y = 0; y < h; y++) memcpy(dst + y * pitch, &rows[(size_t)y * w * b], (size_t)w * b);
}

static void gpu_read(Obj *t, uint32_t l) {
    GLuint tex = d3d9w_texture_gl(d3d8_texture9(t));
    if (!tex) return;
    uint32_t w = std::max(1u, t->w >> l), h = std::max(1u, t->h >> l), b = bpp(t->fmt) / 8;
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, (GLint)l);
    std::vector<uint8_t> px((size_t)w * h * 4);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    glDeleteFramebuffers(1, &fbo);
    if (!ok) return;
    rt_trace("d3d8: render target read back %ux%u level %u fmt %u", w, h, l, t->fmt);
    uint8_t *dst = (uint8_t *)G2H(t->lvl_shadow[l]);
    uint32_t pitch = t->lvl_pitch[l];
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *p = &px[((size_t)y * w + x) * 4];
            uint8_t *d = dst + y * pitch + x * b;
            if (b == 4) {
                d[0] = p[2];
                d[1] = p[1];
                d[2] = p[0];
                d[3] = p[3];
            }          // A8R8G8B8 / X8R8G8B8
            else if (b == 2) {
                uint16_t v = t->fmt == 23
                                 ? (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3)        // R5G6B5
                                 : (uint16_t)((p[3] >> 7) << 15 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3);
                memcpy(d, &v, 2);
            }
        }
}

uint32_t lock_level(Obj *t, uint32_t l, uint32_t lr, uint32_t rect) {
    if (l >= t->levels) return D3DERR_INVALIDCALL_;
    if (!t->lvl_shadow[l]) t->lvl_shadow[l] = gcalloc(t->lvl_size[l]);
    readback_level(t, l);
    uint32_t off = 0, pitch = t->lvl_pitch[l];
    if (rect) {
        uint32_t x = rd32(rect), y = rd32(rect + 4);
        off = compressed(t->fmt) ? y / 4 * pitch + x / 4 * (t->fmt == D3DFMT_DXT1 ? 8 : 16)
                                 : y * pitch + x * bpp(t->fmt) / 8;
    }
    wr32(lr, pitch);
    wr32(lr + 4, t->lvl_shadow[l] + off);
    return D3D_OK_;
}

void level_desc(Obj *t, uint32_t l, uint32_t p) {   // D3DSURFACE_DESC8: Format Type Usage Pool Size MS W H
    wr32(p, t->fmt);
    wr32(p + 4, 1);
    wr32(p + 8, t->usage);
    wr32(p + 12, t->pool);
    wr32(p + 16, l < t->lvl_size.size() ? t->lvl_size[l] : 0);
    wr32(p + 20, 0);
    wr32(p + 24, std::max(1u, t->w >> l));
    wr32(p + 28, std::max(1u, t->h >> l));
}

uint32_t level_surface(Obj *t, uint32_t l) {
    if (l >= t->levels) return 0;
    if (!t->lvl_surf[l]) {
        Obj *s = new Obj(K_SURFACE);
        s->parent = t;
        s->level = l;
        s->owner = t->owner;
        s->fmt = t->fmt;
        s->usage = t->usage;
        s->pool = t->pool;
        s->w = std::max(1u, t->w >> l);
        s->h = std::max(1u, t->h >> l);
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
        s->host = s9;
        s->owns = false;
    }
    return static_cast<IDirect3DSurface9 *>(s->host);
}
