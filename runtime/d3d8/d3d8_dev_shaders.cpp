// d3d8_dev_shaders.cpp -- IDirect3DDevice8 vertex and pixel shader handles: declarations as FVFs, the
// programmable ones as GL programs (translation: d3d8_vshader.cpp), constant registers.
#include "d3d8_dev.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

// Vertex shaders. A declaration without function tokens that uses one stream in FVF order
// becomes that FVF; programmable ones are translated by d3d8_vshader.cpp (drawing them is not
// wired yet). Handles carry the high bit (FVF codes never set it here).
static std::vector<Vs8 *> g_vs;

static uint32_t decl_to_fvf(const Vs8 *v, bool *ok) {
    uint32_t fvf = 0, ntex = 0, texsize = 0;
    static const uint8_t comps[] = { 1, 2, 3, 4, 4, 4, 2, 4 };   // D3DVSDT_FLOAT1..SHORT4
    *ok = (v->streams & ~1u) == 0;
    uint32_t next = 0;
    for (const Vs8Elem &e : v->elems) {
        if (e.offset != next) *ok = false;                         // SKIP breaks FVF order
        next = e.offset + (e.type < 8 ? (uint32_t)(e.type <= 3 ? 4 * (e.type + 1) : e.type == 7 ? 8 : 4) : 0);
        switch (e.reg) {
        case 0: fvf |= e.type == 3 ? 0x004 : 0x002; break;         // POSITION: XYZ (XYZRHW if float4)
        case 3: fvf |= 0x010; break;                                // NORMAL
        case 4: fvf |= 0x020; break;                                // PSIZE
        case 5: fvf |= 0x040; break;                                // DIFFUSE
        case 6: fvf |= 0x080; break;                                // SPECULAR
        default:
            if (e.reg >= 7 && e.reg <= 14 && e.reg - 7u == ntex) {   // TEXCOORDn in order
                uint32_t c = e.type < 8 ? comps[e.type] : 2;
                texsize |= (c == 1 ? 3u : c == 3 ? 1u : c == 4 ? 2u : 0u) << (16 + 2 * ntex);
                ntex++;
            } else {
                *ok = false;
            }
        }
    }
    return fvf | ntex << 8 | texsize;
}

HLE_D3D(IDirect3DDevice8, CreateVertexShader) {   // (this, pDeclaration, pFunction, pHandle, Usage)
    Vs8 *v = new Vs8;
    int id = (int)g_vs.size();
    bool decl_ok = a[1] ? vs8_parse_decl(a[1], v) : false;
    v->fvf = decl_to_fvf(v, &v->fvf_ok);
    v->fvf_ok = v->fvf_ok && decl_ok;
    v->programmable = a[2] != 0;
    if (v->programmable) vs8_translate(a[2], v, id);
    else if (!v->fvf_ok)
        rt_log("CreateVertexShader #%d: declaration has no FVF equivalent (streams 0x%X)", id, v->streams);
    g_vs.push_back(v);
    wr32(a[3], 0x80000000u | (uint32_t)id);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, DeleteVertexShader) {
    (void)a;
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetVertexShaderConstant) {   // (this, Register, pConstantData, ConstantCount)
    DevState *s = S(a);
    for (uint32_t i = 0; i < a[3] && a[1] + i < 96; i++)
        for (int k = 0; k < 4; k++) {
            uint32_t u = rd32(a[2] + 16 * i + 4 * (uint32_t)k);
            memcpy(&s->vsc[a[1] + i][k], &u, 4);
        }
    if (rt_trace_vsc)
        for (uint32_t i = 0; i < a[3] && a[1] + i < 96; i++)
            printf("vsc c%u = %g %g %g %g\n", a[1] + i, s->vsc[a[1] + i][0], s->vsc[a[1] + i][1], s->vsc[a[1] + i][2],
                   s->vsc[a[1] + i][3]);
    return D3D_OK_;
}

