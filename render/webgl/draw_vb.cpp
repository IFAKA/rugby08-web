// draw_vb.cpp -- draws from vertex/index buffers (DrawPrimitive, DrawIndexedPrimitive).
#include "internal.h"
#include "device.h"

HRESULT D3D9Device::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    // Non-indexed primitive drawing using hardware VB (used by cloth rendering)
    if (m_current_vertex_buffer == 0) return -1;
    if (g_shader_program == 0) init_shaders();
    use_draw_program(); trace_draw("DP", (unsigned)PrimitiveCount);

    // FBO Y-flip and clip plane uniforms
    glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
    glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
    if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

    D3DMATRIX view_model, mvp;
    if (fvf_rhw(g_current_fvf)) {
        float vw = (g_viewport.Width > 0) ? (float)g_viewport.Width : (float)g_logical_width;
        float vh = (g_viewport.Height > 0) ? (float)g_viewport.Height : (float)g_logical_height;
        memset(&mvp, 0, sizeof(D3DMATRIX));
        mvp.m[0][0] = 2.0f / vw;
        mvp.m[1][1] = -2.0f / vh;
        mvp.m[2][2] = 1.0f;
        mvp.m[3][0] = -1.0f;
        mvp.m[3][1] = 1.0f;
        mvp.m[3][3] = 1.0f;
    } else {
        matrix_multiply(&view_model, &m_world, &m_view);
        matrix_multiply(&mvp, &view_model, &m_projection);
    }

    // D3D9 auto-detects mirrored transforms (negative determinant) and flips culling.
    // WebGL doesn't, so we detect it per draw call. Y-flip also reverses winding.
    // XOR: exactly one flip → CCW, both or neither → CW.
    if (!(fvf_rhw(g_current_fvf))) {
        bool mirror = (det3x3(view_model) < 0.0f);
        glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
    } else {
        glFrontFace(GL_CW);
    }

    gl_set_matrix_uniform(g_mvp_uniform, &mvp);
    {   // D3DRS_VERTEXBLEND with an XYZB1 position: the shader mixes world 0 and world 1 by the weight
        bool blend = fvf_blend_weights(g_current_fvf) > 0 && m_render_states[D3DRS_VERTEXBLEND] != 0;
        if (g_blend_uniform != -1) glUniform1i(g_blend_uniform, blend ? 1 : 0);
        if (blend) {
            D3DMATRIX vm1, mvp1;
            matrix_multiply(&vm1, &m_world1, &m_view);
            matrix_multiply(&mvp1, &vm1, &m_projection);
            gl_set_matrix_uniform(g_mvp1_uniform, &mvp1);
        }
    }

    // Set texture factor
    if (g_texture_factor_uniform != -1) {
        float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
        float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
        float b = (m_texture_factor & 0xFF) / 255.0f;
        float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
        glUniform4f(g_texture_factor_uniform, r, g, b, a);
    }

    // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
    // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
    // lower-res mip levels compared to D3D9 on the same hardware. An additional
    // -0.75 offset compensates for this difference.
    if (g_lod_bias_uniform != -1) {
        DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
        float lod_bias;
        memcpy(&lod_bias, &bias_bits, sizeof(float));
        lod_bias -= 0.75f;
        glUniform1f(g_lod_bias_uniform, lod_bias);
    }

    // Fog uniforms
    {
        bool apply_fog = m_fog_enabled && !(fvf_rhw(g_current_fvf));
        glUniform1i(g_fog_enabled_uniform, apply_fog ? 1 : 0);
        if (apply_fog) {
            float r = ((m_fog_color >> 16) & 0xFF) / 255.0f;
            float g_c = ((m_fog_color >> 8) & 0xFF) / 255.0f;
            float b = (m_fog_color & 0xFF) / 255.0f;
            glUniform3f(g_fog_color_uniform, r, g_c, b);
            glUniform1f(g_fog_start_uniform, m_fog_start);
            glUniform1f(g_fog_end_uniform, m_fog_end);
            gl_set_matrix_uniform(g_view_model_uniform, &view_model);
        }
    }

    // Set Stage 0 texture stage states
    if (g_stage0_colorop_uniform != -1)
        glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
    if (g_stage0_colorarg1_uniform != -1)
        glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
    if (g_stage0_colorarg2_uniform != -1)
        glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
    if (g_stage0_alphaop_uniform != -1)
        glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
    if (g_stage0_alphaarg1_uniform != -1)
        glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
    if (g_stage0_alphaarg2_uniform != -1)
        glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);

    glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);

    // FVF Parsing
    bool has_xyz = fvf_xyz(g_current_fvf);
    bool has_xyzrhw = (fvf_rhw(g_current_fvf)) != 0;
    bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
    bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
    int tex_count = (g_current_fvf >> 8) & 0xF;

    // Calculate offsets accounting for all FVF components
    size_t offset = 0;
    size_t position_offset = 0;
    size_t normal_offset = 0;
    size_t color_offset = 0;
    size_t tex_offset = 0;

    if (has_xyz) { position_offset = offset; offset += 12; }
    if (has_xyzrhw) { position_offset = offset; offset += 16; }
    if (has_normal) { normal_offset = offset; offset += 12; }
    if (has_diffuse) { color_offset = offset; offset += 4; }
    tex_offset = offset;

    GLsizei stride = m_current_stride;

    if ((has_xyz || has_xyzrhw) && g_position_location != -1) {
        glEnableVertexAttribArray(g_position_location);
        glVertexAttribPointer(g_position_location, has_xyzrhw ? 4 : 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset);
    }

    // Normal attribute for FFP lighting
    if (has_normal && g_normal_location != -1) {
        glEnableVertexAttribArray(g_normal_location);
        glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset);
    } else if (g_normal_location != -1) {
        glDisableVertexAttribArray(g_normal_location);
        glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f);
    }

    // D3D9 FFP Lighting
    bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
    if (lighting_enabled) {
        glUniform1i(g_lighting_enabled_uniform, 1);
        SetFFPLightingUniforms(&m_world, m_render_states, has_diffuse);

        if (has_diffuse && g_color_location != -1) {   // the vertex colour may be the material (D3DMCS_COLOR1)
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset);
        } else {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    } else {
        glUniform1i(g_lighting_enabled_uniform, 0);
        // Always set world matrix for clip plane (v_position_world) even without lighting
        gl_set_matrix_uniform(g_world_uniform, &m_world);

        if (has_diffuse && g_color_location != -1) {
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset);
        } else if (g_color_location != -1) {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    if (tex_count > 0 && g_texcoord_location != -1) {
        glEnableVertexAttribArray(g_texcoord_location);
        glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset);
    } else if (g_texcoord_location != -1) {
        glDisableVertexAttribArray(g_texcoord_location);
        glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f);
    }

    // Texcoord1 - explicitly disable to avoid stale state
    if (g_texcoord1_location != -1) {
        glDisableVertexAttribArray(g_texcoord1_location);
        glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f);
    }

    // Texture setup
    if (g_texture_stages[0] != 0 && tex_count > 0) {
        glActiveTexture(GL_TEXTURE0);
        if (g_cached_texture != g_texture_stages[0]) {
            glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]);
            g_cached_texture = g_texture_stages[0];
        }
        ApplySamplerStates(0);
        glUniform1i(g_texture_uniform, 0);
        glUniform1i(g_use_texture_uniform, 1);
    } else {
        glUniform1i(g_use_texture_uniform, 0);
    }

    // Alpha test
    glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0);
    glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref);

    // Disable lightmap
    glUniform1i(g_use_lightmap_uniform, 0);

    GLenum type = GL_TRIANGLES;
    UINT vcount = 0;
    if (PrimitiveType == D3DPT_TRIANGLELIST) { type = GL_TRIANGLES; vcount = PrimitiveCount * 3; }
    else if (PrimitiveType == D3DPT_TRIANGLESTRIP) { type = GL_TRIANGLE_STRIP; vcount = PrimitiveCount + 2; }
    else if (PrimitiveType == D3DPT_TRIANGLEFAN) { type = GL_TRIANGLE_FAN; vcount = PrimitiveCount + 2; }

    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    glDrawArrays(type, StartVertex, vcount);

    return D3D_OK;
}

