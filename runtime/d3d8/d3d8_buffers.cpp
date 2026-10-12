// d3d8_buffers.cpp -- IDirect3DVertexBuffer8/IndexBuffer8: a guest shadow per buffer; Unlock uploads the
// range written since the lock with the lock flags (renderer: render/webgl/buffers.cpp).
#include "d3d8_int.hpp"
#include <algorithm>
#include <cstring>

extern "C" {

// ---- vertex / index buffers -------------------------------------------------------------------
static uint32_t new_buffer(Kind k, Obj *dev, uint32_t len, uint32_t usage, uint32_t fvf_or_fmt, uint32_t pool, uint32_t *out) {
    Obj *o = new Obj(k);
    if (k == K_VB) {
        IDirect3DVertexBuffer9 *vb = nullptr;
        if (dev9(dev)->CreateVertexBuffer(len, usage, fvf_or_fmt, (D3DPOOL)pool, &vb, nullptr) != D3D_OK) { delete o; return D3DERR_OUTOFVIDEOMEMORY_; }
        o->host = vb; o->fvf = fvf_or_fmt; o->fmt = 100;   // D3DFMT_VERTEXDATA
    } else {
        IDirect3DIndexBuffer9 *ib = nullptr;
        if (dev9(dev)->CreateIndexBuffer(len, usage, (D3DFORMAT)fvf_or_fmt, (D3DPOOL)pool, &ib, nullptr) != D3D_OK) { delete o; return D3DERR_OUTOFVIDEOMEMORY_; }
        o->host = ib; o->fmt = fvf_or_fmt;
    }
    o->owner = dev->guest; o->size = len; o->usage = usage; o->pool = pool;
    o->shadow = gcalloc(len ? len : 1);
    *out = d3d8_new(o);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, CreateVertexBuffer) { uint32_t g = 0, hr = new_buffer(K_VB, d3d8_obj(a[0]), a[1], a[2], a[3], a[4], &g); wr32(a[5], g); return hr; }
HLE_D3D(IDirect3DDevice8, CreateIndexBuffer) { uint32_t g = 0, hr = new_buffer(K_IB, d3d8_obj(a[0]), a[1], a[2], a[3], a[4], &g); wr32(a[5], g); return hr; }

static uint32_t buf_lock(const uint32_t *a) {   // (this, offset, size, BYTE**, flags)
    Obj *b = d3d8_obj(a[0]);
    uint32_t off = a[1], sz = a[2] ? a[2] : b->size - std::min(a[1], b->size);
    if (off > b->size || off + sz > b->size) return D3DERR_INVALIDCALL_;
    if (!b->locks++) { b->lock_lo = off; b->lock_hi = off + sz; b->lock_flags = 0; }
    else { b->lock_lo = std::min(b->lock_lo, off); b->lock_hi = std::max(b->lock_hi, off + sz); }
    b->lock_flags |= a[4];
    wr32(a[3], b->shadow + off);
    return D3D_OK_;
}

static uint32_t buf_unlock(const uint32_t *a) {
    Obj *b = d3d8_obj(a[0]);
    if (!b->locks) return D3DERR_INVALIDCALL_;
    if (--b->locks) return D3D_OK_;
    uint32_t n = b->lock_hi - b->lock_lo;
    if (!n) return D3D_OK_;
    // Small buffers are cheaper to re-send whole as a fresh allocation than to patch in place: the
    // GL driver stalls on a SubData into storage the GPU still reads. NOOVERWRITE keeps its range.
    uint32_t fl = b->lock_flags & 0x3000;   // D3DLOCK_NOOVERWRITE | D3DLOCK_DISCARD
    if (!(fl & 0x1000) && b->size <= (256u << 10)) { b->lock_lo = 0; b->lock_hi = b->size; n = b->size; fl = 0x2000; }
    void *p = nullptr;
    HRESULT hr = b->kind == K_VB ? ((IDirect3DVertexBuffer9 *)b->host)->Lock(b->lock_lo, n, &p, fl)
                                 : ((IDirect3DIndexBuffer9 *)b->host)->Lock(b->lock_lo, n, &p, fl);
    if (hr != D3D_OK || !p) return D3DERR_INVALIDCALL_;
    memcpy(p, G2H(b->shadow + b->lock_lo), n);
    if (b->kind == K_VB) ((IDirect3DVertexBuffer9 *)b->host)->Unlock(); else ((IDirect3DIndexBuffer9 *)b->host)->Unlock();
    return D3D_OK_;
}

HLE_D3D(IDirect3DVertexBuffer8, Lock) { return buf_lock(a); }
HLE_D3D(IDirect3DVertexBuffer8, Unlock) { return buf_unlock(a); }
HLE_D3D(IDirect3DIndexBuffer8, Lock) { return buf_lock(a); }
HLE_D3D(IDirect3DIndexBuffer8, Unlock) { return buf_unlock(a); }

HLE_D3D(IDirect3DVertexBuffer8, GetDesc) {   // Format Type Usage Pool Size FVF
    Obj *b = d3d8_obj(a[0]);
    wr32(a[1], 100); wr32(a[1] + 4, 6); wr32(a[1] + 8, b->usage); wr32(a[1] + 12, b->pool); wr32(a[1] + 16, b->size); wr32(a[1] + 20, b->fvf);
    return D3D_OK_;
}
HLE_D3D(IDirect3DIndexBuffer8, GetDesc) {    // Format Type Usage Pool Size
    Obj *b = d3d8_obj(a[0]);
    wr32(a[1], b->fmt); wr32(a[1] + 4, 7); wr32(a[1] + 8, b->usage); wr32(a[1] + 12, b->pool); wr32(a[1] + 16, b->size);
    return D3D_OK_;
}

}  // extern "C"
