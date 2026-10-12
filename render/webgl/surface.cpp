// surface.cpp -- D3D9Surface: a render target texture, a depth renderbuffer or the back buffer.
#include "internal.h"
#include "resources.h"

D3D9Surface::D3D9Surface(UINT width, UINT height, GLuint gl_obj, bool is_rb, bool is_bb)
    : m_width(width), m_height(height), m_gl_object(gl_obj), m_is_renderbuffer(is_rb), m_is_backbuffer(is_bb),
      m_refCount(1) {}

D3D9Surface::~D3D9Surface() {
    if (m_is_renderbuffer && m_gl_object != 0 && !m_is_backbuffer) { glDeleteRenderbuffers(1, &m_gl_object); }
    // If it's a texture surface, D3D9Texture owns the GL object, so we don't delete it here.
}

ULONG D3D9Surface::AddRef() { return ++m_refCount; }

ULONG D3D9Surface::Release() {
    ULONG ref = --m_refCount;
    if (ref == 0) { delete this; }
    return ref;
}

HRESULT D3D9Surface::LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) {
    // Not implemented for Surfaces yet (usually needed for CPU access to render targets)
    return D3DERR_INVALIDCALL;
}

HRESULT D3D9Surface::UnlockRect() { return D3DERR_INVALIDCALL; }

HRESULT D3D9Surface::GetDesc(D3DSURFACE_DESC *pDesc) {
    if (!pDesc) return D3DERR_INVALIDCALL;
    memset(pDesc, 0, sizeof(D3DSURFACE_DESC));
    pDesc->Width = m_width;
    pDesc->Height = m_height;
    pDesc->Format = D3DFMT_X8R8G8B8; // Default format
    pDesc->Type = m_is_renderbuffer ? D3DRTYPE_SURFACE : D3DRTYPE_TEXTURE;
    pDesc->Pool = D3DPOOL_DEFAULT;
    return D3D_OK;
}
