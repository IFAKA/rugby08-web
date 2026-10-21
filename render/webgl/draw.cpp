// draw.cpp -- the four D3D draw calls. The shared steps are in draw_setup.cpp; what differs between
// them (buffer source, pre-transformed screen size, the 2D UI state overrides) is here.
#include "internal.h"
#include "device.h"

// From the vertex buffer on stream 0 (cloth, some effects).
HRESULT D3D9Device::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    if (rq_game()) rq_consts();
    RQ_POST(DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount));
    CostScope cost(COST_DRAW);
    if (rec_draw(21, PrimitiveType << 24 | StartVertex, PrimitiveCount,
                 [=](D3D9Device *d) { d->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount); }))
        return D3D_OK;
    if (m_current_vertex_buffer == 0) return -1;
    draw_begin("DP", (unsigned)PrimitiveCount);
    float w, h;
    viewport_size(&w, &h);
    draw_uniforms(w, h);
    glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);
    FvfLayout l = fvf_layout(g_current_fvf, true);
    GLsizei stride = m_current_stride;
    draw_attribs(l, stride, 0, false);
    draw_textures(l.tex_count, false, LM_OFF);
    GLenum mode;
    UINT n;
    gl_primitive(PrimitiveType, PrimitiveCount, PRIM_TRIANGLES, &mode, &n);
    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    glDrawArrays(mode, StartVertex, n);
    return D3D_OK;
}

// From the vertex and index buffers (the 3D world: pitch, stadium, players).
HRESULT D3D9Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex,
                                         UINT NumVertices, UINT startIndex, UINT primCount) {
    if (rq_game()) rq_consts();
    RQ_POST(DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount));
    CostScope cost(COST_DRAW);
    if (rec_draw(22, PrimitiveType << 24 | startIndex, primCount ^ BaseVertexIndex << 16, [=](D3D9Device *d) {
            d->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
        }))
        return D3D_OK;
    draw_begin("DIP", (unsigned)primCount);
    if (m_current_vertex_buffer == 0 || m_current_index_buffer == 0) return -1;
    bool rhw = fvf_rhw(g_current_fvf);
    if (rhw) {   // 2D UI: blended, no depth, no culling (restored below)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
    }
    float w, h;
    target_size(&w, &h);
    draw_uniforms(w, h);
    glBindBuffer(GL_ARRAY_BUFFER, m_current_vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_current_index_buffer);

    FvfLayout l = fvf_layout(g_current_fvf, false);
    GLsizei stride = m_current_stride > 0 ? (GLsizei)m_current_stride : l.stride;
    size_t base = BaseVertexIndex * stride;
    draw_attribs(l, stride, base, true);
    draw_textures(l.tex_count, false, LM_BIND_SAMPLED);

    GLenum mode;
    UINT n;
    gl_primitive(PrimitiveType, primCount, PRIM_ALL, &mode, &n);
    bool index32 = m_current_ib_format == D3DFMT_INDEX32;
    void *indices = (void *)(uintptr_t)(startIndex * (index32 ? 4 : 2));
    trace_blend(stride);
    ff_blend_attrib(base, stride);
    progvs_attribs(base, stride, m_fbo_rendering, BaseVertexIndex);
    glDrawElements(mode, n, index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, indices);

    glDisableVertexAttribArray(g_position_location);
    glDisableVertexAttribArray(g_color_location);
    glDisableVertexAttribArray(g_texcoord_location);
    glDisableVertexAttribArray(g_texcoord1_location);
    if (g_normal_location != -1) glDisableVertexAttribArray(g_normal_location);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (rhw) {   // back to the tracked depth, culling and blend states
        if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
        if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
        if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
        if (!m_render_states[D3DRS_ALPHABLENDENABLE]) glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(m_src_blend, m_dst_blend);
        }
    }
    return D3D_OK;
}

