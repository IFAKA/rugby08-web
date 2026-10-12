// device_state.cpp -- render states, transforms, lights, material, stage and sampler states.
#include "internal.h"
#include "device.h"

// Map D3DBLEND to GL enum
static GLenum d3d_blend_to_gl(DWORD blend) {
    switch (blend) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
    }
}

static GLenum d3d_cmp_to_gl(DWORD func) {
    switch (func) {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    case D3DCMP_ALWAYS: return GL_ALWAYS;
    default: return GL_ALWAYS;
    }
}

static GLenum d3d_stencil_op_to_gl(DWORD op) {
    switch (op) {
    case D3DSTENCILOP_KEEP: return GL_KEEP;
    case D3DSTENCILOP_ZERO: return GL_ZERO;
    case D3DSTENCILOP_REPLACE: return GL_REPLACE;
    case D3DSTENCILOP_INCRSAT: return GL_INCR;
    case D3DSTENCILOP_DECRSAT: return GL_DECR;
    case D3DSTENCILOP_INVERT: return GL_INVERT;
    case D3DSTENCILOP_INCR: return GL_INCR_WRAP;
    case D3DSTENCILOP_DECR: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

HRESULT D3D9Device::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) {
    if ((UINT)State < 256) m_render_states[State] = Value;
    if ((int)State == 29) g_render_state_specular = Value != 0;
    if ((int)State == 152) { // D3DRS_CLIPPLANEENABLE
        m_clip_plane_enabled = (Value != 0);
        return D3D_OK;
    }

    switch (State) {
    case D3DRS_ALPHABLENDENABLE:
        if (Value) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
        break;

    case D3DRS_SRCBLEND:
        m_src_blend = d3d_blend_to_gl(Value);
        glBlendFunc(m_src_blend, m_dst_blend);
        break;

    case D3DRS_DESTBLEND:
        m_dst_blend = d3d_blend_to_gl(Value);
        glBlendFunc(m_src_blend, m_dst_blend);
        break;

    case D3DRS_CULLMODE:
        // [WASM FIX] We set glFrontFace(GL_CW) to match D3D9's left-handed system
        // Now winding orders align:
        //   D3D9 & OpenGL: CW = front, CCW = back
        //
        // D3DCULL_NONE (1) -> Disable culling
        // D3DCULL_CW (2) -> Cull CW (front) faces -> glCullFace(GL_FRONT)
        // D3DCULL_CCW (3) -> Cull CCW (back) faces -> glCullFace(GL_BACK)

        if (Value == D3DCULL_NONE) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(Value == D3DCULL_CW ? GL_FRONT : GL_BACK);
        }
        break;

    case D3DRS_ZENABLE:
        if (Value) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        break;

    case D3DRS_ZWRITEENABLE: glDepthMask(Value ? GL_TRUE : GL_FALSE); break;

    case D3DRS_ZFUNC: glDepthFunc(d3d_cmp_to_gl(Value)); break;

    case D3DRS_ALPHATESTENABLE: m_alpha_test_enabled = (Value != 0); break;

    case D3DRS_ALPHAREF: m_alpha_ref = Value; break;

    case D3DRS_SCISSORTESTENABLE:
        if (Value) {
            glEnable(GL_SCISSOR_TEST);
            g_cached_scissor_test = GL_TRUE;
        } else {
            glDisable(GL_SCISSOR_TEST);
            g_cached_scissor_test = GL_FALSE;
        }
        break;

    case D3DRS_STENCILENABLE:
        if (Value) glEnable(GL_STENCIL_TEST);
        else glDisable(GL_STENCIL_TEST);
        break;
    // Each D3D stencil state is independent; GL sets func/ref/mask and the three ops together,
    // so apply the full tracked set whenever any one changes.
    case D3DRS_STENCILFUNC:
    case D3DRS_STENCILREF:
    case D3DRS_STENCILMASK:
        glStencilFunc(d3d_cmp_to_gl(m_render_states[D3DRS_STENCILFUNC]), (GLint)m_render_states[D3DRS_STENCILREF],
                      m_render_states[D3DRS_STENCILMASK]);
        break;
    case D3DRS_STENCILFAIL:
    case D3DRS_STENCILZFAIL:
    case D3DRS_STENCILPASS:
        glStencilOp(d3d_stencil_op_to_gl(m_render_states[D3DRS_STENCILFAIL]),
                    d3d_stencil_op_to_gl(m_render_states[D3DRS_STENCILZFAIL]),
                    d3d_stencil_op_to_gl(m_render_states[D3DRS_STENCILPASS]));
        break;
    case D3DRS_STENCILWRITEMASK: glStencilMask(Value); break;

    case D3DRS_COLORWRITEENABLE:
        glColorMask((Value & D3DCOLORWRITEENABLE_RED) ? GL_TRUE : GL_FALSE,
                    (Value & D3DCOLORWRITEENABLE_GREEN) ? GL_TRUE : GL_FALSE,
                    (Value & D3DCOLORWRITEENABLE_BLUE) ? GL_TRUE : GL_FALSE,
                    (Value & D3DCOLORWRITEENABLE_ALPHA) ? GL_TRUE : GL_FALSE);
        break;
    case D3DRS_TEXTUREFACTOR: m_texture_factor = Value; break;

    case D3DRS_FOGENABLE: m_fog_enabled = (Value != 0); break;
    case D3DRS_FOGCOLOR: m_fog_color = Value; break;
    case D3DRS_FOGSTART: m_fog_start = *(float *)&Value; break;
    case D3DRS_FOGEND: m_fog_end = *(float *)&Value; break;
    case D3DRS_FOGTABLEMODE:
        // Only D3DFOG_LINEAR is implemented. D3DFOG_EXP and D3DFOG_EXP2 are not supported.
        // TODO: implement EXP/EXP2 fog modes if needed.
        break;
    case D3DRS_FOGVERTEXMODE:
        // Vertex fog mode is ignored; table fog (per-pixel) is always used.
        // TODO: implement vertex fog mode if needed.
        break;
    case D3DRS_RANGEFOGENABLE:
        // Range-based fog is not implemented; depth (eye-space Z) is used instead.
        // TODO: implement range-based fog if needed.
        break;
    }
    return D3D_OK;
}

