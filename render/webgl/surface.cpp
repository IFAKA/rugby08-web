// surface.cpp -- D3D9Surface: a render target texture, a depth renderbuffer or the back buffer.
#include "internal.h"
#include "resources.h"

D3D9Surface::D3D9Surface(UINT width, UINT height, GLuint gl_obj, bool is_rb, bool is_bb)
    : m_width(width), m_height(height), m_gl_object(gl_obj), m_is_renderbuffer(is_rb), m_is_backbuffer(is_bb),
      m_refCount(1) {}

D3D9Surface::~D3D9Surface() {
    interp_forget(this);
    if (m_is_renderbuffer && m_gl_object != 0 && !m_is_backbuffer) { glDeleteRenderbuffers(1, &m_gl_object); }
    // If it's a texture surface, D3D9Texture owns the GL object, so we don't delete it here.
}

ULONG D3D9Surface::AddRef() { return ++m_refCount; }

ULONG D3D9Surface::Release() {
    ULONG ref = --m_refCount;
    if (ref == 0) { delete this; }
    return ref;
}