HLE_D3D(IDirect3DDevice8, SetVertexShader) {  // FVF codes, or a handle from CreateVertexShader (high bit)
    DevState *s = S(a);
    s->vshader = a[1];
    if (a[1] & 0x80000000u) {
        uint32_t i = a[1] & 0x7FFFFFFFu;
        if (i >= g_vs.size()) return D3DERR_INVALIDCALL_;
        Vs8 *v = g_vs[i];
        for (const Vs8Const &c : v->consts)
            if (c.reg < 96) memcpy(s->vsc[c.reg], c.v, sizeof c.v);
        if (v->programmable) {
            /* The draw paths still read the FVF for texture and colour decisions: the declaration's
               equivalent when it has one. The shader itself runs as a GL program (d3d8_vshader.cpp
               vs8_gl, d3d9-webgl d3d9w_progvs_*). */
            if (v->fvf_ok) D(a)->SetFVF(v->fvf);
            static int off = getenv("RT_NO_PROGVS") != nullptr;   // A/B: draw without the GL program
            D3D9WProgVS *gl = off ? nullptr : vs8_gl(v, (int)i);
            d3d9w_progvs_use(gl, s->vsc);
            if (!gl) d3d8_unimpl("SetVertexShader(programmable: GL program failed)", 0);
            return D3D_OK_;
        }
        d3d9w_progvs_use(nullptr, nullptr);
        D(a)->SetFVF(v->fvf);
        return D3D_OK_;
    }
    d3d9w_progvs_use(nullptr, nullptr);
    {   /* each distinct FVF once: position kinds the backend may not handle (XYZB1-5 blend weights) */
        static uint32_t seen[64];
        static int nseen;
        int k = 0;
        while (k < nseen && seen[k] != a[1]) k++;
        if (k == nseen && nseen < 64) {
            seen[nseen++] = a[1];
            rt_log("SetVertexShader: FVF 0x%08X (position kind 0x%X)", a[1], a[1] & 0xE);
        }
    }
    D(a)->SetFVF(a[1]);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetVertexShader) {
    wr32(a[1], S(a)->vshader);
    return D3D_OK_;
}
// Pixel shaders (reported in the caps with RT_PS_CAPS=1 while they are being verified): ps.1.x
// through MojoShader, run by d3d9-webgl in place of the fixed-function texture stages.
static std::vector<Ps8 *> g_ps;   // handle - 1
HLE_D3D(IDirect3DDevice8, CreatePixelShader) {   // (this, pFunction, pHandle)
    Ps8 *p = new Ps8;
    int id = (int)g_ps.size() + 1;
    if (a[1]) ps8_translate(a[1], p, id);
    g_ps.push_back(p);
    wr32(a[2], (uint32_t)id);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, DeletePixelShader) {
    (void)a;
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, SetPixelShaderConstant) {   // (this, Register, pConstantData, ConstantCount)
    DevState *s = S(a);
    for (uint32_t i = 0; i < a[3] && a[1] + i < 8; i++)
        for (int k = 0; k < 4; k++) {
            uint32_t u = rd32(a[2] + 16 * i + 4 * (uint32_t)k);
            memcpy(&s->psc[a[1] + i][k], &u, 4);
        }
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetPixelShaderConstant) {
    DevState *s = S(a);
    for (uint32_t i = 0; i < a[3] && a[1] + i < 8; i++)
        for (int k = 0; k < 4; k++) {
            uint32_t u;
            memcpy(&u, &s->psc[a[1] + i][k], 4);
            wr32(a[2] + 16 * i + 4 * (uint32_t)k, u);
        }
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, SetPixelShader) {   // (this, handle): 0 = fixed-function stages
    DevState *s = S(a);
    s->pshader = a[1];
    if (!a[1] || a[1] > g_ps.size()) {
        d3d9w_progps_use(nullptr, nullptr);
        return D3D_OK_;
    }
    D3D9WProgPS *gl = ps8_gl(g_ps[a[1] - 1], (int)a[1]);
    if (!gl) d3d8_unimpl("SetPixelShader(GL program failed)", 0);
    d3d9w_progps_use(gl, s->psc);
    return D3D_OK_;
}
HLE_D3D(IDirect3DDevice8, GetPixelShader) {
    wr32(a[1], S(a)->pshader);
    return D3D_OK_;
}

}  // extern "C"
