// program_ps.cpp -- D3D8 pixel shaders (ps.1.x via MojoShader) linked with a vertex stage.
#include "internal.h"

// ---- pixel shaders --------------------------------------------------------------------------
D3D9WProgPS *g_progps_active = nullptr;
const float (*g_progps_consts)[4] = nullptr;

// The fixed-function vertex shader as GLSL ES 3.00 whose outputs carry MojoShader's names
// (io_10_0/1 = oD0/oD1, io_5_n = oTn), so a translated pixel shader reads them as v0/v1/tn.
std::string ff_vertex_source_es3() {
    std::string s = std::string("#version 300 es\n") + vertex_shader_source;
    str_replace_all(s, "attribute ", "in ");
    str_replace_all(s, "varying vec4 v_color;", "out highp vec4 io_10_0;\n#define v_color io_10_0");
    str_replace_all(s, "varying vec4 v_specular;", "out highp vec4 io_10_1;\n#define v_specular io_10_1");
    str_replace_all(
        s, "varying vec2 v_texcoord;",
        "out highp vec4 io_5_0;\n#define v_texcoord io_5_0.xy\nout highp vec4 io_5_2;\nout highp vec4 io_5_3;");
    str_replace_all(s, "varying vec2 v_texcoord1;", "out highp vec4 io_5_1;\n#define v_texcoord1 io_5_1.xy");
    str_replace_all(s, "varying vec4 v_position_world;", "out highp vec4 v_position_world;");
    str_replace_all(s, "varying float v_eye_depth;", "out highp float v_eye_depth;");
    size_t m = s.find("void main()");
    size_t open = m == std::string::npos ? m : s.find('{', m);
    if (open == std::string::npos) return std::string();
    s.insert(open + 1, "\n    io_5_0 = vec4(0.0, 0.0, 0.0, 1.0); io_5_1 = io_5_0; io_5_2 = io_5_0; io_5_3 = io_5_0;\n");
    return s;
}

D3D9WLinked *progps_link(D3D9WProgVS *vsp, D3D9WProgPS *psp) {
    auto it = psp->links.find(vsp);
    if (it != psp->links.end()) return it->second;
    D3D9WLinked *L = nullptr;
    std::string vsrc = vsp ? vsp->vsrc : ff_vertex_source_es3();
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc.c_str());
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, psp->fsrc.c_str());
#ifndef __EMSCRIPTEN__
    if (!vs || !fs) {   // keep the source that failed: logs/shaders/link_fail_<n>_{vs,fs}.glsl
        static int nfail;
        char path[96];
        snprintf(path, sizeof path, "logs/shaders/link_fail_%d_%s.glsl", nfail++, !vs ? "vs" : "fs");
        if (FILE *f = fopen(path, "w")) {
            fputs(!vs ? vsrc.c_str() : psp->fsrc.c_str(), f);
            fclose(f);
        }
    }
#endif
    if (vs && fs) {
        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        if (vsp) {
            for (int r = 0; r < 16; ++r) {
                std::string n = "vs_v" + std::to_string(r);
                glBindAttribLocation(p, r, n.c_str());
                glBindAttribLocation(p, r, (n + "_bgra").c_str());
            }
        }
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok) {
            L = new D3D9WLinked;
            L->prog = p;
            loc_query(p, L->locs);
            L->vconsts = glGetUniformLocation(p, "vs_uniforms_vec4");
            L->pconsts = glGetUniformLocation(p, "ps_uniforms_vec4");
            for (int i = 0; i < 4; ++i) L->samp[i] = glGetUniformLocation(p, ("ps_s" + std::to_string(i)).c_str());
        } else {
            char log[1024];
            glGetProgramInfoLog(p, sizeof log, nullptr, log);
            printf("d3d9w_progps: link failed (%s vertex stage): %s\n", vsp ? "programmable" : "fixed-function", log);
            glDeleteProgram(p);
        }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    psp->links[vsp] = L;
    return L;
}

// MojoShader's pixel shader plus what D3D does after it: alpha test and fog.
std::string progps_fragment_source(const char *glsl) {
    std::string s = glsl;
    // ps.1.x texture registers: MojoShader writes `vec4 ps_tN = io_5_N;` without declaring the
    // input (and ES 3.00 allows no input in a global initializer): declare it, copy in main().
    std::string init;
    for (int n = 0; n < 8; ++n) {
        std::string t = "ps_t" + std::to_string(n), io = "io_5_" + std::to_string(n);
        std::string g = "vec4 ";
        g += t;
        g += " = ";
        g += io;
        g += ";";
        if (s.find(g) == std::string::npos) continue;
        std::string decl = "in highp vec4 ";
        decl += io;
        decl += ";\nvec4 ";
        decl += t;
        decl += ";";
        str_replace_all(s, g, decl);
        init += "    ";
        init += t;
        init += " = ";
        init += io;
        init += ";\n";
    }
    if (!init.empty()) {
        size_t mm = s.find("void main()");
        size_t open = mm == std::string::npos ? mm : s.find('{', mm);
        if (open != std::string::npos) s.insert(open + 1, "\n" + init);
    }
    size_t m = s.find("void main()");
    size_t close = s.rfind('}');
    if (m == std::string::npos || close == std::string::npos || close < m) return std::string();
    std::string decl = "uniform bool u_alpha_test_enabled;\nuniform highp float u_alpha_ref;\n"
                       "uniform bool u_fog_enabled;\nuniform highp float u_fog_start;\nuniform highp float u_fog_end;\n"
                       "uniform highp vec3 u_fog_color;\n";
    std::string tail = "\n    if (u_alpha_test_enabled && D3D9W_OUT.a < u_alpha_ref / 255.0) discard;\n"
                       "    if (u_fog_enabled) { highp float f = clamp((u_fog_end - 1.0 / gl_FragCoord.w) / (u_fog_end "
                       "- u_fog_start), 0.0, 1.0);\n"
                       "        D3D9W_OUT.rgb = mix(u_fog_color, D3D9W_OUT.rgb, f); }\n";
    s.insert(close, tail);
    s.insert(m, decl);
    return s;
}

extern "C" D3D9WProgPS *d3d9w_progps_create(const char *glsl, const unsigned char *regs, int nregs) {
    if (g_shader_program == 0) init_shaders();
    std::string f = progps_fragment_source(glsl);
    if (f.empty()) return nullptr;
    // the colour output's name: MojoShader declares it as `out ... vec4 NAME;` (ES 3.00)
    std::string outname;
    {
        size_t o = f.find("out ");
        while (o != std::string::npos) {
            size_t semi = f.find(';', o);
            std::string decl = f.substr(o, semi - o);
            if (decl.find("vec4") != std::string::npos) {
                outname = decl.substr(decl.rfind(' ') + 1);
                break;
            }
            o = f.find("out ", o + 4);
        }
    }
    if (outname.empty()) {
        printf("d3d9w_progps_create: no colour output\n");
        return nullptr;
    }
    str_replace_all(f, "D3D9W_OUT", outname);
    D3D9WProgPS *o = new D3D9WProgPS;
    o->fsrc = f;
    o->regs.assign(regs, regs + nregs);
    GLuint test = compile_shader(GL_FRAGMENT_SHADER, f.c_str());
    if (!test) {
        printf("d3d9w_progps_create: compile failed\n");
        delete o;
        return nullptr;
    }
    glDeleteShader(test);
    return o;
}

extern "C" void d3d9w_progps_use(D3D9WProgPS *p, const float (*consts)[4]) {
    g_progps_active = p;
    g_progps_consts = consts;
}
