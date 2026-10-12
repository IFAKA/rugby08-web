// texture.cpp -- D3D9Texture: lock into host memory, upload (format conversion) on unlock.
#include "internal.h"
#include "resources.h"

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// GLI library maps RGB DXT1 to a custom FOURCC 'GLI1'; treat like DXT1
#define D3DFMT_GLI1 ((D3DFORMAT)0x31494C47)

// Helper to check for DXT format
bool IsCompressedFormat(D3DFORMAT format) {
    return (format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5
            || format == D3DFMT_GLI1);
}

UINT GetFormatBpp(D3DFORMAT format) {
    switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
        case D3DFMT_A8B8G8R8:
        case D3DFMT_X8B8G8R8:
            return 32;
        case D3DFMT_R8G8B8:
            return 24;
        case D3DFMT_R5G6B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_INDEX16:
            return 16;
        default:
            return 32;
    }
}

// Convert D3D DXT format to GL extension enum
// D3D decodes DXT1 with punch-through alpha: a 3-colour block (c0 <= c1) makes index 3 transparent
// black (alpha 0), 4-colour blocks are opaque. That is the RGBA variant; the RGB one turns the
// transparent texels into opaque black (Rugby 08: black boxes around alpha-tested sprites).
GLenum GetGLCompressedFormat(D3DFORMAT fmt) {
    switch (fmt) {
        case D3DFMT_DXT1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case D3DFMT_GLI1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // GLI's RGB DXT1
        case D3DFMT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case D3DFMT_DXT5: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        default: return 0;
    }
}

D3D9Texture::D3D9Texture(UINT width, UINT height, UINT levels, D3DFORMAT format) : m_width(width), m_height(height), m_levels(levels), m_format(format), m_locked_data(nullptr) {
    glGenTextures(1, &m_texture);
    g_trace_texinfo[m_texture] = TraceTex{ width, height, (unsigned)format };
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // Invalidate ALL texture binding caches since we bound outside of draw calls
    g_last_bound_texture_id = 0xFFFFFFFF;
    g_last_bound_lightmap_id = 0xFFFFFFFF;
    g_cached_texture = 0xFFFFFFFF;

    // [WASM] Use GL_LINEAR as default to avoid "Texture Incomplete" if no mipmaps provided
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // [WASM] Restrict to Level 0 only until mip levels are uploaded/generated.
    // Prevents "Texture Incomplete" when mipmap filter is applied to a texture
    // that hasn't had its full mip chain uploaded yet.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    // [WASM] Use CLAMP_TO_EDGE which is required for NPOT textures in WebGL 1.0
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

D3D9Texture::~D3D9Texture() {
    if (m_texture) {
        // Invalidate caches if this texture was cached (ID may be recycled by GL)
        if (g_last_bound_texture_id == m_texture) g_last_bound_texture_id = 0xFFFFFFFF;
        if (g_last_bound_lightmap_id == m_texture) g_last_bound_lightmap_id = 0xFFFFFFFF;
        if (g_cached_texture == m_texture) g_cached_texture = 0xFFFFFFFF;
        g_mipmapped_textures.erase(m_texture);
        glDeleteTextures(1, &m_texture);
    }
    if (m_locked_data) {
        delete[] static_cast<uint8_t*>(m_locked_data);
    }
}

ULONG D3D9Texture::AddRef() {
    return 1; // Stub: no ref counting for textures (owned by game code)
}

ULONG D3D9Texture::Release() {
    delete this;
    return 0;
}

HRESULT D3D9Texture::GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
    if (!ppSurfaceLevel) return D3DERR_INVALIDCALL;
    if (Level != 0) return D3DERR_INVALIDCALL; // Only level 0 supported for now
    
    // Return a Surface wrapper for this texture
    *ppSurfaceLevel = new D3D9Surface(m_width, m_height, m_texture, false);
    return D3D_OK;
}

HRESULT D3D9Texture::LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect,
                            const void* pRect, DWORD Flags) {
    if (!pLockedRect) return -1;
    if (m_locked_data) return -1; // Already locked - prevent memory leak

    // Calculate mip dimensions
    UINT mip_width = std::max(1u, m_width >> Level);
    UINT mip_height = std::max(1u, m_height >> Level);

    // Calculate size for allocation
    size_t data_size = 0;
    if (IsCompressedFormat(m_format)) {
        // DXT1/GLI1: 8 bytes per 4x4 block
        // DXT3/5: 16 bytes per 4x4 block
        size_t block_size = (m_format == D3DFMT_DXT1 || m_format == D3DFMT_GLI1) ? 8 : 16;
        size_t blocks_w = (mip_width + 3) / 4;
        size_t blocks_h = (mip_height + 3) / 4;
        data_size = blocks_w * blocks_h * block_size;
        pLockedRect->Pitch = blocks_w * block_size;
    } else {
        // Uncompressed
        UINT bpp = GetFormatBpp(m_format);
        pLockedRect->Pitch = (mip_width * bpp + 7) / 8;
        data_size = pLockedRect->Pitch * mip_height;
    }

    m_locked_data = new uint8_t[data_size];
    pLockedRect->pBits = m_locked_data;

    return D3D_OK;
}

