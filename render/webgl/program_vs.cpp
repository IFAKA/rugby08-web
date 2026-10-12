// program_vs.cpp -- D3D8 programmable vertex shaders as GL programs (MojoShader GLSL).
#include "internal.h"

D3D9WProgVS *g_progvs_active = nullptr;

// fragment_shader_source as GLSL ES 3.00, reading MojoShader's outputs (io_<usage>_<index>):
// oD0 -> v_color, oT0/oT1 -> v_texcoord/v_texcoord1. No world position (clip plane) or eye depth
// from the shader: fog depth comes from the fragment's 1/w, which is view depth for a projection.
const std::string &progvs_fragment_source() {
    static std::string fs;
    if (!fs.empty()) return fs;
    fs = std::string("#version 300 es\nout highp vec4 d3d9w_frag;\n") + fragment_shader_source;
    str_replace_all(fs, "varying vec4 v_color;", "in highp vec4 io_10_0;\n#define v_color io_10_0");
    str_replace_all(fs, "varying vec2 v_texcoord;", "in highp vec4 io_5_0;\n#define v_texcoord io_5_0.xy");
    str_replace_all(fs, "varying vec2 v_texcoord1;", "in highp vec4 io_5_1;\n#define v_texcoord1 io_5_1.xy");
    str_replace_all(fs, "varying vec4 v_specular;", "in highp vec4 io_10_1;\n#define v_specular io_10_1");
    str_replace_all(fs, "varying vec4 v_position_world;", "#define v_position_world vec4(0.0)");
    str_replace_all(fs, "varying float v_eye_depth;", "#define v_eye_depth (1.0 / gl_FragCoord.w)");
    str_replace_all(fs, "gl_FragColor", "d3d9w_frag");
    str_replace_all(fs, "texture2D(", "texture(");
    return fs;
}

// MojoShader's vertex shader plus: D3DCOLOR inputs swizzled (vertex buffers hold BGRA bytes and
// v.x must be red), the outputs the fragment stage reads but this shader does not write, and the
// render-target Y flip.
std::string progvs_vertex_source(const char *glsl, const std::vector<D3D9WElem> &elems) {
    std::string s = glsl, decl = "uniform bool u_fbo_flip_y;\n", prologue;
    for (const D3D9WElem &e : elems) {
        if (e.type != 4) continue;                                    // D3DVSDT_D3DCOLOR
        std::string v = "vs_v" + std::to_string(e.reg);
        str_replace_all(s, "in vec4 " + v + ";", "in vec4 " + v + "_bgra;\nvec4 " + v + ";");
        prologue += "\t" + v + " = " + v + "_bgra.zyxw;\n";
    }
    static const char *const outs[][2] = {
        { "io_10_0", "vec4(1.0)" }, { "io_10_1", "vec4(0.0)" }, { "io_5_0", "vec4(0.0)" }, { "io_5_1", "vec4(0.0)" }
    };
    for (auto &o : outs)
        if (s.find(std::string("out highp vec4 ") + o[0] + ";") == std::string::npos) {
            decl += std::string("out highp vec4 ") + o[0] + ";\n";
            prologue += std::string("\t") + o[0] + " = " + o[1] + ";\n";
        }
    size_t m = s.find("void main()");
    size_t open = m == std::string::npos ? m : s.find('{', m);
    size_t close = s.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) return std::string();
    s.insert(close, "\tgl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n\tif (u_fbo_flip_y) gl_Position.y = "
                    "-gl_Position.y;\n");
    s.insert(open + 1, "\n" + prologue);
    s.insert(m, decl);
    return s;
}

// D3DVSDT_* -> (components, GL type, normalized)
void progvs_type(unsigned t, GLint *n, GLenum *type, GLboolean *norm) {
    static const struct {
        GLint n;
        GLenum type;
        GLboolean norm;
    } k[8] = { { 1, GL_FLOAT, GL_FALSE }, { 2, GL_FLOAT, GL_FALSE },        { 3, GL_FLOAT, GL_FALSE },
               { 4, GL_FLOAT, GL_FALSE }, { 4, GL_UNSIGNED_BYTE, GL_TRUE }, { 4, GL_UNSIGNED_BYTE, GL_FALSE },
               { 2, GL_SHORT, GL_FALSE }, { 4, GL_SHORT, GL_FALSE } };
    unsigned i = t < 8 ? t : 3;
    *n = k[i].n;
    *type = k[i].type;
    *norm = k[i].norm;
}

