// draw_up.cpp -- draws from guest pointers (DrawPrimitiveUP, DrawIndexedPrimitiveUP).
#include "internal.h"
#include "device.h"

HRESULT D3D9Device::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,
                                   UINT PrimitiveCount,
                                   const void* pVertexStreamZeroData,
                                   UINT VertexStreamZeroStride) {
    if (!pVertexStreamZeroData) return -1;

    if (g_shader_program == 0) init_shaders();

    // State Caching: Only call glUseProgram if program changed
    {
        use_draw_program(); trace_draw("DPUP", (unsigned)PrimitiveCount);
    }

    // FBO Y-flip and clip plane uniforms
    glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
    glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
    if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

    EnsureUPBuffers();
    CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo));
    CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)); // No indices for UP

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
    
    if (VertexStreamZeroStride > 0) stride = VertexStreamZeroStride;

    // Override Viewport for UI rendering to ensure it covers the full canvas
    GLint saved_viewport[4];
    bool override_viewport = false;
    
    // Restore state variables
    GLboolean saved_scissor_test = GL_FALSE;
    GLint saved_scissor_box[4];

    if ((fvf_rhw(g_current_fvf)) && m_current_rt && m_current_rt->m_is_backbuffer) {
        // Use cached viewport instead of glGetIntegerv (performance optimization)
        saved_viewport[0] = g_cached_viewport[0];
        saved_viewport[1] = g_cached_viewport[1];
        saved_viewport[2] = g_cached_viewport[2];
        saved_viewport[3] = g_cached_viewport[3];

        glViewport(0, 0, g_logical_width, g_logical_height);

        // Disable Culling and Depth Test for 2D UI elements to ensure visibility
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Use cached scissor state instead of glGet* (performance optimization)
        saved_scissor_test = g_cached_scissor_test;
        saved_scissor_box[0] = g_cached_scissor_box[0];
        saved_scissor_box[1] = g_cached_scissor_box[1];
        saved_scissor_box[2] = g_cached_scissor_box[2];
        saved_scissor_box[3] = g_cached_scissor_box[3];

        glEnable(GL_SCISSOR_TEST);

        // Scissor to current viewport area (WebGL Y is bottom-to-top)
        int sy = g_logical_height - (g_viewport.Y + g_viewport.Height);
        glScissor(g_viewport.X, sy, g_viewport.Width, g_viewport.Height);
        
        override_viewport = true;
    }

    // Calculate MVP
    D3DMATRIX view_model, mvp;
    if (fvf_rhw(g_current_fvf)) {
        // For XYZRHW, use the app's logical resolution
        float sw = (float)g_logical_width;
        float sh = (float)g_logical_height;
        // If rendering to texture, use texture dimensions
        if (m_current_rt && !m_current_rt->m_is_backbuffer) {
             sw = (float)m_current_rt->m_width;
             sh = (float)m_current_rt->m_height;
        }

        memset(&mvp, 0, sizeof(D3DMATRIX));
        mvp.m[0][0] = 2.0f / sw;
        mvp.m[1][1] = -2.0f / sh; 
        mvp.m[3][0] = -1.0f;
        mvp.m[3][1] = 1.0f;
        mvp.m[2][2] = 1.0f;
        mvp.m[3][3] = 1.0f;
        mvp.m[3][2] = 0.0f;
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

    const uint8_t* vertex_data = static_cast<const uint8_t*>(pVertexStreamZeroData);

    // Calculate vertex count and total size
    UINT vertex_count = 0;
     switch (PrimitiveType) {
        case D3DPT_TRIANGLELIST: vertex_count = PrimitiveCount * 3; break;
        case D3DPT_TRIANGLESTRIP: vertex_count = PrimitiveCount + 2; break;
        case D3DPT_TRIANGLEFAN: vertex_count = PrimitiveCount + 2; break;
        case D3DPT_LINELIST: vertex_count = PrimitiveCount * 2; break;
        case D3DPT_LINESTRIP: vertex_count = PrimitiveCount + 1; break;
        case D3DPT_POINTLIST: vertex_count = PrimitiveCount; break;
    }
    
    size_t total_size = stride * vertex_count;

    // Convert BGRA vertex colors to RGBA if needed
    bool converted = false;
    static std::vector<uint8_t> converted_data;
    if (has_diffuse && !g_progvs_active) {
        converted_data.resize(total_size);
        memcpy(converted_data.data(), pVertexStreamZeroData, total_size);
        
        // Swap R and B in each vertex's color (BGRA -> RGBA)
        // [WASM] Restore Red-Blue swap for vertex colors (BGRA -> RGBA)
        for (UINT i = 0; i < vertex_count; ++i) {
            uint8_t* color_ptr = converted_data.data() + (i * stride) + color_offset;
            std::swap(color_ptr[0], color_ptr[2]); 
            
            // Removed: Force Alpha to 255 if it's 0
            // This was breaking opacity functionality for UI elements (glow effects, etc.)
            // D3D9 FFP respects alpha=0 for transparency when D3DFVF_DIFFUSE is specified
        }
        vertex_data = converted_data.data();
        converted = true;
    }

    // Upload to VBO - Use persistent buffer for better performance
    if (total_size > g_up_vbo_size) {
        // Buffer too small, reallocate with 2x growth strategy
        size_t new_size = total_size * 2;
        CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
        g_up_vbo_size = new_size;
    }
    // Orphan first: the previous draw may still be reading this storage, and writing into it makes
    // Apple's GL-on-Metal driver flush the queue and wait (40% of a match frame). Fresh storage never waits.
    CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, g_up_vbo_size, nullptr, GL_DYNAMIC_DRAW));
    CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, 0, total_size, vertex_data));
    
    // Enable/Set Pointers (Use offsets!)
    if (has_xyzrhw) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 4, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
    } else if (has_xyz) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
    }

    // Normal attribute for FFP lighting
    if (has_normal && g_normal_location != -1) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_normal_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset));
    } else if (g_normal_location != -1) {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_normal_location));
        CHECK_GL_ERROR(glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f));
    }

    // D3D9 FFP Lighting: when FVF has NORMAL and D3DRS_LIGHTING is TRUE
    bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
    if (lighting_enabled) {
        CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 1));
        SetFFPLightingUniforms(&m_world, m_render_states, has_diffuse);

        if (has_diffuse && g_color_location != -1) {   // the vertex colour may be the material (D3DMCS_COLOR1)
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset);
        } else {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    } else {
        CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 0));
        // Always set world matrix for clip plane (v_position_world) even without lighting
        gl_set_matrix_uniform(g_world_uniform, &m_world);

        if (has_diffuse) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset));
        } else {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    if (tex_count > 0) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset));
    } else {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord_location));
        CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f));
    }

    if (tex_count > 1) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord1_location));
        size_t tex1_offset = tex_offset + (size_t)fvf_tex_components(g_current_fvf, 0) * 4;
        CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord1_location, fvf_tex_components(g_current_fvf, 1), GL_FLOAT, GL_FALSE, stride, (void*)tex1_offset));
    } else {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord1_location));
        CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f));
    }
    
    // Texture setup
    if (g_texture_stages[0] != 0 && tex_count > 0) {
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));

        // State Caching: Only bind texture if it changed
        if (g_cached_texture != g_texture_stages[0]) {
            CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]));
            g_cached_texture = g_texture_stages[0];
        }

        CHECK_GL_ERROR(glUniform1i(g_texture_uniform, 0));
        CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 1));

        // Apply D3D sampler states (Filter, Wrap)
        ApplySamplerStates(0);
    } else {
        // No texture case - bind texture 0
        // IMPORTANT: Must set active texture unit to TEXTURE0 before unbinding,
        // otherwise we may accidentally unbind GL_TEXTURE1 (lightmap) if that was
        // the last active unit (e.g., after DrawIndexedPrimitive's lightmap setup).
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
        if (g_cached_texture != 0) {
            CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
            g_cached_texture = 0;
        }
        CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 0));
    }

    CHECK_GL_ERROR(glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0));
    CHECK_GL_ERROR(glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref));

    // Disable lightmap to avoid stale state from DrawIndexedPrimitive
    CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 0));

    // Draw
    GLenum gl_primitive_type = GL_TRIANGLES;
    switch (PrimitiveType) {
        case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; break;
        case D3DPT_TRIANGLESTRIP: gl_primitive_type = GL_TRIANGLE_STRIP; break;
        case D3DPT_TRIANGLEFAN: gl_primitive_type = GL_TRIANGLE_FAN; break;
        case D3DPT_LINELIST: gl_primitive_type = GL_LINES; break;
        case D3DPT_LINESTRIP: gl_primitive_type = GL_LINE_STRIP; break;
        case D3DPT_POINTLIST: gl_primitive_type = GL_POINTS; break;
    }

    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    CHECK_GL_ERROR(glDrawArrays(gl_primitive_type, 0, vertex_count));

    // Cleanup pointers (unbind RB to be safe?)
    CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));

    // Restore Viewport/Scissor/DepthTest/CullFace if overridden
    if (override_viewport) {
        glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);

        if (saved_scissor_test) {
            glScissor(saved_scissor_box[0], saved_scissor_box[1], saved_scissor_box[2], saved_scissor_box[3]);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        // Restore depth test, depth mask, and cull face from tracked render states
        if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
        if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
        if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
    }

    return D3D_OK;
}

