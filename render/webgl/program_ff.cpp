// program_ff.cpp -- the fixed-function GL program and the per-draw program selection.
#include "internal.h"

const char *vertex_shader_source =
#include "ffp_vert.inc"
    ;
const char *fragment_shader_source =
#include "ffp_frag.inc"
    ;

void str_replace_all(std::string &s, const std::string &a, const std::string &b) {
    for (size_t at = s.find(a); at != std::string::npos; at = s.find(a, at + b.size())) s.replace(at, a.size(), b);
}

// Upload the constant registers a translated shader reads: slot i of its uniform array holds
// register regs[i] of the device's nregs registers.
static void upload_consts(GLint loc, const std::vector<unsigned char> &regs, const float (*consts)[4], size_t nregs) {
    if (loc == -1 || !consts || regs.empty()) return;
    float buf[96 * 4];
    size_t n = std::min(regs.size(), nregs);
    for (size_t i = 0; i < n; ++i) memcpy(buf + 4 * i, consts[regs[i] < nregs ? regs[i] : 0], 16);
    glUniform4fv(loc, (GLsizei)n, buf);
}

// Every draw path selects its program here: the fixed-function one, or the programmable shader in
// use with its locations swapped in and the constant registers it reads uploaded.
void use_draw_program() {
    D3D9WProgVS *p = g_progvs_active;
    if (g_progps_active) {
        D3D9WLinked *L = progps_link(p, g_progps_active);
        if (L) {
            glUseProgram(L->prog);
            loc_apply(L->locs);
            if (p) upload_consts(L->vconsts, p->regs, g_progvs_consts, 96);
            upload_consts(L->pconsts, g_progps_active->regs, g_progps_consts, 8);
            for (int st = 0; st < 4; ++st) {   // ps.1.x: sampler n reads texture stage n
                if (L->samp[st] == -1) continue;
                glActiveTexture(GL_TEXTURE0 + st);
                glBindTexture(GL_TEXTURE_2D, g_texture_stages[st]);
                if (g_texture_stages[st]) apply_sampler_states(st, false);
                glUniform1i(L->samp[st], st);
            }
            glActiveTexture(GL_TEXTURE0);
            g_last_bound_texture_id = g_texture_stages[0];
            g_last_bound_lightmap_id = g_texture_stages[1];
            return;
        }
    }
    GLuint prog = p ? p->prog : g_shader_program;
    glUseProgram(prog);
    loc_apply(p ? p->locs : g_ff_locs);
    if (g_ts_uniform != -1) {   // stages 0-2: COLOROP, COLORARG1, COLORARG2, ALPHAOP, ALPHAARG1, ALPHAARG2
        static const int k[6] = { D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
                                  D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2 };
        GLint v[18];
        for (int st = 0; st < 3; ++st)
            for (int i = 0; i < 6; ++i) v[st * 6 + i] = (GLint)g_texture_stage_states[st][k[i]];
        glUniform1iv(g_ts_uniform, 18, v);
    }
    if (g_specular_add_uniform != -1) glUniform1i(g_specular_add_uniform, g_render_state_specular ? 1 : 0);
    if (p) upload_consts(p->consts, p->regs, g_progvs_consts, 96);
}

GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        printf("Shader compilation failed: %s\n", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// D3D9's sampler defaults (the game sets LINEAR mip filtering for gameplay itself).
void reset_sampler_states() {
    for (int i = 0; i < 8; ++i) {
        g_sampler_states[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
    }
}

void init_shaders() {
    if (g_shader_program != 0) return;
    reset_sampler_states();
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    g_shader_program = glCreateProgram();
    glAttachShader(g_shader_program, vs);
    glAttachShader(g_shader_program, fs);
    glLinkProgram(g_shader_program);
    GLint success;
    glGetProgramiv(g_shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(g_shader_program, 512, nullptr, info_log);
        printf("Shader linking failed: %s\n", info_log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    loc_query(g_shader_program, g_ff_locs);
    loc_apply(g_ff_locs);
}
