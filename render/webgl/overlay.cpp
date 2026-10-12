// overlay.cpp -- d3d9w_overlay_draw: an RGBA image of the back buffer's size drawn over the finished frame
// at Present (the in-game Online screens, runtime/core/ui_online.c). One texture, uploaded only when the
// image changed, one quad over the whole back buffer, alpha-blended into the default framebuffer. Every
// piece of GL state it touches is saved and restored, so the renderer's caches (internal.h) stay true.
#include "internal.h"

static GLuint g_ov_prog, g_ov_vao, g_ov_vbo, g_ov_tex;
static GLint g_ov_u_tex = -1;
static int g_ov_w, g_ov_h;

static const char *k_ov_vs = "#version 300 es\n"
                             "layout(location = 0) in vec2 a_p;\n"
                             "out vec2 v_uv;\n"
                             "void main() { v_uv = vec2(a_p.x * 0.5 + 0.5, 0.5 - a_p.y * 0.5);"
                             " gl_Position = vec4(a_p, 0.0, 1.0); }\n";
static const char *k_ov_fs = "#version 300 es\n"
                             "precision mediump float;\n"
                             "in vec2 v_uv;\n"
                             "uniform sampler2D u_img;\n"
                             "out vec4 o_c;\n"
                             "void main() { o_c = texture(u_img, v_uv); }\n";

static bool overlay_init() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_ov_vs), fs = compile_shader(GL_FRAGMENT_SHADER, k_ov_fs);
    if (!vs || !fs) return false;
    g_ov_prog = glCreateProgram();
    glAttachShader(g_ov_prog, vs);
    glAttachShader(g_ov_prog, fs);
    glLinkProgram(g_ov_prog);
    GLint ok = 0;
    glGetProgramiv(g_ov_prog, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    g_ov_u_tex = glGetUniformLocation(g_ov_prog, "u_img");
    static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glGenVertexArrays(1, &g_ov_vao);
    glBindVertexArray(g_ov_vao);
    glGenBuffers(1, &g_ov_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_ov_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenTextures(1, &g_ov_tex);
    return true;
}

struct SavedGL {   // what the overlay changes
    GLint prog, vao, vbo, active, tex0, sampler0, fbo, viewport[4], unpack, blend_src_rgb, blend_dst_rgb, blend_src_a,
        blend_dst_a, blend_eq_rgb, blend_eq_a;
    GLboolean blend, depth, cull, scissor, stencil, mask[4];
};

static void save(SavedGL &s) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &s.prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.vbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s.active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex0);
    glGetIntegerv(GL_SAMPLER_BINDING, &s.sampler0);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
    glGetIntegerv(GL_VIEWPORT, s.viewport);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &s.unpack);
    glGetIntegerv(GL_BLEND_SRC_RGB, &s.blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &s.blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blend_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blend_dst_a);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.blend_eq_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blend_eq_a);
    s.blend = glIsEnabled(GL_BLEND);
    s.depth = glIsEnabled(GL_DEPTH_TEST);
    s.cull = glIsEnabled(GL_CULL_FACE);
    s.scissor = glIsEnabled(GL_SCISSOR_TEST);
    s.stencil = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, s.mask);
}

static void set(GLenum cap, GLboolean on) { on ? glEnable(cap) : glDisable(cap); }

static void restore(const SavedGL &s) {
    glUseProgram((GLuint)s.prog);
    glBindVertexArray((GLuint)s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s.vbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s.tex0);
    glBindSampler(0, (GLuint)s.sampler0);
    glActiveTexture((GLenum)s.active);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)s.fbo);
    glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, s.unpack);
    glBlendFuncSeparate((GLenum)s.blend_src_rgb, (GLenum)s.blend_dst_rgb, (GLenum)s.blend_src_a, (GLenum)s.blend_dst_a);
    glBlendEquationSeparate((GLenum)s.blend_eq_rgb, (GLenum)s.blend_eq_a);
    set(GL_BLEND, s.blend);
    set(GL_DEPTH_TEST, s.depth);
    set(GL_CULL_FACE, s.cull);
    set(GL_SCISSOR_TEST, s.scissor);
    set(GL_STENCIL_TEST, s.stencil);
    glColorMask(s.mask[0], s.mask[1], s.mask[2], s.mask[3]);
}

extern "C" void d3d9w_overlay_draw(const unsigned char *rgba, int w, int h, int changed) {
    static int state;   // 0 not yet, 1 ready, -1 failed
    if (!rgba || w <= 0 || h <= 0 || state < 0) return;
    SavedGL s;
    save(s);
    if (!state) state = overlay_init() ? 1 : -1;
    if (state > 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, g_logical_width ? g_logical_width : w, g_logical_height ? g_logical_height : h);
        glBindTexture(GL_TEXTURE_2D, g_ov_tex);
        glBindSampler(0, 0);
        if (changed || w != g_ov_w || h != g_ov_h) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g_ov_w = w;
            g_ov_h = h;
        }
        glUseProgram(g_ov_prog);
        glUniform1i(g_ov_u_tex, 0);
        glBindVertexArray(g_ov_vao);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    restore(s);
}
