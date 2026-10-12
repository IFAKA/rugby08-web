// buffers.cpp -- vertex/index buffer storage and the draw-from-pointer (UP) buffers.
#include "internal.h"
#include "resources.h"

#ifndef D3DLOCK_NOOVERWRITE
#define D3DLOCK_NOOVERWRITE 0x00001000L
#endif
#ifndef D3DLOCK_DISCARD
#define D3DLOCK_DISCARD 0x00002000L
#endif

void EnsureUPBuffers() {
    if (g_up_vbo == 0) glGenBuffers(1, &g_up_vbo);
    if (g_up_ibo == 0) glGenBuffers(1, &g_up_ibo);
}

void GLBufferStore::create(GLenum target, UINT len) {
    length = len;
    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
    glBufferData(target, len, nullptr, GL_STATIC_DRAW);
    glBindBuffer(target, 0);
}

void GLBufferStore::destroy() {
    if (buffer) glDeleteBuffers(1, &buffer);
    delete[] locked;
    locked = nullptr;
}

HRESULT GLBufferStore::lock(UINT offset, UINT size, void **ppbData, DWORD flags) {
    if (!ppbData || locked) return -1;
    locked_size = size == 0 ? length - offset : size;   // size 0: to the end of the buffer
    locked_offset = offset;
    locked_flags = flags;
    locked = new uint8_t[locked_size];
    *ppbData = locked;
    return D3D_OK;
}

// An upload that does not stall Apple's GL-on-Metal driver: glBufferSubData into a buffer the GPU may
// still be reading flushes the command queue and waits (40% of a match frame). The lock flags say what
// is safe: DISCARD / whole-buffer locks orphan the storage (a fresh allocation, no wait), NOOVERWRITE
// writes a range the GPU is not using (an unsynchronized map; WebGL2 has no glMapBufferRange).
HRESULT GLBufferStore::unlock(GLenum target) {
    if (!locked) return -1;
    glBindBuffer(target, buffer);
    if (locked_size == length) {
        glBufferData(target, length, locked, GL_DYNAMIC_DRAW);
    } else if (locked_flags & D3DLOCK_DISCARD) {
        glBufferData(target, length, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(target, locked_offset, locked_size, locked);
#ifndef __EMSCRIPTEN__
    } else if (locked_flags & D3DLOCK_NOOVERWRITE) {
        void *dst = glMapBufferRange(target, locked_offset, locked_size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (dst) {
            memcpy(dst, locked, locked_size);
            glUnmapBuffer(target);
        } else {
            glBufferSubData(target, locked_offset, locked_size, locked);
        }
#endif
    } else {
        glBufferSubData(target, locked_offset, locked_size, locked);
    }
    glBindBuffer(target, 0);
    delete[] locked;
    locked = nullptr;
    return D3D_OK;
}