HRESULT D3D9Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,
                                        INT BaseVertexIndex,
                                        UINT MinVertexIndex,
                                        UINT NumVertices,
                                        UINT startIndex,
                                        UINT primCount) {
    if (g_shader_program == 0) init_shaders();
    use_draw_program(); trace_draw("DIP", (unsigned)primCount);

    // FBO Y-flip and clip plane uniforms
    glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
    glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
    if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

    if (m_current_vertex_buffer == 0 || m_current_index_buffer == 0) return -1;

    // Calculate MVP
    D3DMATRIX view_model, mvp;
    if (fvf_rhw(g_current_fvf)) {
        float sw = (m_current_rt) ? (float)m_current_rt->m_width : (float)g_logical_width;
        float sh = (m_current_rt) ? (float)m_current_rt->m_height : (float)g_logical_height;
        
        memset(&mvp, 0, sizeof(D3DMATRIX));
        mvp.m[0][0] = 2.0f / sw;
        mvp.m[1][1] = -2.0f / sh;
        mvp.m[2][2] = 1.0f; 
        mvp.m[3][0] = -1.0f;
        mvp.m[3][1] = 1.0f;
        mvp.m[3][3] = 1.0f;
        
        // 2D UI states
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
    } else {
        matrix_multiply(&view_model, &m_world, &m_view);
        matrix_multiply(&mvp, &view_model, &m_projection);
    }

    // Winding detection: D3D9 auto-flips culling for mirrored transforms; we emulate it
    if (!(fvf_rhw(g_current_fvf))) {
        bool mirror = (det3x3(view_model) < 0.0f);
        glFrontFace((mirror != m_fbo_rendering) ? GL_CCW : GL_CW);
    } else {
        glFrontFace(GL_CW);
    }

    gl_set_matrix_uniform(g_mvp_uniform, &mvp);
    {   // D3DRS_VERTEXBLEND with an XYZB1 position: the shader mixes world 0 and world 1 by the weight
        bool blend = fvf_blend_weights(g_current_fvf) > 0 && m_render_states[D3DRS_VERTEXBLEND] != 0;
        if (g_blend_uniform != -1) glUniform1i(g_blend_uniform, blend ? 1 : 0);
        if (blend) {
            D3DMATRIX vm1, mvp1;
            matrix_multiply(&vm1, &m_world1, &m_view);
            matrix_multiply(&mvp1, &vm1, &m_projection);
            gl_set_matrix_uniform(g_mvp1_uniform, &mvp1);
        }
    }

    // Set texture factor
    if (g_texture_factor_uniform != -1) {
        float r = ((m_texture_factor >> 16) & 0xFF) / 255.0f;
        float g = ((m_texture_factor >> 8) & 0xFF) / 255.0f;
        float b = (m_texture_factor & 0xFF) / 255.0f;
        float a = ((m_texture_factor >> 24) & 0xFF) / 255.0f;
        glUniform4f(g_texture_factor_uniform, r, g, b, a);
    }

    // Set mipmap LOD bias (D3D9 stores float as DWORD via type-punning).
    // WebGL/OpenGL's LOD calculation uses a "max" rule that tends to select
    // lower-res mip levels compared to D3D9 on the same hardware. An additional
    // -0.75 offset compensates for this difference.
    if (g_lod_bias_uniform != -1) {
        DWORD bias_bits = g_sampler_states[0][D3DSAMP_MIPMAPLODBIAS];
        float lod_bias;
        memcpy(&lod_bias, &bias_bits, sizeof(float));
        lod_bias -= 0.75f;
        glUniform1f(g_lod_bias_uniform, lod_bias);
    }

    // Set Stage 0 texture stage states
    if (g_stage0_colorop_uniform != -1) {
        glUniform1i(g_stage0_colorop_uniform, g_texture_stage_states[0][D3DTSS_COLOROP]);
    }
    if (g_stage0_colorarg1_uniform != -1) {
        glUniform1i(g_stage0_colorarg1_uniform, g_texture_stage_states[0][D3DTSS_COLORARG1]);
    }
    if (g_stage0_colorarg2_uniform != -1) {
        glUniform1i(g_stage0_colorarg2_uniform, g_texture_stage_states[0][D3DTSS_COLORARG2]);
    }
    if (g_stage0_alphaop_uniform != -1) {
        glUniform1i(g_stage0_alphaop_uniform, g_texture_stage_states[0][D3DTSS_ALPHAOP]);
    }
    if (g_stage0_alphaarg1_uniform != -1) {
        glUniform1i(g_stage0_alphaarg1_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG1]);
    }
    if (g_stage0_alphaarg2_uniform != -1) {
        glUniform1i(g_stage0_alphaarg2_uniform, g_texture_stage_states[0][D3DTSS_ALPHAARG2]);
    }

    // Fog uniforms
    {
        bool apply_fog = m_fog_enabled && !(fvf_rhw(g_current_fvf));
        glUniform1i(g_fog_enabled_uniform, apply_fog ? 1 : 0);
        if (apply_fog) {
            float r = ((m_fog_color >> 16) & 0xFF) / 255.0f;
            float g_c = ((m_fog_color >> 8) & 0xFF) / 255.0f;
            float b = (m_fog_color & 0xFF) / 255.0f;
            glUniform3f(g_fog_color_uniform, r, g_c, b);
            glUniform1f(g_fog_start_uniform, m_fog_start);
            glUniform1f(g_fog_end_uniform, m_fog_end);
            gl_set_matrix_uniform(g_view_model_uniform, &view_model);
        }
    }

    // Bind buffers
    glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_current_index_buffer);
    
    // Dynamic FVF Parsing
    bool has_xyz = fvf_xyz(g_current_fvf);
    bool has_xyzrhw = (fvf_rhw(g_current_fvf)) != 0;
    bool has_normal = (g_current_fvf & D3DFVF_NORMAL) != 0;
    bool has_diffuse = (g_current_fvf & D3DFVF_DIFFUSE) != 0;
    int tex_count = (g_current_fvf >> 8) & 0xF;

    GLsizei stride = 0;
    size_t position_offset = 0;
    size_t normal_offset = 0;
    size_t color_offset = 0;
    size_t tex_offset = 0;

    // XYZ and XYZRHW are mutually exclusive in D3D9 FVF
    if (has_xyzrhw) { position_offset = stride; stride += 16; }
    else if (has_xyz) { position_offset = stride; stride += 12 + 4 * fvf_blend_weights(g_current_fvf); }
    if (has_normal) { normal_offset = stride; stride += 12; }
    if (has_diffuse) { color_offset = stride; stride += 4; }
    tex_offset = stride;
    for (int i = 0; i < tex_count; ++i) stride += fvf_tex_components(g_current_fvf, i) * 4;

    // Use stored stride if valid
    if (m_current_stride > 0) stride = m_current_stride;

    // BaseVertexIndex support
    size_t base_offset = BaseVertexIndex * stride;

    // Enable/Set Pointers with safety guards
    if ((has_xyz || has_xyzrhw) && g_position_location != -1) {
        glEnableVertexAttribArray(g_position_location);
        GLint pos_component_count = has_xyzrhw ? 4 : 3;
        glVertexAttribPointer(g_position_location, pos_component_count, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + position_offset));
    }

    // D3D9 FFP Lighting
    bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
    if (lighting_enabled) {
        // Normal attribute
        if (g_normal_location != -1) {
            glEnableVertexAttribArray(g_normal_location);
            glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + normal_offset));
        }

        glUniform1i(g_lighting_enabled_uniform, 1);
        SetFFPLightingUniforms(&m_world, m_render_states, has_diffuse);

        if (has_diffuse && g_color_location != -1) {   // the vertex colour may be the material (D3DMCS_COLOR1)
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(base_offset + color_offset));
        } else {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    } else {
        glUniform1i(g_lighting_enabled_uniform, 0);
        // Always set world matrix for clip plane (v_position_world) even without lighting
        gl_set_matrix_uniform(g_world_uniform, &m_world);
        if (g_normal_location != -1) {
            glDisableVertexAttribArray(g_normal_location);
        }

        if (has_diffuse && g_color_location != -1) {
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(base_offset + color_offset));
        } else if (g_color_location != -1) {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    if (tex_count > 0 && g_texcoord_location != -1) {
        glEnableVertexAttribArray(g_texcoord_location);
        glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + tex_offset));
    } else if (g_texcoord_location != -1) {
        glDisableVertexAttribArray(g_texcoord_location);
        glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f);
    }

    if (tex_count > 1 && g_texcoord1_location != -1) {
        glEnableVertexAttribArray(g_texcoord1_location);
        size_t tex1_offset = tex_offset + (size_t)fvf_tex_components(g_current_fvf, 0) * 4;
        glVertexAttribPointer(g_texcoord1_location, fvf_tex_components(g_current_fvf, 1), GL_FLOAT, GL_FALSE, stride, (void*)(base_offset + tex1_offset));
    } else if (g_texcoord1_location != -1) {
        glDisableVertexAttribArray(g_texcoord1_location);
        glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f);
    }
    
    // Texture setup
    if (g_texture_stages[0] != 0 && tex_count > 0) {
        glActiveTexture(GL_TEXTURE0);
        
        // Optimization: Avoid redundant texture binding
        if (g_last_bound_texture_id != g_texture_stages[0]) {
            glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]);
            g_last_bound_texture_id = g_texture_stages[0];
        }
        
        // Apply cached sampler states
        ApplySamplerStates(0);

        glUniform1i(g_texture_uniform, 0);
        glUniform1i(g_use_texture_uniform, 1);

        // Texture transform
        if (g_texture_stage_states[0][D3DTSS_TEXTURETRANSFORMFLAGS] != D3DTTFF_DISABLE) {
            // D3D row-major matrices. Shader does u_tex_matrix * vec4(uv, 0, 1).
            // We need to pass the matrix correctly.
            glUniformMatrix4fv(g_tex_matrix_uniform, 1, GL_FALSE, &g_texture_matrices[0].m[0][0]);
            glUniform1i(g_use_tex_matrix_uniform, 1);
        } else {
            glUniform1i(g_use_tex_matrix_uniform, 0);
        }
    } else {
        glUniform1i(g_use_texture_uniform, 0);
        glUniform1i(g_use_tex_matrix_uniform, 0);
    }

    // Lightmap setup (Stage 1) - requires at least 2 texture coordinate sets
    if (g_texture_stages[1] != 0 && tex_count > 1) {
        glActiveTexture(GL_TEXTURE1);
        if (g_last_bound_lightmap_id != g_texture_stages[1]) {
            glBindTexture(GL_TEXTURE_2D, g_texture_stages[1]);
            g_last_bound_lightmap_id = g_texture_stages[1];
        }

        // Apply sampler states for Stage 1 (reuse shared logic for mipmap awareness)
        ApplySamplerStates(1);

        glUniform1i(g_texture_lm_uniform, 1);
        glUniform1i(g_use_lightmap_uniform, 1);

        // Apply texture stage operation mode
        DWORD op = g_texture_stage_states[1][D3DTSS_COLOROP];
        glUniform1i(g_stage1_op_uniform, op);

        // Restore active texture unit to TEXTURE0 so subsequent draw calls
        // (e.g., DrawIndexedPrimitiveUP/DrawPrimitiveUP) don't accidentally
        // operate on GL_TEXTURE1 when they assume TEXTURE0 is active.
        glActiveTexture(GL_TEXTURE0);
    } else {
        glUniform1i(g_use_lightmap_uniform, 0);
    }

    glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0);
    glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref);

    // Draw
    GLenum gl_primitive_type = GL_TRIANGLES;
    UINT index_count = 0;
    switch (PrimitiveType) {
        case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; index_count = primCount * 3; break;
        case D3DPT_TRIANGLESTRIP: gl_primitive_type = GL_TRIANGLE_STRIP; index_count = primCount + 2; break;
        case D3DPT_TRIANGLEFAN: gl_primitive_type = GL_TRIANGLE_FAN; index_count = primCount + 2; break;
        case D3DPT_LINELIST: gl_primitive_type = GL_LINES; index_count = primCount * 2; break;
        case D3DPT_LINESTRIP: gl_primitive_type = GL_LINE_STRIP; index_count = primCount + 1; break;
        case D3DPT_POINTLIST: gl_primitive_type = GL_POINTS; index_count = primCount; break;
    }

    // Offset in index buffer (respects 16-bit vs 32-bit index format)
    bool is_index32 = (m_current_ib_format == D3DFMT_INDEX32);
    GLenum gl_index_type = is_index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    size_t index_stride = is_index32 ? 4 : 2;
    void* indices = (void*)(uintptr_t)(startIndex * index_stride);

    if (fvf_blend_weights(g_current_fvf) > 0 && getenv("D3D9W_TRACE_BLEND")) {   // skinned-draw state, a few times
        static int n;
        if (n++ < 6)
            printf("trace-blend: fvf 0x%X stride %d light %u vblend %u mat d(%.2f %.2f %.2f %.2f) a(%.2f %.2f %.2f) "
                   "L0 on %d type %d d(%.2f %.2f %.2f) dir(%.2f %.2f %.2f) amb 0x%08X op %u a1 %u a2 %u aop %u tex %u tc %d "
                   "s1op %u colorvertex %u dms %u\n",
                   (unsigned)g_current_fvf, (int)stride, (unsigned)m_render_states[D3DRS_LIGHTING],
                   (unsigned)m_render_states[D3DRS_VERTEXBLEND], g_material.Diffuse.r, g_material.Diffuse.g,
                   g_material.Diffuse.b, g_material.Diffuse.a, g_material.Ambient.r, g_material.Ambient.g,
                   g_material.Ambient.b, (int)g_light_enabled[0], (int)g_lights[0].Type, g_lights[0].Diffuse.r,
                   g_lights[0].Diffuse.g, g_lights[0].Diffuse.b, g_lights[0].Direction.x, g_lights[0].Direction.y,
                   g_lights[0].Direction.z, (unsigned)m_render_states[D3DRS_AMBIENT],
                   (unsigned)g_texture_stage_states[0][D3DTSS_COLOROP], (unsigned)g_texture_stage_states[0][D3DTSS_COLORARG1],
                   (unsigned)g_texture_stage_states[0][D3DTSS_COLORARG2], (unsigned)g_texture_stage_states[0][D3DTSS_ALPHAOP],
                   (unsigned)g_texture_stages[0], (int)((g_current_fvf >> 8) & 0xF),
                   (unsigned)g_texture_stage_states[1][D3DTSS_COLOROP], (unsigned)m_render_states[141],
                   (unsigned)m_render_states[145]);
    }
    ff_blend_attrib(base_offset, stride);
    progvs_attribs(base_offset, stride, m_fbo_rendering, BaseVertexIndex);
    glDrawElements(gl_primitive_type, index_count, gl_index_type, indices);

    glDisableVertexAttribArray(g_position_location);
    glDisableVertexAttribArray(g_color_location);
    glDisableVertexAttribArray(g_texcoord_location);
    glDisableVertexAttribArray(g_texcoord1_location);
    if (g_normal_location != -1) glDisableVertexAttribArray(g_normal_location);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Restore GL states if XYZRHW path overrode them
    if (fvf_rhw(g_current_fvf)) {
        if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
        if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
        if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
        // Restore blend state from tracked render states
        if (!m_render_states[D3DRS_ALPHABLENDENABLE]) glDisable(GL_BLEND);
        else { glEnable(GL_BLEND); glBlendFunc(m_src_blend, m_dst_blend); }
    }

    return D3D_OK;
}
