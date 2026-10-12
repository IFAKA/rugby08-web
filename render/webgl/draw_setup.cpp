// draw_setup.cpp -- what every draw does before glDraw*: program, transforms and per-draw uniforms,
// the FVF vertex layout and attributes, the stage textures, and the UP upload buffers.
#include "internal.h"
#include "device.h"

// Byte offsets of the FVF components in a vertex. legacy: DrawPrimitive's original layout, which
// counts XYZ as 12 bytes whatever the blend weights (its stride is always the stream's).
FvfLayout fvf_layout(DWORD fvf, bool legacy) {
    FvfLayout l = {};
    l.xyz = fvf_xyz(fvf);
    l.rhw = fvf_rhw(fvf);
    l.normal = (fvf & D3DFVF_NORMAL) != 0;
    l.diffuse = (fvf & D3DFVF_DIFFUSE) != 0;
    l.tex_count = (fvf >> 8) & 0xF;
    size_t o = 0;
    if (l.rhw) {
        l.pos = o;
        o += 16;
    } else if (l.xyz) {
        l.pos = o;
        o += 12 + (legacy ? 0 : 4 * fvf_blend_weights(fvf));
    }
    if (l.normal) {
        l.nrm = o;
        o += 12;
    }
    if (l.diffuse) {
        l.col = o;
        o += 4;
    }
    l.tex = o;
    l.tex1 = o + (size_t)fvf_tex_components(fvf, 0) * 4;
    if (!legacy)
        for (int i = 0; i < l.tex_count; ++i) o += fvf_tex_components(fvf, i) * 4;
    l.stride = (GLsizei)o;
    return l;
}

// D3DPRIMITIVETYPE -> GL mode and vertex/index count. set: the draw path's original table (DrawPrimitive
// knew triangles only, DrawIndexedPrimitiveUP drew everything as a list); unknown types draw nothing.
bool gl_primitive(D3DPRIMITIVETYPE t, UINT prims, PrimSet set, GLenum *mode, UINT *count) {
    *mode = GL_TRIANGLES;
    *count = 0;
    if (set == PRIM_INDEXED_UP) {
        *count = (t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN) ? prims + 2 : prims * 3;
        return true;
    }
    switch (t) {
    case D3DPT_TRIANGLELIST:
        *mode = GL_TRIANGLES;
        *count = prims * 3;
        return true;
    case D3DPT_TRIANGLESTRIP:
        *mode = GL_TRIANGLE_STRIP;
        *count = prims + 2;
        return true;
    case D3DPT_TRIANGLEFAN:
        *mode = GL_TRIANGLE_FAN;
        *count = prims + 2;
        return true;
    default: break;
    }
    if (set == PRIM_TRIANGLES) return false;
    switch (t) {
    case D3DPT_LINELIST:
        *mode = GL_LINES;
        *count = prims * 2;
        return true;
    case D3DPT_LINESTRIP:
        *mode = GL_LINE_STRIP;
        *count = prims + 1;
        return true;
    case D3DPT_POINTLIST:
        *mode = GL_POINTS;
        *count = prims;
        return true;
    default: return false;
    }
}

// The program for this draw, its trace line, the render-target Y flip and the clip plane.
void D3D9Device::draw_begin(const char *path, unsigned count) {
    if (g_shader_program == 0) init_shaders();
    use_draw_program();
    trace_draw(path, count);
    glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
    glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
    if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);
}

void D3D9Device::viewport_size(float *w, float *h) const {
    *w = g_viewport.Width > 0 ? (float)g_viewport.Width : (float)g_logical_width;
    *h = g_viewport.Height > 0 ? (float)g_viewport.Height : (float)g_logical_height;
}

void D3D9Device::target_size(float *w, float *h) const {
    *w = m_current_rt ? (float)m_current_rt->m_width : (float)g_logical_width;
    *h = m_current_rt ? (float)m_current_rt->m_height : (float)g_logical_height;
}

