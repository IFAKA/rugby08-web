// resources.h -- the renderer's surface, texture and buffer objects.
#pragma once
#include "internal.h"

// surface.cpp: a render-target texture level, a depth/stencil renderbuffer, or the back buffer.
class D3D9Surface : public IDirect3DSurface9 {
  public:
    GLuint m_gl_object;       // texture (render target) or renderbuffer (depth/stencil)
    bool m_is_renderbuffer;
    bool m_is_backbuffer;     // the default framebuffer (the canvas)
    UINT m_width;
    UINT m_height;
    ULONG m_refCount;

    D3D9Surface(UINT width, UINT height, GLuint gl_obj, bool is_rb, bool is_bb = false);
    virtual ~D3D9Surface();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
};

// texture.cpp: a 2D texture; a lock hands out host memory, the unlock uploads (and converts) it.
class D3D9Texture : public IDirect3DTexture9 {
  public:
    GLuint m_texture;
    UINT m_width;
    UINT m_height;
    UINT m_levels;
    D3DFORMAT m_format;
    void *m_locked_data;

    D3D9Texture(UINT width, UINT height, UINT levels, D3DFORMAT format);
    virtual ~D3D9Texture();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) override;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const void *pRect, DWORD Flags) override;
    virtual HRESULT UnlockRect(UINT Level) override;
    GLuint GetGLTexture() const { return m_texture; }
};

// buffers.cpp: a GL buffer behind a D3D vertex or index buffer. A lock hands out host memory; the
// unlock uploads the locked range the way the lock flags allow without a GPU wait.
struct GLBufferStore {
    GLuint buffer = 0;
    UINT length = 0;
    uint8_t *locked = nullptr;
    UINT locked_offset = 0, locked_size = 0;
    DWORD locked_flags = 0;

    void create(GLenum target, UINT len);
    void destroy();
    HRESULT lock(UINT offset, UINT size, void **ppbData, DWORD flags);
    HRESULT unlock(GLenum target);
};

class D3D9IndexBuffer : public IDirect3DIndexBuffer9 {
    GLBufferStore m_store;
    D3DFORMAT m_format;

  public:
    D3D9IndexBuffer(UINT length, D3DFORMAT format) : m_format(format) {
        m_store.create(GL_ELEMENT_ARRAY_BUFFER, length);
    }
    virtual ~D3D9IndexBuffer() { m_store.destroy(); }
    virtual ULONG AddRef() override { return 1; }   // the D3D8 layer owns the lifetime
    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override {
        return m_store.lock(OffsetToLock, SizeToLock, ppbData, Flags);
    }
    virtual HRESULT Unlock() override { return m_store.unlock(GL_ELEMENT_ARRAY_BUFFER); }
    GLuint GetGLBuffer() const { return m_store.buffer; }
    D3DFORMAT GetFormat() const { return m_format; }
};

class D3D9VertexBuffer : public IDirect3DVertexBuffer9 {
    GLBufferStore m_store;

  public:
    explicit D3D9VertexBuffer(UINT length) { m_store.create(GL_ARRAY_BUFFER, length); }
    virtual ~D3D9VertexBuffer() { m_store.destroy(); }
    virtual ULONG AddRef() override { return 1; }   // the D3D8 layer owns the lifetime
    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override {
        return m_store.lock(OffsetToLock, SizeToLock, ppbData, Flags);
    }
    virtual HRESULT Unlock() override { return m_store.unlock(GL_ARRAY_BUFFER); }
    GLuint GetGLBuffer() const { return m_store.buffer; }
};
