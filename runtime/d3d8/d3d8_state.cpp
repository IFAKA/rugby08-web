// d3d8_state.cpp -- IDirect3DDevice8 state: transforms, viewport, material, lights, clip planes, render
// and texture stage states, textures, streams and indices.
#include "d3d8_dev.hpp"
#include <cstdlib>
#include <cstring>

extern "C" {

// ---- transforms, viewport, material, lights, clip planes -------------------------------------
HLE_D3D(IDirect3DDevice8, SetTransform) {
    D3DMATRIX m; memcpy(&m, G2H(a[2]), 64);
    S(a)->xf[a[1]] = m;
    D(a)->SetTransform((D3DTRANSFORMSTATETYPE)a[1], &m);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetTransform) {
    DevState *s = S(a);
    auto it = s->xf.find(a[1]);
    D3DMATRIX m = {};
    if (it != s->xf.end()) m = it->second; else m._11 = m._22 = m._33 = m._44 = 1.f;
    memcpy(G2H(a[2]), &m, 64);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, MultiplyTransform) {   // state = M * state (D3D: pMatrix * current)
    DevState *s = S(a);
    D3DMATRIX m, c = {}, r = {};
    memcpy(&m, G2H(a[2]), 64);
    auto it = s->xf.find(a[1]);
    if (it != s->xf.end()) c = it->second; else c._11 = c._22 = c._33 = c._44 = 1.f;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += m.m[i][k] * c.m[k][j];
            r.m[i][j] = v;
        }
    s->xf[a[1]] = r;
    D(a)->SetTransform((D3DTRANSFORMSTATETYPE)a[1], &r);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetViewport) { DevState *s = S(a); memcpy(&s->vp, G2H(a[1]), 24); D(a)->SetViewport(&s->vp); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetViewport) { memcpy(G2H(a[1]), &S(a)->vp, 24); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetMaterial) { DevState *s = S(a); memcpy(&s->mat, G2H(a[1]), 68); D(a)->SetMaterial(&s->mat); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetMaterial) { memcpy(G2H(a[1]), &S(a)->mat, 68); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetLight) {
    D3DLIGHT9 l; memcpy(&l, G2H(a[2]), 104);
    d3d8_debug_light(a[1], &l, a[-1], a[2]);   // RT_LOG_LIGHTS (runtime/debug/frames.cpp)
    S(a)->lights[a[1]] = l;
    D(a)->SetLight(a[1], &l);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetLight) {
    auto &m = S(a)->lights;
    auto it = m.find(a[1]);
    if (it == m.end()) return D3DERR_INVALIDCALL_;
    memcpy(G2H(a[2]), &it->second, 104);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, LightEnable) { S(a)->light_on[a[1]] = a[2]; D(a)->LightEnable(a[1], (BOOL)a[2]); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetLightEnable) { wr32(a[2], S(a)->light_on[a[1]] ? 128 : 0); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetClipPlane) {
    if (a[1] >= 32) return D3DERR_INVALIDCALL_;
    memcpy(S(a)->clip[a[1]], G2H(a[2]), 16);
    D(a)->SetClipPlane(a[1], S(a)->clip[a[1]]);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetClipPlane) { if (a[1] >= 32) return D3DERR_INVALIDCALL_; memcpy(G2H(a[2]), S(a)->clip[a[1]], 16); return D3D_OK_; }

