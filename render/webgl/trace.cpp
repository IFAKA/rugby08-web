// trace.cpp -- d3d9w_trace_draws: one line per draw with its state, and its pixel footprint.
#include "internal.h"
#include "device.h"

int g_trace_draws;               // d3d9w_trace_draws: print every draw's state (one frame)
std::map<GLuint, TraceTex> g_trace_texinfo;   // GL texture -> size and D3D format (trace only)
std::vector<uint8_t> g_trace_fb; // back buffer before the previous traced draw (its pixel footprint)
void trace_fb_diff(bool report) {   // what the previous draw changed in the back buffer; then re-snapshot
    int w = g_logical_width, h = g_logical_height;
    if (w <= 0 || h <= 0) return;
    std::vector<uint8_t> px((size_t)w * h * 4);
    GLint rd = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rd);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)rd);
    if (report && g_trace_fb.size() == px.size()) {
        int x0 = w, y0 = h, x1 = -1, y1 = -1; long n = 0; double r = 0, g = 0, b = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t i = ((size_t)y * w + x) * 4;
                if (!memcmp(&px[i], &g_trace_fb[i], 3)) continue;
                int sy = h - 1 - y;                       // screen rows top-down
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
                n++; r += px[i]; g += px[i + 1]; b += px[i + 2];
            }
        if (n) printf("  -> changed %ld px, box %d,%d-%d,%d, mean rgb %.0f,%.0f,%.0f\n", n, x0, y0, x1, y1, r / n, g / n, b / n);
        else printf("  -> changed nothing on screen\n");
    }
    g_trace_fb.swap(px);
}

extern "C" void d3d9w_trace_draws(int on) {
    if (g_trace_draws && !on) trace_fb_diff(true);       // the traced frame's last draw
    if (!g_trace_draws && on) g_trace_fb.clear();
    g_trace_draws = on;
}