// Transform, vertex blending, texture factor, LOD bias and fog uniforms. Pre-transformed (XYZRHW)
// vertices map pixel coordinates of a rhw_w x rhw_h screen to clip space.
void D3D9Device::draw_uniforms(float rhw_w, float rhw_h) {
    bool rhw = fvf_rhw(g_current_fvf);
    D3DMATRIX view_model, mvp;
    if (rhw) {
        memset(&mvp, 0, sizeof(D3DMATRIX));
        mvp.m[0][0] = 2.0f / rhw_w;
        mvp.m[1][1] = -2.0f / rhw_h;
        mvp.m[2][2] = 1.0f;
        mvp.m[3][0] = -1.0f;
        mvp.m[3][1] = 1.0f;
        mvp.m[3][3] = 1.0f;
        glFrontFace(GL_CW);
    } else {
        matrix_multiply(&view_model, &m_world, &m_view);
        matrix_multiply(&mvp, &view_model, &m_projection);
        // D3D flips culling for mirrored transforms (negative determinant) and WebGL does not; the
        // render-target Y flip reverses the winding too: exactly one flip -> CCW.
        bool mirror = det3x3(view_model) < 0.0f;
        glFrontFace(mirror != m_fbo_rendering ? GL_CCW : GL_CW);
    }
    gl_set_matrix_uniform(g_mvp_uniform, &mvp);

    // D3DRS_VERTEXBLEND with an XYZB1 position: the shader mixes world 0 and world 1 by the weight
    bool blend = fvf_blend_weights(g_current_fvf) > 0 && m_render_states[D3DRS_VERTEXBLEND] != 0;
    if (g_blend_uniform != -1) glUniform1i(g_blend_uniform, blend ? 1 : 0);
    if (blend) {
        D3DMATRIX vm1, mvp1;
        matrix_multiply(&vm1, &m_world1, &m_view);
        matrix_multiply(&mvp1, &vm1, &m_projection);
        gl_set_matrix_uniform(g_mvp1_uniform, &mvp1);
    }

    if (g_texture_factor_uniform != -1) {
        D3DCOLOR f = m_texture_factor;
        glUniform4f(g_texture_factor_uniform, ((f >> 16) & 0xFF) / 255.0f, ((f >> 8) & 0xFF) / 255.0f,
                    (f & 0xFF) / 255.0f, ((f >> 24) & 0xFF) / 255.0f);
    }

    // D3D stores the mipmap LOD bias as a float in a DWORD. GL's LOD rule picks lower-resolution mips
    // than D3D on the same hardware: -0.75 compensates.
    if (g_lod_bias_uniform != -1) {
        DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
        float lod_bias;
        memcpy(&lod_bias, &bias_bits, sizeof(float));
        glUniform1f(g_lod_bias_uniform, lod_bias - 0.75f);
    }

    bool fog = m_fog_enabled && !rhw;
    glUniform1i(g_fog_enabled_uniform, fog ? 1 : 0);
    if (fog) {
        glUniform3f(g_fog_color_uniform, ((m_fog_color >> 16) & 0xFF) / 255.0f, ((m_fog_color >> 8) & 0xFF) / 255.0f,
                    (m_fog_color & 0xFF) / 255.0f);
        glUniform1f(g_fog_start_uniform, m_fog_start);
        glUniform1f(g_fog_end_uniform, m_fog_end);
        gl_set_matrix_uniform(g_view_model_uniform, &view_model);
    }
}

// Stream-0 vertex attributes of the fixed-function program, from the buffer bound to GL_ARRAY_BUFFER
// (base: byte offset of vertex 0), and the lighting uniforms. tex1: bind the second texture coordinate.
void D3D9Device::draw_attribs(const FvfLayout &l, GLsizei stride, size_t base, bool tex1) {
    if ((l.xyz || l.rhw) && g_position_location != -1) {
        glEnableVertexAttribArray(g_position_location);
        glVertexAttribPointer(g_position_location, l.rhw ? 4 : 3, GL_FLOAT, GL_FALSE, stride, (void *)(base + l.pos));
    }
    if (l.normal && g_normal_location != -1) {
        glEnableVertexAttribArray(g_normal_location);
        glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void *)(base + l.nrm));
    } else if (g_normal_location != -1) {
        glDisableVertexAttribArray(g_normal_location);
        glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f);
    }

    bool lit = l.normal && !l.rhw && m_render_states[D3DRS_LIGHTING];
    glUniform1i(g_lighting_enabled_uniform, lit ? 1 : 0);
    if (lit) SetFFPLightingUniforms(&m_world, m_render_states, l.diffuse);
    else gl_set_matrix_uniform(g_world_uniform, &m_world);   // the clip plane reads the world position

    if (l.diffuse && g_color_location != -1) {   // lit: the vertex colour may be the material (D3DMCS_COLOR1)
        glEnableVertexAttribArray(g_color_location);
        glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)(base + l.col));
    } else if (g_color_location != -1) {
        glDisableVertexAttribArray(g_color_location);
        glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (l.tex_count > 0 && g_texcoord_location != -1) {
        glEnableVertexAttribArray(g_texcoord_location);
        glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void *)(base + l.tex));
    } else if (g_texcoord_location != -1) {
        glDisableVertexAttribArray(g_texcoord_location);
        glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f);
    }
    if (tex1 && l.tex_count > 1 && g_texcoord1_location != -1) {
        glEnableVertexAttribArray(g_texcoord1_location);
        glVertexAttribPointer(g_texcoord1_location, fvf_tex_components(g_current_fvf, 1), GL_FLOAT, GL_FALSE, stride,
                              (void *)(base + l.tex1));
    } else if (g_texcoord1_location != -1) {
        glDisableVertexAttribArray(g_texcoord1_location);
        glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f);
    }
}

