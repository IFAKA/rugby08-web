// device_targets.cpp -- render targets, depth buffers, viewport and scissor.
#include "internal.h"
#include "device.h"

HRESULT D3D9Device::GetRenderTarget(DWORD Index, IDirect3DSurface9** ppRenderTarget) {
    if (Index != 0) return D3DERR_INVALIDCALL;
    if (ppRenderTarget) {
        *ppRenderTarget = m_current_rt;
        if (m_current_rt) {
            m_current_rt->AddRef(); // Proper COM semantics: caller must Release
        }
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetRenderTarget(DWORD Index, IDirect3DSurface9* pRenderTarget) {
    if (Index != 0 || !pRenderTarget) return D3DERR_INVALIDCALL;

    D3D9Surface* surf = static_cast<D3D9Surface*>(pRenderTarget);
    m_current_rt = surf;

    if (surf->m_is_backbuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); // Bind default
        m_fbo_rendering = false;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surf->m_gl_object, 0);
        m_fbo_rendering = true;
    }
    // Note: glFrontFace is set per draw call based on det(world*view) and Y-flip state,
    // because D3D9 auto-detects mirrored transforms but WebGL doesn't.

    // Reset viewport to match target size
    D3DVIEWPORT9 vp;
    vp.X = 0; vp.Y = 0;
    vp.Width = surf->m_width;
    vp.Height = surf->m_height;
    vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
    SetViewport(&vp);

    return D3D_OK;
}

HRESULT D3D9Device::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) {
    if (ppZStencilSurface) {
        *ppZStencilSurface = m_current_ds;
        if (m_current_ds) {
            m_current_ds->AddRef(); // Proper COM semantics: caller must Release
        }
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) {
    // If backbuffer is active, we assume default DS. If FBO is active, we attach DS.
    m_current_ds = static_cast<D3D9Surface*>(pZStencilSurface);
    
    if (!m_current_rt->m_is_backbuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        if (m_current_ds) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_current_ds->m_gl_object);
        } else {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        }
    }
    return D3D_OK;
}

HRESULT D3D9Device::StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                               IDirect3DSurface9* pDestSurface, const void* pDestRect,
                               D3DTEXTUREFILTERTYPE Filter) {
    if (!pSourceSurface || !pDestSurface) return D3DERR_INVALIDCALL;

    D3D9Surface* src = static_cast<D3D9Surface*>(pSourceSurface);
    D3D9Surface* dst = static_cast<D3D9Surface*>(pDestSurface);

    // Bind source to read framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src->m_is_backbuffer ? 0 : g_fbo);
    if (!src->m_is_backbuffer) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src->m_gl_object, 0);
    }

    // Bind dest to draw framebuffer
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->m_is_backbuffer ? 0 : g_fbo);
    if (!dst->m_is_backbuffer) {
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst->m_gl_object, 0);
    }

    // Validate FBO status before blit
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("[D3D9 Debug] StretchRect FAILED: Framebuffer incomplete (Read=%X Draw=%X)\n", 
               glCheckFramebufferStatus(GL_READ_FRAMEBUFFER), glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
    }

    // When FBO rendering uses Y-flip (u_fbo_flip_y), FBO textures store content
    // in D3D9 convention (Y=0 at top) while backbuffer uses OpenGL convention
    // (Y=0 at bottom). Flip Y when blitting between different conventions.
    bool conventions_differ = (src->m_is_backbuffer != dst->m_is_backbuffer);
    GLenum filter = (Filter == D3DTEXF_LINEAR) ? GL_LINEAR : GL_NEAREST;
    if (conventions_differ) {
        // Flip destination Y to compensate for convention mismatch
        glBlitFramebuffer(0, 0, src->m_width, src->m_height,
                         0, dst->m_height, dst->m_width, 0,
                         GL_COLOR_BUFFER_BIT, filter);
    } else {
        glBlitFramebuffer(0, 0, src->m_width, src->m_height,
                         0, 0, dst->m_width, dst->m_height,
                         GL_COLOR_BUFFER_BIT, filter);
    }

    // Restore original RT binding
    if (m_current_rt->m_is_backbuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_current_rt->m_gl_object, 0);
    }
    
    return D3D_OK;
}

HRESULT D3D9Device::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                             D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                             BOOL Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) {
    if (!ppSurface) return D3DERR_INVALIDCALL;
    
    GLuint rb;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Width, Height); // Use standard D24S8
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    *ppSurface = new D3D9Surface(Width, Height, rb, true);
    return D3D_OK;
}

HRESULT D3D9Device::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) {
    if (!ppSurface) return D3DERR_INVALIDCALL;
    *ppSurface = new D3D9Surface(Width, Height, 0, false); 
    return D3D_OK;
}

HRESULT D3D9Device::SetViewport(const D3DVIEWPORT9* pViewport) {
    if (!pViewport) return D3DERR_INVALIDCALL;
    g_viewport = *pViewport;

    if (!m_current_rt) return D3D_OK;

    // WebGL Y is bottom-to-top: flip Y origin
    int gl_y = m_current_rt->m_height - (pViewport->Y + pViewport->Height);
    glViewport(pViewport->X, gl_y, pViewport->Width, pViewport->Height);
    g_cached_viewport[0] = pViewport->X;
    g_cached_viewport[1] = gl_y;
    g_cached_viewport[2] = pViewport->Width;
    g_cached_viewport[3] = pViewport->Height;

    glDepthRangef(pViewport->MinZ, pViewport->MaxZ);
    return D3D_OK;
}

HRESULT D3D9Device::GetViewport(D3DVIEWPORT9* pViewport) {
    if (!pViewport) return D3DERR_INVALIDCALL;
    *pViewport = g_viewport;
    return D3D_OK;
}

HRESULT D3D9Device::SetScissorRect(const RECT* pRect) {
    if (!pRect) return D3DERR_INVALIDCALL;
    
    if (!m_current_rt) return D3D_OK;

    // WebGL Y is bottom-to-top: flip Y origin
    int gl_y = m_current_rt->m_height - pRect->bottom;
    int gl_w = pRect->right - pRect->left;
    int gl_h = pRect->bottom - pRect->top;
    glScissor(pRect->left, gl_y, gl_w, gl_h);
    g_cached_scissor_box[0] = pRect->left;
    g_cached_scissor_box[1] = gl_y;
    g_cached_scissor_box[2] = gl_w;
    g_cached_scissor_box[3] = gl_h;
    return D3D_OK;
}