void D3D9Device::trace_draw(const char* path, unsigned count) {
    if (!g_trace_draws) return;
    trace_fb_diff(true);   // footprint of the previous draw
    TraceTex ti0 = g_trace_texinfo.count(g_texture_stages[0]) ? g_trace_texinfo[g_texture_stages[0]] : TraceTex{0, 0, 0};
    TraceTex ti1 = g_trace_texinfo.count(g_texture_stages[1]) ? g_trace_texinfo[g_texture_stages[1]] : TraceTex{0, 0, 0};
    if (m_render_states[D3DRS_LIGHTING]) {
        int nl = 0; for (int i = 0; i < 8; ++i) nl += g_light_enabled[i];
        const D3DMATERIAL9& m = g_material;
        printf("draw light on=%d amb=%08X src=%u/%u/%u/%u cv=%u matD=%.2f,%.2f,%.2f,%.2f matA=%.2f,%.2f,%.2f matE=%.2f,%.2f,%.2f\n", nl,
               (unsigned)m_render_states[D3DRS_AMBIENT], (unsigned)m_render_states[145], (unsigned)m_render_states[146],
               (unsigned)m_render_states[147], (unsigned)m_render_states[148], (unsigned)m_render_states[141],
               m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a, m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Emissive.r, m.Emissive.g, m.Emissive.b);
        for (int i = 0; i < 8; ++i) if (g_light_enabled[i]) {
            const D3DLIGHT9& l = g_lights[i];
            printf("draw light %d type=%d D=%.2f,%.2f,%.2f A=%.2f,%.2f,%.2f dir=%.2f,%.2f,%.2f pos=%.1f,%.1f,%.1f range=%.1f att=%.3f,%.3f,%.3f\n", i, (int)l.Type,
                   l.Diffuse.r, l.Diffuse.g, l.Diffuse.b, l.Ambient.r, l.Ambient.g, l.Ambient.b, l.Direction.x, l.Direction.y, l.Direction.z,
                   l.Position.x, l.Position.y, l.Position.z, l.Range, l.Attenuation0, l.Attenuation1, l.Attenuation2);
        }
    }
    printf("draw stages %u:%X,%X/%u:%X,%X | %u:%X,%X/%u:%X,%X | %u:%X,%X/%u:%X,%X resarg=%X,%X,%X tci=%X,%X,%X\n",
           (unsigned)g_texture_stage_states[0][1], (unsigned)g_texture_stage_states[0][2], (unsigned)g_texture_stage_states[0][3],
           (unsigned)g_texture_stage_states[0][4], (unsigned)g_texture_stage_states[0][5], (unsigned)g_texture_stage_states[0][6],
           (unsigned)g_texture_stage_states[1][1], (unsigned)g_texture_stage_states[1][2], (unsigned)g_texture_stage_states[1][3],
           (unsigned)g_texture_stage_states[1][4], (unsigned)g_texture_stage_states[1][5], (unsigned)g_texture_stage_states[1][6],
           (unsigned)g_texture_stage_states[2][1], (unsigned)g_texture_stage_states[2][2], (unsigned)g_texture_stage_states[2][3],
           (unsigned)g_texture_stage_states[2][4], (unsigned)g_texture_stage_states[2][5], (unsigned)g_texture_stage_states[2][6],
           (unsigned)g_texture_stage_states[0][28], (unsigned)g_texture_stage_states[1][28], (unsigned)g_texture_stage_states[2][28],
           (unsigned)g_texture_stage_states[0][11], (unsigned)g_texture_stage_states[1][11], (unsigned)g_texture_stage_states[2][11]);
    printf("draw zfunc=%u zbias=%08X stencil=%u sfunc=%u ttff1=%u colorvertex=%u vblend=%u\n",
           (unsigned)m_render_states[23], (unsigned)m_render_states[195], (unsigned)m_render_states[52],
           (unsigned)m_render_states[56], (unsigned)g_texture_stage_states[1][24], (unsigned)m_render_states[141],
           (unsigned)m_render_states[151]);
    printf("draw stencilst ref=%u mask=%X wmask=%X fail=%u zfail=%u pass=%u\n",
           (unsigned)m_render_states[57], (unsigned)m_render_states[58], (unsigned)m_render_states[59],
           (unsigned)m_render_states[53], (unsigned)m_render_states[54], (unsigned)m_render_states[55]);
    printf("draw tex0 %ux%u fmt 0x%X tex1 %ux%u fmt 0x%X addr0 %u/%u texop2=%u tci1=0x%X fog=%u lighting=%u tfactor=%08X\n",
           ti0.w, ti0.h, ti0.fmt, ti1.w, ti1.h, ti1.fmt,
           (unsigned)g_sampler_states[0][D3DSAMP_ADDRESSU], (unsigned)g_sampler_states[0][D3DSAMP_ADDRESSV],
           (unsigned)g_texture_stage_states[2][D3DTSS_COLOROP], (unsigned)g_texture_stage_states[1][11],
           (unsigned)m_render_states[28], (unsigned)m_render_states[137], (unsigned)m_render_states[60]);
    printf("draw %s n=%u fvf=0x%X vs=%s tex0=%u tex1=%u op0=%u/%u/%u aop0=%u op1=%u tci0=0x%X ttff0=%u "
           "blend=%u %u>%u z=%u/%u atest=%u ref=%u cull=%u fbo=%d\n", path, count, (unsigned)g_current_fvf,
           g_progvs_active ? "prog" : "ff", (unsigned)g_texture_stages[0], (unsigned)g_texture_stages[1],
           (unsigned)g_texture_stage_states[0][D3DTSS_COLOROP], (unsigned)g_texture_stage_states[0][D3DTSS_COLORARG1],
           (unsigned)g_texture_stage_states[0][D3DTSS_COLORARG2], (unsigned)g_texture_stage_states[0][D3DTSS_ALPHAOP],
           (unsigned)g_texture_stage_states[1][D3DTSS_COLOROP], (unsigned)g_texture_stage_states[0][11],
           (unsigned)g_texture_stage_states[0][24], (unsigned)m_render_states[27], (unsigned)m_render_states[19],
           (unsigned)m_render_states[20], (unsigned)m_render_states[7], (unsigned)m_render_states[14],
           (unsigned)m_render_states[15], (unsigned)m_render_states[24], (unsigned)m_render_states[22], (int)m_fbo_rendering);
}

// D3D9W_TRACE_BLEND=1: the state of the first skinned (XYZBn) indexed draws.
void D3D9Device::trace_blend(GLsizei stride) {
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
}