// Fixed-function draws: the XYZB* blend weight (right after the position) as a_blendweight.
void ff_blend_attrib(size_t base, GLsizei stride) {
    if (g_progvs_active || g_blendweight_location == -1) return;
    if (fvf_blend_weights(g_current_fvf) > 0) {
        glEnableVertexAttribArray(g_blendweight_location);
        glVertexAttribPointer(g_blendweight_location, 1, GL_FLOAT, GL_FALSE, stride, (void *)(base + 12));
    } else {
        glDisableVertexAttribArray(g_blendweight_location);
        glVertexAttrib1f(g_blendweight_location, 1.0f);
    }
}

extern "C" D3D9WProgVS *d3d9w_progvs_create(const char *glsl, const unsigned char *regs, int nregs,
                                            const D3D9WElem *elems, int nelems) {
    if (g_shader_program == 0) init_shaders();
    std::vector<D3D9WElem> ev(elems, elems + nelems);
    std::string vsrc = progvs_vertex_source(glsl, ev);
    if (vsrc.empty()) {
        printf("d3d9w_progvs_create: no main() in the vertex shader\n");
        return nullptr;
    }
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc.c_str());
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, progvs_fragment_source().c_str());
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return nullptr;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    for (int r = 0; r < 16; ++r) {                                   // v<reg> -> attribute location <reg>
        std::string n = "vs_v" + std::to_string(r);
        glBindAttribLocation(p, r, n.c_str());
        glBindAttribLocation(p, r, (n + "_bgra").c_str());
    }
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        printf("d3d9w_progvs_create: link failed: %s\n", log);
        glDeleteProgram(p);
        return nullptr;
    }
    D3D9WProgVS *o = new D3D9WProgVS;
    o->prog = p;
    o->vsrc = vsrc;
    loc_query(p, o->locs);
    o->consts = glGetUniformLocation(p, "vs_uniforms_vec4");
    o->regs.assign(regs, regs + nregs);
    o->elems = ev;
    return o;
}

extern "C" void d3d9w_progvs_use(D3D9WProgVS *p, const float (*consts)[4]) {
    g_progvs_active = p;
    g_progvs_consts = consts;
}

// Right before a draw with a programmable shader: its declaration's elements become the v<reg>
// inputs, each from its own stream (base_vertex: DrawIndexedPrimitive's BaseVertexIndex) (the fixed-function attribute setup above them used locations this program does
// not have). Culling uses the unmirrored winding: the shader, not m_world/m_view, transforms.
void progvs_attribs(size_t base, GLsizei stride, bool fbo, int base_vertex) {
    D3D9WProgVS *p = g_progvs_active;
    if (!p) return;
    GLint stream0 = 0;                   // the draw path bound stream 0's data (a VB, or its UP copy)
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &stream0);
    for (GLuint l = 0; l < 16; ++l) glDisableVertexAttribArray(l);
    for (const D3D9WElem &e : p->elems) {
        GLint n;
        GLenum type;
        GLboolean norm;
        progvs_type(e.type, &n, &type, &norm);
        if (e.stream == 0) {
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)stream0);
            glEnableVertexAttribArray(e.reg);
            glVertexAttribPointer(e.reg, n, type, norm, stride, (void *)(base + e.offset));
        } else if (e.stream < 16 && g_stream_vb[e.stream]) {
            GLsizei st = g_stream_stride[e.stream];
            glBindBuffer(GL_ARRAY_BUFFER, g_stream_vb[e.stream]);
            glEnableVertexAttribArray(e.reg);
            glVertexAttribPointer(e.reg, n, type, norm, st, (void *)((size_t)base_vertex * st + e.offset));
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)stream0);
    glFrontFace(fbo ? GL_CCW : GL_CW);
}
