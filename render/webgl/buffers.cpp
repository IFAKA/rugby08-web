// buffers.cpp -- vertex/index buffers and the draw-from-pointer (UP) buffers.
#include "internal.h"
#include "resources.h"

// Manual UP buffers (Moved up for RestoreContext)

void EnsureUPBuffers() {
    if (g_up_vbo == 0) {
        glGenBuffers(1, &g_up_vbo);
    }
    if (g_up_ibo == 0) {
        glGenBuffers(1, &g_up_ibo);
    }
}

// Buffer upload that avoids stalling on Apple's GL-on-Metal driver: glBufferSubData into a buffer the
// GPU may still be reading flushes the command queue and waits (40% of a match frame). D3D's lock
// flags say what is safe: DISCARD / whole-buffer locks orphan the storage (a fresh allocation, no
// wait), NOOVERWRITE writes a range the GPU is not using (unsynchronized map).
#ifndef D3DLOCK_NOOVERWRITE
#define D3DLOCK_NOOVERWRITE 0x00001000L
#endif
#ifndef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0x00002000L
#endif
static void upload_buffer(GLenum target, GLuint buf, UINT length, UINT off, UINT size, const void* data, DWORD flags) {
    glBindBuffer(target, buf);
    if (size == length) {
        glBufferData(target, length, data, GL_DYNAMIC_DRAW);
    } else if (flags & D3DLOCK_DISCARD) {
        glBufferData(target, length, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(target, off, size, data);
#ifndef __EMSCRIPTEN__
    } else if (flags & D3DLOCK_NOOVERWRITE) {
        void* dst = glMapBufferRange(target, off, size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (dst) { memcpy(dst, data, size); glUnmapBuffer(target); }
        else glBufferSubData(target, off, size, data);
#endif
    } else {
        glBufferSubData(target, off, size, data);
    }
    glBindBuffer(target, 0);
}

D3D9IndexBuffer::D3D9IndexBuffer(UINT length, D3DFORMAT format) : m_length(length), m_format(format), m_locked_data(nullptr), m_locked_size(0), m_locked_offset(0) {
    glGenBuffers(1, &m_buffer);
    // Initialize buffer size
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, length, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

D3D9IndexBuffer::~D3D9IndexBuffer() {
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
    if (m_locked_data) {
        delete[] static_cast<uint8_t*>(m_locked_data);
    }
}

ULONG D3D9IndexBuffer::AddRef() {
    return 1; // Stub: no ref counting for index buffers
}

ULONG D3D9IndexBuffer::Release() {
    delete this;
    return 0;
}

HRESULT D3D9IndexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    if (!ppbData) return -1;
    if (m_locked_data) return -1; // Already locked
    
    // Simple implementation: always lock the whole buffer or requested size
    UINT size = (SizeToLock == 0) ? (m_length - OffsetToLock) : SizeToLock;
    m_locked_data = new uint8_t[size];
    m_locked_size = size;
    m_locked_offset = OffsetToLock;
    m_locked_flags = Flags;
    *ppbData = m_locked_data;
    
    return D3D_OK;
}

HRESULT D3D9IndexBuffer::Unlock() {
    if (!m_locked_data) return -1;
    
    upload_buffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer, m_length, m_locked_offset, m_locked_size, m_locked_data, m_locked_flags);
    
    delete[] static_cast<uint8_t*>(m_locked_data);
    m_locked_data = nullptr;
    return D3D_OK;
}

GLuint D3D9IndexBuffer::GetGLBuffer() const {
    return m_buffer;
}

D3DFORMAT D3D9IndexBuffer::GetFormat() const {
    return m_format;
}
D3D9VertexBuffer::D3D9VertexBuffer(UINT length) : m_length(length), m_locked_data(nullptr), m_locked_size(0), m_locked_offset(0) {
    glGenBuffers(1, &m_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ARRAY_BUFFER, length, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

D3D9VertexBuffer::~D3D9VertexBuffer() {
    if (m_buffer) {
        glDeleteBuffers(1, &m_buffer);
    }
    if (m_locked_data) {
        delete[] static_cast<uint8_t*>(m_locked_data);
    }
}

ULONG D3D9VertexBuffer::AddRef() {
    return 1; // Stub: no ref counting for vertex buffers
}

ULONG D3D9VertexBuffer::Release() {
    delete this;
    return 0;
}

HRESULT D3D9VertexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    if (!ppbData) return -1;
    if (m_locked_data) return -1; // Already locked
    
    UINT size = (SizeToLock == 0) ? (m_length - OffsetToLock) : SizeToLock;
    m_locked_data = new uint8_t[size];
    m_locked_size = size;
    m_locked_offset = OffsetToLock;
    m_locked_flags = Flags;
    *ppbData = m_locked_data;
    
    return D3D_OK;
}

HRESULT D3D9VertexBuffer::Unlock() {
    if (!m_locked_data) return -1;
    
    upload_buffer(GL_ARRAY_BUFFER, m_buffer, m_length, m_locked_offset, m_locked_size, m_locked_data, m_locked_flags);
    
    delete[] static_cast<uint8_t*>(m_locked_data);
    m_locked_data = nullptr;
    return D3D_OK;
}

GLuint D3D9VertexBuffer::GetGLBuffer() const {
    return m_buffer;
}