// Stage 0's texture on unit 0 with its sampler states, and the alpha test. unbind: with no texture,
// bind none on unit 0 (the UP paths; the buffer paths leave the binding as it is).
void D3D9Device::draw_textures(int tex_count, bool unbind, Lightmap lightmap) {
    if (g_texture_stages[0] != 0 && tex_count > 0) {
        glActiveTexture(GL_TEXTURE0);
        if (g_cached_texture != g_texture_stages[0]) {
            glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]);
            g_cached_texture = g_texture_stages[0];
        }
        apply_sampler_states(0, true);
        glUniform1i(g_texture_uniform, 0);
        glUniform1i(g_use_texture_uniform, 1);
    } else {
        if (unbind) {   // unit 0 first: the last active unit may be the lightmap's
            glActiveTexture(GL_TEXTURE0);
            if (g_cached_texture != 0) {
                glBindTexture(GL_TEXTURE_2D, 0);
                g_cached_texture = 0;
            }
        }
        glUniform1i(g_use_texture_uniform, 0);
    }

    // Stage 1 (the lightmap) on unit 1, with a second texture coordinate set
    if (lightmap != LM_OFF && g_texture_stages[1] != 0 && tex_count > 1) {
        glActiveTexture(GL_TEXTURE1);
        if (lightmap == LM_BIND || g_last_bound_lightmap_id != g_texture_stages[1]) {
            glBindTexture(GL_TEXTURE_2D, g_texture_stages[1]);
            g_last_bound_lightmap_id = g_texture_stages[1];
        }
        if (lightmap == LM_BIND_SAMPLED) apply_sampler_states(1, true);
        glUniform1i(g_texture_lm_uniform, 1);
        glUniform1i(g_use_lightmap_uniform, 1);
        glActiveTexture(GL_TEXTURE0);   // later draws assume unit 0 is active
    } else {
        glUniform1i(g_use_lightmap_uniform, 0);
    }

    glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0);
    glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref);
}

// Guest vertices for the fixed-function program: D3DCOLOR diffuse is BGRA in memory, GL reads RGBA.
// A programmable shader swizzles its colour inputs itself (progvs_vertex_source).
const void *up_vertices(const void *data, size_t size, UINT n, GLsizei stride, const FvfLayout &l) {
    static std::vector<uint8_t> converted;
    if (!l.diffuse || g_progvs_active) return data;
    converted.resize(size);
    memcpy(converted.data(), data, size);
    for (UINT i = 0; i < n; ++i) {   // alpha stays: D3D honours diffuse alpha 0 (UI glow, fades)
        uint8_t *c = converted.data() + (size_t)i * stride + l.col;
        std::swap(c[0], c[2]);
    }
    return converted.data();
}

// Into the UP buffer bound to target: grow (x2) when too small, then orphan before writing. The
// previous draw may still read the old storage, and writing into it makes Apple's GL-on-Metal flush
// the queue and wait (40% of a match frame); fresh storage never waits.
void up_upload(GLenum target, size_t *capacity, size_t size, const void *data) {
    if (size > *capacity) *capacity = size * 2;
    glBufferData(target, *capacity, nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(target, 0, size, data);
}