HRESULT D3D9Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                           UINT NumVertices, UINT PrimitiveCount,
                                           const void* pIndexData, D3DFORMAT IndexDataFormat,
                                           const void* pVertexStreamZeroData,
                                           UINT VertexStreamZeroStride) {
    if (!pVertexStreamZeroData || !pIndexData) return -1;

    if (g_shader_program == 0) init_shaders();
    use_draw_program(); trace_draw("DIPUP", (unsigned)PrimitiveCount);

    // FBO Y-flip and clip plane uniforms
    glUniform1i(g_fbo_flip_y_uniform, m_fbo_rendering ? 1 : 0);
    glUniform1i(g_clip_plane_enabled_uniform, m_clip_plane_enabled ? 1 : 0);
    if (m_clip_plane_enabled) glUniform4fv(g_clip_plane_uniform, 1, m_clip_planes[0]);

    EnsureUPBuffers();
    CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo));
    CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_up_ibo));

    // Respect game's blend state (set via SetRenderState before this draw call)

    // Calculate MVP
    D3DMATRIX view_model, mvp;
    if (fvf_rhw(g_current_fvf)) {
        float vw = (g_viewport.Width > 0) ? (float)g_viewport.Width : (float)g_logical_width;
        float vh = (g_viewport.Height > 0) ? (float)g_viewport.Height : (float)g_logical_height;

        memset(&mvp, 0, sizeof(D3DMATRIX));
        mvp.m[0][0] = 2.0f / vw;
        mvp.m[3][0] = -1.0f;
        mvp.m[1][1] = -2.0f / vh;
        mvp.m[3][1] = 1.0f;
        mvp.m[2][2] = 1.0f;
        mvp.m[3][3] = 1.0f;
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

    if (VertexStreamZeroStride > 0) stride = VertexStreamZeroStride;

    const uint8_t* vertex_data = static_cast<const uint8_t*>(pVertexStreamZeroData);

    // Upload Vertices
    // NumVertices isn't reliable for buffer size, but MinVertexIndex + NumVertices is max index.
    // For simplicity, we assume we need to upload consistent with PrimitiveCount, 
    // BUT indexed usage is tricky. We'll reuse NumVertices logic but be careful.
    // Actually, for direct pointer indexed drawing, we should upload the whole range used.
    // Simplest safe bet: upload everything provided... but we only have a pointer.
    // We HAVE to trust NumVertices.
    
    size_t total_v_size = stride * NumVertices;
    
    // BGRA -> RGBA conversion
    static std::vector<uint8_t> converted_v_data;
    if (has_diffuse && !g_progvs_active) {
        converted_v_data.resize(total_v_size);
        memcpy(converted_v_data.data(), pVertexStreamZeroData, total_v_size);
         for (UINT i = 0; i < NumVertices; ++i) {
            uint8_t* color_ptr = converted_v_data.data() + (i * stride) + color_offset;
            std::swap(color_ptr[0], color_ptr[2]);
            // D3D9 FFP respects alpha=0 for transparency when D3DFVF_DIFFUSE is specified.
            // Do NOT force alpha=255 here; doing so breaks opacity for UI elements and glow effects.
            // (Aligned with DrawPrimitiveUP behavior.)
        }
        vertex_data = converted_v_data.data();
    }

    // Upload Vertices - Use persistent buffer for better performance
    if (total_v_size > g_up_vbo_size) {
        // Buffer too small, reallocate with 2x growth strategy
        size_t new_size = total_v_size * 2;
        CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
        g_up_vbo_size = new_size;
    }
    // Orphan first: the previous draw may still be reading this storage, and writing into it makes
    // Apple's GL-on-Metal driver flush the queue and wait (40% of a match frame). Fresh storage never waits.
    CHECK_GL_ERROR(glBufferData(GL_ARRAY_BUFFER, g_up_vbo_size, nullptr, GL_DYNAMIC_DRAW));
    CHECK_GL_ERROR(glBufferSubData(GL_ARRAY_BUFFER, 0, total_v_size, vertex_data));

    // Upload Indices
    // PrimitiveCount triangles/lines etc.
    size_t index_size = (IndexDataFormat == D3DFMT_INDEX16) ? 2 : 4;
    size_t index_count = 0;
    switch (PrimitiveType) {
        case D3DPT_TRIANGLELIST: index_count = PrimitiveCount * 3; break;
        case D3DPT_TRIANGLESTRIP: index_count = PrimitiveCount + 2; break; // Approximated
        case D3DPT_TRIANGLEFAN: index_count = PrimitiveCount + 2; break; // Approximated
        // Lines etc
        default: index_count = PrimitiveCount * 3; // Safe bet for alloc
    }
    
    // Actually, primitives define how many indices we read.
    size_t total_i_size = index_count * index_size;

    // Upload Indices - Use persistent buffer for better performance
    if (total_i_size > g_up_ibo_size) {
        // Buffer too small, reallocate with 2x growth strategy
        size_t new_size = total_i_size * 2;
        CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER, new_size, nullptr, GL_DYNAMIC_DRAW));
        g_up_ibo_size = new_size;
    }
    // Orphan first: the previous draw may still be reading this storage, and writing into it makes
    // Apple's GL-on-Metal driver flush the queue and wait (40% of a match frame). Fresh storage never waits.
    CHECK_GL_ERROR(glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_up_ibo_size, nullptr, GL_DYNAMIC_DRAW));
    CHECK_GL_ERROR(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, total_i_size, pIndexData));

     // Enable/Set Pointers (Use offsets!)
    if (has_xyzrhw) {
        // [IMPORTANT] Pass all 4 components for XYZRHW to properly set gl_Position.w
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 4, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
    } else if (has_xyz) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_position_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_position_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)position_offset));
    }

    // Normal attribute for FFP lighting
    if (has_normal && g_normal_location != -1) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_normal_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_normal_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)normal_offset));
    } else if (g_normal_location != -1) {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_normal_location));
        CHECK_GL_ERROR(glVertexAttrib3f(g_normal_location, 0.0f, 0.0f, 1.0f));
    }

    // D3D9 FFP Lighting: when FVF has NORMAL and D3DRS_LIGHTING is TRUE
    bool lighting_enabled = has_normal && !has_xyzrhw && m_render_states[D3DRS_LIGHTING];
    if (lighting_enabled) {
        CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 1));
        SetFFPLightingUniforms(&m_world, m_render_states, has_diffuse);

        if (has_diffuse && g_color_location != -1) {   // the vertex colour may be the material (D3DMCS_COLOR1)
            glEnableVertexAttribArray(g_color_location);
            glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset);
        } else {
            glDisableVertexAttribArray(g_color_location);
            glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    } else {
        CHECK_GL_ERROR(glUniform1i(g_lighting_enabled_uniform, 0));
        // Always set world matrix for clip plane (v_position_world) even without lighting
        gl_set_matrix_uniform(g_world_uniform, &m_world);

        if (has_diffuse) {
            CHECK_GL_ERROR(glEnableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttribPointer(g_color_location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)color_offset));
        } else {
            CHECK_GL_ERROR(glDisableVertexAttribArray(g_color_location));
            CHECK_GL_ERROR(glVertexAttrib4f(g_color_location, 1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    if (tex_count > 0) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord_location));
        CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord_location, 2, GL_FLOAT, GL_FALSE, stride, (void*)tex_offset));
    } else {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord_location));
        CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord_location, 0.0f, 0.0f));
    }

    // Texcoord1 (second UV set) - must explicitly handle to avoid stale state
    if (tex_count > 1 && g_texcoord1_location != -1) {
        CHECK_GL_ERROR(glEnableVertexAttribArray(g_texcoord1_location));
        size_t tex1_offset = tex_offset + (size_t)fvf_tex_components(g_current_fvf, 0) * 4;
        CHECK_GL_ERROR(glVertexAttribPointer(g_texcoord1_location, fvf_tex_components(g_current_fvf, 1), GL_FLOAT, GL_FALSE, stride, (void*)tex1_offset));
    } else if (g_texcoord1_location != -1) {
        CHECK_GL_ERROR(glDisableVertexAttribArray(g_texcoord1_location));
        CHECK_GL_ERROR(glVertexAttrib2f(g_texcoord1_location, 0.0f, 0.0f));
    }

    // Texture setup
    if (g_texture_stages[0] != 0 && tex_count > 0) {
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));

        // State Caching: Only bind texture if it changed
        if (g_cached_texture != g_texture_stages[0]) {
            CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[0]));
            g_cached_texture = g_texture_stages[0];
        }

        CHECK_GL_ERROR(glUniform1i(g_texture_uniform, 0));
        CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 1));

        // Apply D3D sampler states (Filter, Wrap)
        ApplySamplerStates(0);
    } else {
        // No texture case - bind texture 0
        // IMPORTANT: Must set active texture unit to TEXTURE0 before unbinding,
        // otherwise we may accidentally unbind GL_TEXTURE1 (lightmap) if that was
        // the last active unit (e.g., after DrawIndexedPrimitive's lightmap setup).
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
        if (g_cached_texture != 0) {
            CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
            g_cached_texture = 0;
        }
        CHECK_GL_ERROR(glUniform1i(g_use_texture_uniform, 0));
    }

    CHECK_GL_ERROR(glUniform1i(g_alpha_test_enabled_uniform, m_alpha_test_enabled ? 1 : 0));
    CHECK_GL_ERROR(glUniform1f(g_alpha_ref_uniform, (float)m_alpha_ref));

    // Stage 1 / Lightmap setup - must explicitly set to avoid stale state from DrawIndexedPrimitive
    if (g_texture_stages[1] != 0 && tex_count > 1) {
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE1));
        CHECK_GL_ERROR(glBindTexture(GL_TEXTURE_2D, g_texture_stages[1]));
        g_last_bound_lightmap_id = g_texture_stages[1];
        CHECK_GL_ERROR(glUniform1i(g_texture_lm_uniform, 1));
        CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 1));
        DWORD op = g_texture_stage_states[1][D3DTSS_COLOROP];
        CHECK_GL_ERROR(glUniform1i(g_stage1_op_uniform, op));
        CHECK_GL_ERROR(glActiveTexture(GL_TEXTURE0));
    } else {
        CHECK_GL_ERROR(glUniform1i(g_use_lightmap_uniform, 0));
    }

    GLenum gl_primitive_type = GL_TRIANGLES;
    switch (PrimitiveType) {
        case D3DPT_TRIANGLELIST: gl_primitive_type = GL_TRIANGLES; break;
        // Others ignored for simplicity as they are rare in indexed
        default: gl_primitive_type = GL_TRIANGLES;
    }

    GLenum gl_index_type = (IndexDataFormat == D3DFMT_INDEX16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    CHECK_GL_ERROR(glDrawElements(gl_primitive_type, index_count, gl_index_type, 0));

    CHECK_GL_ERROR(glBindBuffer(GL_ARRAY_BUFFER, 0));
    CHECK_GL_ERROR(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    // Blend state is managed by game's SetRenderState calls

    return D3D_OK;
}
