// resources.h -- the renderer's surface, texture, buffer and state-block objects.
#pragma once
#include "internal.h"

class D3D9Surface : public IDirect3DSurface9 {
public:
    GLuint m_gl_object;      // Texture ID or Renderbuffer ID
    bool m_is_renderbuffer;  // True if Renderbuffer (Depth), False if Texture
    bool m_is_backbuffer;    // True if this represents the default backbuffer (Canvas)
    UINT m_width;
    UINT m_height;
    ULONG m_refCount;        // Reference count for COM-style lifetime management

    


    
    
    
    D3D9Surface(UINT width, UINT height, GLuint gl_obj, bool is_rb, bool is_bb = false);
    virtual ~D3D9Surface();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
    virtual HRESULT UnlockRect() override;
    virtual HRESULT GetDesc(D3DSURFACE_DESC* pDesc) override;
};

class D3D9Texture : public IDirect3DTexture9 {
public:
    GLuint m_texture;
    UINT m_width;
    UINT m_height;
    UINT m_levels;
    D3DFORMAT m_format;
    void* m_locked_data;
    
public:
    


    
    
    
    
    D3D9Texture(UINT width, UINT height, UINT levels, D3DFORMAT format);
    virtual ~D3D9Texture();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect,
                            const void* pRect, DWORD Flags) override;
    virtual HRESULT UnlockRect(UINT Level) override;
    GLuint GetGLTexture() const;
};

class D3D9IndexBuffer : public IDirect3DIndexBuffer9 {
private:
    GLuint m_buffer;
    UINT m_length;
    D3DFORMAT m_format;
    void* m_locked_data;
    UINT m_locked_size;
    UINT m_locked_offset;
    DWORD m_locked_flags = 0;

public:
    


    
    

    D3D9IndexBuffer(UINT length, D3DFORMAT format);
    virtual ~D3D9IndexBuffer();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) override;
    virtual HRESULT Unlock() override;
    GLuint GetGLBuffer() const;
    D3DFORMAT GetFormat() const;
};

class D3D9VertexBuffer : public IDirect3DVertexBuffer9 {
private:
    GLuint m_buffer;
    UINT m_length;
    void* m_locked_data;
    UINT m_locked_size;
    UINT m_locked_offset;
    DWORD m_locked_flags = 0;
    
public:
    


    
    
    
    D3D9VertexBuffer(UINT length);
    virtual ~D3D9VertexBuffer();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) override;
    virtual HRESULT Unlock() override;
    GLuint GetGLBuffer() const;
};

// D3D9StateBlock class
class D3D9StateBlock : public IDirect3DStateBlock9 {
public:
    virtual ~D3D9StateBlock() {}
    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for state blocks
    }
    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    virtual HRESULT Capture() override {
        return D3D_OK;
    }
    virtual HRESULT Apply() override {
        return D3D_OK;
    }
};
