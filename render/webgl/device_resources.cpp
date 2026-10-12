// device_resources.cpp -- creating and binding textures, buffers, streams and the FVF.
#include "internal.h"
#include "device.h"

HRESULT D3D9Device::CreateTexture(UINT Width, UINT Height, UINT Levels,
                                  DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9** ppTexture, void* pSharedHandle) {
    if (!ppTexture) return D3DERR_INVALIDCALL;

    D3D9Texture* tex = new D3D9Texture(Width, Height, Levels, Format);

    // If this is a render target, allocate texture storage immediately
    // so it can be attached to an FBO without causing "zero size attachment" errors
    if (Usage & D3DUSAGE_RENDERTARGET) {
        glBindTexture(GL_TEXTURE_2D, tex->m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // Invalidate texture binding cache
        g_last_bound_texture_id = 0xFFFFFFFF;
        g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_cached_texture = 0xFFFFFFFF;
    }

    *ppTexture = tex;
    return D3D_OK;
}

HRESULT D3D9Device::SetTexture(DWORD Stage, IDirect3DTexture9* pTexture) {
    if (Stage >= 8) return D3D_OK;
    
    GLuint texture_id = 0;
    if (pTexture) {
        D3D9Texture* tex = static_cast<D3D9Texture*>(pTexture);
        texture_id = tex->GetGLTexture();
    }
    
    // Optimization: return early if texture is already set for this stage
    if (g_texture_stages[Stage] == texture_id)
        return D3D_OK;

    g_texture_stages[Stage] = texture_id;
    return D3D_OK;
}

HRESULT D3D9Device::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format,
                                     D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer,
                                     void* pSharedHandle) {
    if (!ppIndexBuffer) return -1;
    *ppIndexBuffer = new D3D9IndexBuffer(Length, Format);
    return D3D_OK;
}

HRESULT D3D9Device::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF,
                                      D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer,
                                      void* pSharedHandle) {
    if (!ppVertexBuffer) return -1;
    *ppVertexBuffer = new D3D9VertexBuffer(Length);
    return D3D_OK;
}

HRESULT D3D9Device::SetIndices(IDirect3DIndexBuffer9* pIndexData) {
    if (pIndexData) {
        D3D9IndexBuffer* ib = static_cast<D3D9IndexBuffer*>(pIndexData);
        m_current_index_buffer = ib->GetGLBuffer();
        m_current_ib_format = ib->GetFormat();
    } else {
        m_current_index_buffer = 0;
        m_current_ib_format = D3DFMT_INDEX16;
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
                                   UINT OffsetInBytes, UINT Stride) {
    if (StreamNumber < 16) {          // every stream for programmable shaders (progvs_attribs)
        g_stream_vb[StreamNumber] = pStreamData ? static_cast<D3D9VertexBuffer*>(pStreamData)->GetGLBuffer() : 0;
        g_stream_stride[StreamNumber] = (GLsizei)Stride;
    }
    if (StreamNumber != 0) return D3D_OK; // the fixed-function paths read stream 0 only
    
    m_current_stride = Stride; // Store stride
    
    if (pStreamData) {
        D3D9VertexBuffer* vb = static_cast<D3D9VertexBuffer*>(pStreamData);
        m_current_vertex_buffer = vb->GetGLBuffer();
    } else {
        m_current_vertex_buffer = 0;
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetFVF(DWORD FVF) {
    g_current_fvf = FVF;
    return D3D_OK;
}