HRESULT D3D9Texture::UnlockRect(UINT Level) {
    if (!m_locked_data) return -1;
    
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // Invalidate ALL texture binding caches since we bound outside of draw calls
    g_last_bound_texture_id = 0xFFFFFFFF;
    g_last_bound_lightmap_id = 0xFFFFFFFF;
    g_cached_texture = 0xFFFFFFFF;

    // Calculate mip dimensions again
    UINT mip_width = std::max(1u, m_width >> Level);
    UINT mip_height = std::max(1u, m_height >> Level);

    if (IsCompressedFormat(m_format)) {
        // Compressed upload
        GLenum gl_fmt = GetGLCompressedFormat(m_format);

        size_t block_size = (m_format == D3DFMT_DXT1 || m_format == D3DFMT_GLI1) ? 8 : 16;
        size_t size = ((mip_width + 3) / 4) * ((mip_height + 3) / 4) * block_size;

        CHECK_GL_ERROR(glCompressedTexImage2D(GL_TEXTURE_2D, Level, gl_fmt, mip_width, mip_height, 0, size, m_locked_data));
    } 
    else 
    {
        // Uncompressed upload: Convert to RGBA32 for WebGL
        size_t pixel_count = mip_width * mip_height;
        std::vector<uint32_t> rgba_pixels(pixel_count);
        
        if (m_format == D3DFMT_A8R8G8B8 || m_format == D3DFMT_X8R8G8B8) {
            uint32_t* src = static_cast<uint32_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                uint32_t color = src[i];
                uint32_t b = (color >> 0) & 0xFF;
                uint32_t g = (color >> 8) & 0xFF;
                uint32_t r = (color >> 16) & 0xFF;
                uint32_t a = (color >> 24) & 0xFF;
                // Only force opaque for X8R8G8B8. A8R8G8B8 must respect actual alpha.
                if (m_format == D3DFMT_X8R8G8B8) a = 0xFF;
                rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
            }
        }
        else if (m_format == D3DFMT_A4R4G4B4) {
            uint16_t* src = static_cast<uint16_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                uint16_t color = src[i];
                uint32_t a = ((color >> 12) & 0xF); a = (a << 4) | a;
                uint32_t r = ((color >>  8) & 0xF); r = (r << 4) | r;
                uint32_t g = ((color >>  4) & 0xF); g = (g << 4) | g;
                uint32_t b = ((color >>  0) & 0xF); b = (b << 4) | b;
                rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
            }
        }
        else if (m_format == D3DFMT_A1R5G5B5 || m_format == 25 /* D3DFMT_X1R5G5B5 */) {
            uint16_t* src = static_cast<uint16_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                uint16_t color = src[i];
                uint32_t r = ((color >> 10) & 0x1F); r = (r << 3) | (r >> 2);
                uint32_t g = ((color >> 5) & 0x1F);  g = (g << 3) | (g >> 2);
                uint32_t b = ((color >> 0) & 0x1F);  b = (b << 3) | (b >> 2);
                uint32_t a = (m_format == D3DFMT_A1R5G5B5) ? (((color >> 15) & 0x1) ? 255 : 0) : 255;
                rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
            }
        }
        else if (m_format == D3DFMT_R5G6B5) {
            uint16_t* src = static_cast<uint16_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                uint16_t color = src[i];
                uint32_t r = ((color >> 11) & 0x1F) << 3;
                uint32_t g = ((color >> 5) & 0x3F) << 2;
                uint32_t b = ((color >> 0) & 0x1F) << 3;
                rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
            }
        }
        else if (m_format == D3DFMT_R8G8B8) {
            // 24-bit BGR (D3D R8G8B8 stores B,G,R byte order)
            printf("[WARN] D3DFMT_R8G8B8 texture uploaded (%ux%u) - untested path\n",
                   mip_width, mip_height);
            uint8_t* src = static_cast<uint8_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                uint32_t b = src[0];
                uint32_t g = src[1];
                uint32_t r = src[2];
                rgba_pixels[i] = (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
                src += 3;
            }
        }
        else {
            // Fallback for other formats
            uint32_t* src = static_cast<uint32_t*>(m_locked_data);
            for (size_t i = 0; i < pixel_count; ++i) {
                rgba_pixels[i] = src[i];
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, Level, GL_RGBA, mip_width, mip_height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels.data());


    }
    
    // Update mipmap state: expand GL_TEXTURE_MAX_LEVEL as levels are uploaded
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Level);

    if (Level > 0) {
        // Compressed textures with pre-baked mip chain (DDS files)
        g_mipmapped_textures.insert(m_texture);
    } else if (!IsCompressedFormat(m_format)) {
        // Non-compressed Level 0: auto-generate full mip chain.
        // glGenerateMipmap cannot operate on compressed formats, but works
        // for RGBA/etc. This ensures UI textures and other non-DXT assets
        // also have mipmaps, preventing "Texture Incomplete" when the engine
        // sets MIPFILTER=LINEAR globally.
        int max_level = (int)std::floor(std::log2((double)std::max(m_width, m_height)));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_level);
        glGenerateMipmap(GL_TEXTURE_2D);
        g_mipmapped_textures.insert(m_texture);
    }

    // Free the locked data
    delete[] static_cast<uint8_t*>(m_locked_data);
    m_locked_data = nullptr;

    return D3D_OK;
}

GLuint D3D9Texture::GetGLTexture() const {
    return m_texture;
}