HRESULT D3D9Device::GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) {
    if (!pValue) return D3DERR_INVALIDCALL;
    if ((UINT)State < 256) *pValue = m_render_states[State];
    else *pValue = 0;
    return D3D_OK;
}

HRESULT D3D9Device::SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) {
    if (!pMatrix) return -1;
    switch (State) {
    case D3DTS_WORLD: m_world = *pMatrix; break;
    case D3DTS_VIEW:
        m_view = *pMatrix;
        g_transform_view = *pMatrix;
        break;
    case (D3DTRANSFORMSTATETYPE)257:   // D3DTS_WORLDMATRIX(1)
        m_world1 = *pMatrix;
        break;
    case D3DTS_PROJECTION: m_projection = *pMatrix; break;
    case D3DTS_TEXTURE0:
    case D3DTS_TEXTURE1:
    case D3DTS_TEXTURE2:
    case D3DTS_TEXTURE3:
    case D3DTS_TEXTURE4:
    case D3DTS_TEXTURE5:
    case D3DTS_TEXTURE6:
    case D3DTS_TEXTURE7: break;   // texture transforms: not applied (no D3DTSS_TEXTURETRANSFORMFLAGS support)
    default: return -1;
    }
    return D3D_OK;
}

HRESULT D3D9Device::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) {
    if (!pMatrix) return -1;

    switch (State) {
    case D3DTS_WORLD: *pMatrix = m_world; break;
    case D3DTS_VIEW: *pMatrix = m_view; break;
    case D3DTS_PROJECTION: *pMatrix = m_projection; break;
    default: return -1;
    }

    return D3D_OK;
}

HRESULT D3D9Device::SetClipPlane(DWORD Index, const float *pPlane) {
    if (Index > 0 || !pPlane) return D3DERR_INVALIDCALL;
    memcpy(m_clip_planes[Index], pPlane, 4 * sizeof(float));
    return D3D_OK;
}

HRESULT D3D9Device::GetClipPlane(DWORD Index, float *pPlane) {
    if (Index > 0 || !pPlane) return D3DERR_INVALIDCALL;
    memcpy(pPlane, m_clip_planes[Index], 4 * sizeof(float));
    return D3D_OK;
}

HRESULT D3D9Device::SetLight(DWORD Index, const D3DLIGHT9 *pLight) {
    if (Index >= 8 || !pLight) return D3DERR_INVALIDCALL;
    g_lights[Index] = *pLight;
    return D3D_OK;
}

HRESULT D3D9Device::LightEnable(DWORD Index, BOOL Enable) {
    if (Index >= 8) return D3DERR_INVALIDCALL;
    g_light_enabled[Index] = Enable != FALSE;
    return D3D_OK;
}

HRESULT D3D9Device::GetLightEnable(DWORD Index, BOOL *pEnable) {
    if (Index >= 8 || !pEnable) return D3DERR_INVALIDCALL;
    *pEnable = g_light_enabled[Index] ? TRUE : FALSE;
    return D3D_OK;
}

HRESULT D3D9Device::GetLight(DWORD Index, D3DLIGHT9 *pLight) {
    if (Index >= 8 || !pLight) return D3DERR_INVALIDCALL;
    *pLight = g_lights[Index];
    return D3D_OK;
}

HRESULT D3D9Device::SetMaterial(const D3DMATERIAL9 *pMaterial) {
    if (!pMaterial) return D3DERR_INVALIDCALL;
    g_material = *pMaterial;
    return D3D_OK;
}

HRESULT D3D9Device::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    if (Stage >= 8 || Type >= 33) return D3DERR_INVALIDCALL;
    g_texture_stage_states[Stage][Type] = Value;
    return D3D_OK;
}

HRESULT D3D9Device::SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
    if (Sampler >= 8) return D3DERR_INVALIDCALL;
    if (Type < 14) { g_sampler_states[Sampler][Type] = Value; }
    return D3D_OK;
}
