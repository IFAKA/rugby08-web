// texture.cpp -- D3D9Texture: a lock hands out host memory, the unlock uploads it (converted to RGBA8
// unless it is DXT) and builds the mip chain.
#include "internal.h"
#include "resources.h"

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#define D3DFMT_GLI1 ((D3DFORMAT)0x31494C47)   // the GLI library's FOURCC for RGB DXT1

static bool compressed(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT3 || f == D3DFMT_DXT5 || f == D3DFMT_GLI1;
}

static UINT bits_per_pixel(D3DFORMAT f) {
    switch (f) {
    case D3DFMT_R8G8B8: return 24;
    case D3DFMT_R5G6B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_INDEX16: return 16;
    default: return 32;
    }
}

// D3D decodes DXT1 with punch-through alpha: a 3-colour block (c0 <= c1) makes index 3 transparent
// black. That is GL's RGBA variant; the RGB one turns those texels opaque black (black boxes around
// the alpha-tested sprites).
static GLenum gl_compressed_format(D3DFORMAT f) {
    switch (f) {
    case D3DFMT_DXT1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    case D3DFMT_GLI1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case D3DFMT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    case D3DFMT_DXT5: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    default: return 0;
    }
}

static size_t dxt_size(D3DFORMAT f, UINT w, UINT h, size_t *pitch) {
    size_t block = (f == D3DFMT_DXT1 || f == D3DFMT_GLI1) ? 8 : 16;   // bytes per 4x4 block
    if (pitch) *pitch = ((w + 3) / 4) * block;
    return ((w + 3) / 4) * ((h + 3) / 4) * block;
}

static inline uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return r | g << 8 | b << 16 | a << 24; }

// n texels of D3D format f (little-endian ARGB-style packing) -> RGBA8 bytes for GL.
static void to_rgba8(D3DFORMAT f, const void *data, size_t n, uint32_t *out) {
    const uint16_t *s16 = static_cast<const uint16_t *>(data);
    const uint32_t *s32 = static_cast<const uint32_t *>(data);
    switch ((int)f) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:   // X8: opaque; A8 keeps its alpha
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = s32[i];
            out[i] = rgba((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, f == D3DFMT_X8R8G8B8 ? 0xFF : c >> 24);
        }
        return;
    case D3DFMT_A4R4G4B4:
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = s16[i];
            out[i] = rgba(((c >> 8) & 0xF) * 0x11, ((c >> 4) & 0xF) * 0x11, (c & 0xF) * 0x11, ((c >> 12) & 0xF) * 0x11);
        }
        return;
    case D3DFMT_A1R5G5B5:   // (X1R5G5B5 is converted by the D3D8 layer before it gets here)
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = s16[i], r = (c >> 10) & 0x1F, g = (c >> 5) & 0x1F, b = c & 0x1F;
            uint32_t a = f == D3DFMT_A1R5G5B5 ? ((c >> 15) & 1 ? 255 : 0) : 255;
            out[i] = rgba(r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, a);
        }
        return;
    case D3DFMT_R5G6B5:
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = s16[i];
            out[i] = rgba(((c >> 11) & 0x1F) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3, 0xFF);
        }
        return;
    case D3DFMT_R8G8B8: {   // B, G, R bytes
        printf("[WARN] D3DFMT_R8G8B8 texture uploaded - untested path\n");
        const uint8_t *s = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < n; ++i, s += 3) out[i] = rgba(s[2], s[1], s[0], 0xFF);
        return;
    }
    default:   // anything else is passed through as 32-bit texels
        memcpy(out, data, n * 4);
    }
}

// Bound outside a draw: the draw paths' binding caches no longer hold.
static void invalidate_binding_caches() {
    g_last_bound_texture_id = 0xFFFFFFFF;
    g_last_bound_lightmap_id = 0xFFFFFFFF;
}

D3D9Texture::D3D9Texture(UINT width, UINT height, UINT levels, D3DFORMAT format)
    : m_width(width), m_height(height), m_levels(levels), m_format(format), m_locked_data(nullptr) {
    glGenTextures(1, &m_texture);
    g_trace_texinfo[m_texture] = TraceTex{ width, height, (unsigned)format };
    glBindTexture(GL_TEXTURE_2D, m_texture);
    invalidate_binding_caches();
    // Level 0 only, linear, clamped until levels are uploaded: no "incomplete texture" if a mip
    // filter is set before the chain exists (and NPOT sizes need CLAMP_TO_EDGE in WebGL 1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

D3D9Texture::~D3D9Texture() {
    if (m_texture) {   // GL may recycle the name: forget it in the caches
        if (g_last_bound_texture_id == m_texture) g_last_bound_texture_id = 0xFFFFFFFF;
        if (g_last_bound_lightmap_id == m_texture) g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_mipmapped_textures.erase(m_texture);
        glDeleteTextures(1, &m_texture);
    }
    delete[] static_cast<uint8_t *>(m_locked_data);
}

ULONG D3D9Texture::AddRef() { return 1; }   // the D3D8 layer owns the lifetime

ULONG D3D9Texture::Release() {
    delete this;
    return 0;
}

HRESULT D3D9Texture::GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) {
    if (!ppSurfaceLevel || Level != 0) return D3DERR_INVALIDCALL;   // level 0 only
    *ppSurfaceLevel = new D3D9Surface(m_width, m_height, m_texture, false);
    return D3D_OK;
}

HRESULT D3D9Texture::LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const void *pRect, DWORD Flags) {
    if (!pLockedRect || m_locked_data) return -1;   // locked twice would leak the first copy
    UINT w = std::max(1u, m_width >> Level), h = std::max(1u, m_height >> Level);
    size_t size;
    if (compressed(m_format)) {
        size_t pitch;
        size = dxt_size(m_format, w, h, &pitch);
        pLockedRect->Pitch = (INT)pitch;
    } else {
        pLockedRect->Pitch = (w * bits_per_pixel(m_format) + 7) / 8;
        size = (size_t)pLockedRect->Pitch * h;
    }
    m_locked_data = new uint8_t[size];
    pLockedRect->pBits = m_locked_data;
    return D3D_OK;
}

HRESULT D3D9Texture::UnlockRect(UINT Level) {
    if (!m_locked_data) return -1;
    glBindTexture(GL_TEXTURE_2D, m_texture);
    invalidate_binding_caches();
    UINT w = std::max(1u, m_width >> Level), h = std::max(1u, m_height >> Level);
    if (compressed(m_format)) {
        CHECK_GL_ERROR(glCompressedTexImage2D(GL_TEXTURE_2D, Level, gl_compressed_format(m_format), w, h, 0,
                                              dxt_size(m_format, w, h, nullptr), m_locked_data));
    } else {
        std::vector<uint32_t> px((size_t)w * h);
        to_rgba8(m_format, m_locked_data, px.size(), px.data());
        glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }

    // The mip chain: levels > 0 come from the file (DXT), level 0 of an uncompressed texture is
    // mipmapped here (glGenerateMipmap cannot do compressed formats), so a LINEAR mip filter (set
    // globally by the engine) never meets an incomplete texture.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Level);
    if (Level > 0) {
        g_mipmapped_textures.insert(m_texture);
    } else if (!compressed(m_format)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                        (int)std::floor(std::log2((double)std::max(m_width, m_height))));
        glGenerateMipmap(GL_TEXTURE_2D);
        g_mipmapped_textures.insert(m_texture);
    }
    delete[] static_cast<uint8_t *>(m_locked_data);
    m_locked_data = nullptr;
    return D3D_OK;
}