// ---- render / texture stage states ---------------------------------------------------------------
HLE_D3D(IDirect3DDevice8, SetRenderState) {
    uint32_t st = a[1], v = a[2];
    if (st >= 256) return D3DERR_INVALIDCALL_;
    S(a)->rs[st] = v;
    IDirect3DDevice9 *d = D(a);
    switch (st) {
    case 10: case 30: case 40: case 164:          // LINEPATTERN, ZVISIBLE, EDGEANTIALIAS, PATCHSEGMENTS: no effect here
        return D3D_OK_;
    case 43: case 159:                            // numbers d3d9-webgl uses for other states (see below)
        return D3D_OK_;
    case 143: d->SetRenderState(D3DRS_NORMALIZENORMALS, v); return D3D_OK_;   // d3d9-webgl: 43
    case 154: d->SetRenderState(D3DRS_POINTSIZE, v); return D3D_OK_;          // d3d9-webgl: 159
    case 47: {                                    // ZBIAS 0..16 -> DEPTHBIAS (d3d8to9 CalcDepthBias, 24-bit z)
        float b = -(float)v / 500000.f; uint32_t u; memcpy(&u, &b, 4);
        d->SetRenderState((D3DRENDERSTATETYPE)195, u);
        return D3D_OK_;
    }
    case 153: return D3D_OK_;                     // SOFTWAREVERTEXPROCESSING
    }
    d->SetRenderState((D3DRENDERSTATETYPE)st, v);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetRenderState) { if (a[1] >= 256) return D3DERR_INVALIDCALL_; wr32(a[2], S(a)->rs[a[1]]); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetTextureStageState) {   // (this, stage, type, value)
    uint32_t st = a[1], ty = a[2], v = a[3];
    if (st >= 8 || ty >= 32) return D3DERR_INVALIDCALL_;
    S(a)->tss[st][ty] = v;
    IDirect3DDevice9 *d = D(a);
    switch (ty) {                                  // D3D8 sampler states live in the stage states
    case 13: d->SetSamplerState(st, D3DSAMP_ADDRESSU, v); break;
    case 14: d->SetSamplerState(st, D3DSAMP_ADDRESSV, v); break;
    case 25: d->SetSamplerState(st, D3DSAMP_ADDRESSW, v); break;
    case 15: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)4, v); break;                 // BORDERCOLOR
    case 16: d->SetSamplerState(st, D3DSAMP_MAGFILTER, v > 2 ? 2 : v); break;         // cubic -> linear
    case 17: d->SetSamplerState(st, D3DSAMP_MINFILTER, v); break;
    case 18: d->SetSamplerState(st, D3DSAMP_MIPFILTER, v); break;
    case 19: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)8, v); break;                 // MIPMAPLODBIAS
    case 20: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)9, v); break;                 // MAXMIPLEVEL
    case 21: d->SetSamplerState(st, (D3DSAMPLERSTATETYPE)10, v); break;                // MAXANISOTROPY
    default: d->SetTextureStageState(st, (D3DTEXTURESTAGESTATETYPE)ty, v);
    }
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, GetTextureStageState) {
    if (a[1] >= 8 || a[2] >= 32) return D3DERR_INVALIDCALL_;
    wr32(a[3], S(a)->tss[a[1]][a[2]]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, ValidateDevice) { wr32(a[1], 1); return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetInfo) { (void)a; return 1; }   // S_FALSE: no device info
HLE_D3D(IDirect3DDevice8, SetPaletteEntries) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, SetCurrentTexturePalette) { (void)a; return D3D_OK_; }
HLE_D3D(IDirect3DDevice8, GetCurrentTexturePalette) { wr32(a[1], 0); return D3D_OK_; }

// ---- textures, streams, indices, FVF ------------------------------------------------------------

HLE_D3D(IDirect3DDevice8, SetTexture) {      // (this, stage, IDirect3DBaseTexture8*)
    DevState *s = S(a);
    if (a[1] >= 8) return D3DERR_INVALIDCALL_;
    Obj *t = d3d8_obj(a[2]);
    D(a)->SetTexture(a[1], t && t->kind == K_TEXTURE ? d3d8_texture9(t) : nullptr);
    if (t && t->kind != K_TEXTURE) d3d8_unimpl("SetTexture(cube/volume)", 0);
    addref(a[2]); release(s->tex[a[1]]); s->tex[a[1]] = a[2];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetTexture) { uint32_t g = a[1] < 8 ? S(a)->tex[a[1]] : 0; addref(g); wr32(a[2], g); return D3D_OK_; }

HLE_D3D(IDirect3DDevice8, SetStreamSource) {  // (this, stream, VB, stride)
    DevState *s = S(a);
    if (a[1] >= 16) return D3DERR_INVALIDCALL_;
    Obj *vb = d3d8_obj(a[2]);
    D(a)->SetStreamSource(a[1], vb ? (IDirect3DVertexBuffer9 *)vb->host : nullptr, 0, a[3]);
    addref(a[2]); release(s->stream[a[1]]); s->stream[a[1]] = a[2]; s->stride[a[1]] = a[3];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetStreamSource) {
    DevState *s = S(a);
    if (a[1] >= 16) return D3DERR_INVALIDCALL_;
    addref(s->stream[a[1]]); wr32(a[2], s->stream[a[1]]); wr32(a[3], s->stride[a[1]]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetIndices) {       // (this, IB, BaseVertexIndex): D3D9 moves the base to the draw
    DevState *s = S(a);
    Obj *ib = d3d8_obj(a[1]);
    D(a)->SetIndices(ib ? (IDirect3DIndexBuffer9 *)ib->host : nullptr);
    addref(a[1]); release(s->ib); s->ib = a[1]; s->base_vertex = a[2];
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetIndices) { DevState *s = S(a); addref(s->ib); wr32(a[1], s->ib); wr32(a[2], s->base_vertex); return D3D_OK_; }

}  // extern "C"