// From a guest pointer (menus, HUD, text, particles).
HRESULT D3D9Device::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                    const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    if (rq_game()) {
        rq_consts();
        auto v = std::make_shared<std::vector<uint8_t>>((const uint8_t *)pVertexStreamZeroData,
                                                        (const uint8_t *)pVertexStreamZeroData +
                                                            prim_vertices(PrimitiveType, PrimitiveCount) *
                                                                VertexStreamZeroStride);
        rq_post([=] { DrawPrimitiveUP(PrimitiveType, PrimitiveCount, v->data(), VertexStreamZeroStride); });
        return D3D_OK;
    }
    CostScope cost(COST_DRAW);
    if (rec_draw(23, PrimitiveType, PrimitiveCount,
                 [=, v = std::vector<uint8_t>((const uint8_t *)pVertexStreamZeroData,
                                              (const uint8_t *)pVertexStreamZeroData +
                                                  prim_vertices(PrimitiveType, PrimitiveCount) *
                                                      VertexStreamZeroStride)](D3D9Device *d) {
                     d->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, v.data(), VertexStreamZeroStride);
                 }))
        return D3D_OK;
    if (!pVertexStreamZeroData) return -1;
    draw_begin("DPUP", (unsigned)PrimitiveCount);
    EnsureUPBuffers();
    glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    FvfLayout l = fvf_layout(g_current_fvf, false);
    GLsizei stride = VertexStreamZeroStride > 0 ? (GLsizei)VertexStreamZeroStride : l.stride;

    // 2D UI on the back buffer: the whole canvas, scissored to the viewport, no depth, no culling
    // (restored below from the cached viewport/scissor and the tracked render states)
    bool ui = fvf_rhw(g_current_fvf) && m_current_rt && m_current_rt->m_is_backbuffer;
    GLint saved_viewport[4], saved_scissor_box[4];
    GLboolean saved_scissor_test = g_cached_scissor_test;
    if (ui) {
        memcpy(saved_viewport, g_cached_viewport, sizeof saved_viewport);
        memcpy(saved_scissor_box, g_cached_scissor_box, sizeof saved_scissor_box);
        glViewport(0, 0, g_logical_width, g_logical_height);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_viewport.X, g_logical_height - (g_viewport.Y + g_viewport.Height), g_viewport.Width,
                  g_viewport.Height);   // GL rows go bottom to top
    }
    float w, h;
    target_size(&w, &h);
    draw_uniforms(w, h);

    GLenum mode;
    UINT n;
    gl_primitive(PrimitiveType, PrimitiveCount, PRIM_ALL, &mode, &n);
    size_t size = (size_t)stride * n;
    up_upload(GL_ARRAY_BUFFER, &g_up_vbo_size, size, up_vertices(pVertexStreamZeroData, size, n, stride, l));
    draw_attribs(l, stride, 0, true);
    draw_textures(l.tex_count, true, LM_OFF);
    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    glDrawArrays(mode, 0, n);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (ui) {
        glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
        if (saved_scissor_test)
            glScissor(saved_scissor_box[0], saved_scissor_box[1], saved_scissor_box[2], saved_scissor_box[3]);
        else glDisable(GL_SCISSOR_TEST);
        if (m_render_states[D3DRS_ZENABLE]) glEnable(GL_DEPTH_TEST);
        if (m_render_states[D3DRS_ZWRITEENABLE]) glDepthMask(GL_TRUE);
        if (m_render_states[D3DRS_CULLMODE] != D3DCULL_NONE) glEnable(GL_CULL_FACE);
    }
    return D3D_OK;
}

// From guest vertex and index pointers (skinned crowd, flags, some UI).
HRESULT D3D9Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                           UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                           const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    if (rq_game()) {
        rq_consts();
        auto v = std::make_shared<std::vector<uint8_t>>((const uint8_t *)pVertexStreamZeroData,
                                                        (const uint8_t *)pVertexStreamZeroData +
                                                            (MinVertexIndex + NumVertices) * VertexStreamZeroStride);
        auto ix = std::make_shared<std::vector<uint8_t>>(
            (const uint8_t *)pIndexData, (const uint8_t *)pIndexData + prim_vertices(PrimitiveType, PrimitiveCount) *
                                                                           (IndexDataFormat == D3DFMT_INDEX32 ? 4 : 2));
        rq_post([=] {
            DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, ix->data(),
                                   IndexDataFormat, v->data(), VertexStreamZeroStride);
        });
        return D3D_OK;
    }
    CostScope cost(COST_DRAW);
    if (rec_draw(24, PrimitiveType << 24 | NumVertices, PrimitiveCount,
                 [=,
                  v = std::vector<uint8_t>((const uint8_t *)pVertexStreamZeroData,
                                           (const uint8_t *)pVertexStreamZeroData +
                                               (MinVertexIndex + NumVertices) * VertexStreamZeroStride),
                  ix = std::vector<uint8_t>((const uint8_t *)pIndexData,
                                            (const uint8_t *)pIndexData +
                                                prim_vertices(PrimitiveType, PrimitiveCount) *
                                                    (IndexDataFormat == D3DFMT_INDEX32 ? 4 : 2))](D3D9Device *d) {
                     d->DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, ix.data(),
                                               IndexDataFormat, v.data(), VertexStreamZeroStride);
                 }))
        return D3D_OK;
    if (!pVertexStreamZeroData || !pIndexData) return -1;
    draw_begin("DIPUP", (unsigned)PrimitiveCount);
    EnsureUPBuffers();
    glBindBuffer(GL_ARRAY_BUFFER, g_up_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_up_ibo);
    float w, h;
    viewport_size(&w, &h);
    draw_uniforms(w, h);

    FvfLayout l = fvf_layout(g_current_fvf, false);
    GLsizei stride = VertexStreamZeroStride > 0 ? (GLsizei)VertexStreamZeroStride : l.stride;
    size_t vsize = (size_t)stride * NumVertices;   // vertices 0..NumVertices-1 of the guest array
    up_upload(GL_ARRAY_BUFFER, &g_up_vbo_size, vsize,
              up_vertices(pVertexStreamZeroData, vsize, NumVertices, stride, l));
    GLenum mode;
    UINT n;
    gl_primitive(PrimitiveType, PrimitiveCount, PRIM_INDEXED_UP, &mode, &n);
    bool index16 = IndexDataFormat == D3DFMT_INDEX16;
    up_upload(GL_ELEMENT_ARRAY_BUFFER, &g_up_ibo_size, (size_t)n * (index16 ? 2 : 4), pIndexData);

    draw_attribs(l, stride, 0, true);
    draw_textures(l.tex_count, true, LM_BIND);
    ff_blend_attrib(0, stride);
    progvs_attribs(0, stride, m_fbo_rendering, 0);
    glDrawElements(mode, n, index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return D3D_OK;
}
