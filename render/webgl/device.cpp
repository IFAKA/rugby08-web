// device.cpp -- device lifetime, scene, Clear, Reset and the methods with nothing to do.
#include "internal.h"
#include "device.h"

D3D9Device::D3D9Device()
    : m_current_index_buffer(0), m_current_ib_format(D3DFMT_INDEX16), m_current_vertex_buffer(0), m_current_stride(0),
      m_texture_factor(0xFFFFFFFF) {
    init_shaders();

    g_viewport.X = 0;
    g_viewport.Y = 0;
    g_viewport.Width = g_logical_width;
    g_viewport.Height = g_logical_height;
    g_viewport.MinZ = 0.0f;
    g_viewport.MaxZ = 1.0f;

    if (g_fbo == 0) glGenFramebuffers(1, &g_fbo);

    // Backbuffer surface matches the app's backbuffer resolution
    m_backbuffer_surface = new D3D9Surface(g_logical_width, g_logical_height, 0, false, true);
    m_current_rt = m_backbuffer_surface;
    m_current_ds = nullptr; // Default DS is implicit in WebGL context

    matrix_identity(&m_world);
    matrix_identity(&m_world1);
    matrix_identity(&m_view);
    matrix_identity(&m_projection);
    reset_sampler_states();
    for (int i = 0; i < 8; i++) {   // D3D9's texture stage defaults
        g_texture_stage_states[i][D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        g_texture_stage_states[i][D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        g_texture_stage_states[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        g_texture_stage_states[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        g_texture_stage_states[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        g_texture_stage_states[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
    }

    // Initialize state
    m_alpha_ref = 0;
    m_alpha_test_enabled = false;
    m_src_blend = GL_ONE;
    m_dst_blend = GL_ZERO;

    // Initialize render states with D3D9 defaults
    memset(m_render_states, 0, sizeof(m_render_states));
    m_render_states[D3DRS_LIGHTING] = TRUE;             // D3D9 default: lighting enabled
    m_render_states[141] = TRUE;      // D3DRS_COLORVERTEX
    m_render_states[145] = 1;         // D3DRS_DIFFUSEMATERIALSOURCE = D3DMCS_COLOR1
    m_render_states[146] = 2;         // D3DRS_SPECULARMATERIALSOURCE = D3DMCS_COLOR2
    m_render_states[D3DRS_ZWRITEENABLE] = TRUE;             // D3D9 default: depth write enabled
    m_render_states[D3DRS_COLORWRITEENABLE] = 0x0000000F;       // D3D9 default: all channels (R|G|B|A)
    m_render_states[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL; // D3D9 default: LESSEQUAL (not GL's LESS)
    m_render_states[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    m_render_states[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    m_render_states[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    m_render_states[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    m_render_states[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    m_render_states[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;

    // Initial viewport call
    SetViewport(&g_viewport);

    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL); // D3D9 default ZFUNC is LESSEQUAL, not LESS
    glDepthMask(GL_TRUE);

    // [WASM FIX] D3D9 uses CW winding for front faces (left-handed)
    // OpenGL uses CCW by default (right-handed), so we flip it
    glFrontFace(GL_CW);

    // Disable culling initially (will be set via SetRenderState)
    glDisable(GL_CULL_FACE);
}

D3D9Device::~D3D9Device() {
    // Release owned backbuffer surface
    if (m_backbuffer_surface) {
        m_backbuffer_surface->Release();
        m_backbuffer_surface = nullptr;
    }
    m_current_rt = nullptr;
    m_current_ds = nullptr;

    // Clean up GL resources owned by this device
    if (g_fbo) {
        glDeleteFramebuffers(1, &g_fbo);
        g_fbo = 0;
    }
    if (g_up_vbo) {
        glDeleteBuffers(1, &g_up_vbo);
        g_up_vbo = 0;
        g_up_vbo_size = 0;
    }
    if (g_up_ibo) {
        glDeleteBuffers(1, &g_up_ibo);
        g_up_ibo = 0;
        g_up_ibo_size = 0;
    }
    if (g_shader_program) {
        glDeleteProgram(g_shader_program);
        g_shader_program = 0;
    }
}

ULONG D3D9Device::AddRef() {
    return 1; // Stub: no ref counting for device
}

ULONG D3D9Device::Release() {
    RQ_CALL(ULONG, Release());
    delete this;
    return 0;
}

HRESULT D3D9Device::BeginScene() { return D3D_OK; }

HRESULT D3D9Device::EndScene() { return D3D_OK; }

HRESULT D3D9Device::Present(const void *pSourceRect, const void *pDestRect, HWND hDestWindowOverride,
                            const void *pDirtyRegion) {
    RQ_POST(Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion));
    interp_present();
    return D3D_OK;
}

HRESULT D3D9Device::Clear(DWORD Count, const void *pRects, DWORD Flags, D3DCOLOR Color, FLOAT Z, DWORD Stencil) {
    if (rq_game()) {
        auto r = std::make_shared<std::vector<D3DRECT>>();
        if (pRects) r->assign((const D3DRECT *)pRects, (const D3DRECT *)pRects + Count);
        rq_post([=] { Clear(Count, pRects ? r->data() : nullptr, Flags, Color, Z, Stencil); });
        return D3D_OK;
    }
    if (rec_draw(
            20, Flags, Color,
            [=, r = std::vector<uint8_t>((const uint8_t *)pRects, (const uint8_t *)pRects + (pRects ? Count * 16 : 0))](
                D3D9Device *d) { d->Clear(Count, pRects ? r.data() : nullptr, Flags, Color, Z, Stencil); }))
        return D3D_OK;
    float r = ((Color >> 16) & 0xFF) / 255.0f;
    float g = ((Color >> 8) & 0xFF) / 255.0f;
    float b = (Color & 0xFF) / 255.0f;
    float a = ((Color >> 24) & 0xFF) / 255.0f;

    glClearColor(r, g, b, a);
    glClearDepthf(Z);
    glClearStencil(Stencil);

    GLbitfield mask = 0;
    if (Flags & D3DCLEAR_TARGET) mask |= GL_COLOR_BUFFER_BIT;
    if (Flags & D3DCLEAR_ZBUFFER) mask |= GL_DEPTH_BUFFER_BIT;
    if (Flags & D3DCLEAR_STENCIL) mask |= GL_STENCIL_BUFFER_BIT;

    // Save current mask states from tracked render states (no GPU readback)
    GLboolean color_write_enabled_r =
        (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_RED) ? GL_TRUE : GL_FALSE;
    GLboolean color_write_enabled_g =
        (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_GREEN) ? GL_TRUE : GL_FALSE;
    GLboolean color_write_enabled_b =
        (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_BLUE) ? GL_TRUE : GL_FALSE;
    GLboolean color_write_enabled_a =
        (m_render_states[D3DRS_COLORWRITEENABLE] & D3DCOLORWRITEENABLE_ALPHA) ? GL_TRUE : GL_FALSE;
    GLboolean depth_write_enabled = m_render_states[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFFFFFFFF);

    CHECK_GL_ERROR(glClear(mask));
    glStencilMask(m_render_states[D3DRS_STENCILWRITEMASK]);

    // Restore masks from tracked state (no GPU readback)
    glColorMask(color_write_enabled_r, color_write_enabled_g, color_write_enabled_b, color_write_enabled_a);
    glDepthMask(depth_write_enabled);

    return D3D_OK;
}

HRESULT D3D9Device::Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) {
    RQ_CALL(HRESULT, Reset(pPresentationParameters));
    d3d9w_interp_mode(0);
    // The app asks for a new back buffer size (a resolution change): follow it, as CreateDevice does.
    if (!pPresentationParameters) return D3DERR_INVALIDCALL;
    int w = (int)pPresentationParameters->BackBufferWidth, h = (int)pPresentationParameters->BackBufferHeight;
    if (w <= 0 || h <= 0 || (w == g_logical_width && h == g_logical_height)) return D3D_OK;
    g_logical_width = w;
    g_logical_height = h;
    plat_gl_set_size(w, h);   // the window too, so the mouse stays in the game's coordinates (factory.cpp)
    if (m_backbuffer_surface) {
        m_backbuffer_surface->m_width = (UINT)w;
        m_backbuffer_surface->m_height = (UINT)h;
    }
    m_current_rt = m_backbuffer_surface;
    g_viewport.X = 0;
    g_viewport.Y = 0;
    g_viewport.Width = (DWORD)w;
    g_viewport.Height = (DWORD)h;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    return D3D_OK;
}
