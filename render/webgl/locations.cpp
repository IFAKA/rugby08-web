// locations.cpp -- attribute/uniform locations: one global per name (g_<name>_location for a_<name>,
// g_<name>_uniform for u_<name>, listed in internal.h), set from the GL program in use.
//
// runtime/d3d8/d3d8_vshader.cpp translates vs.1.x to GLSL ES 3.00 (MojoShader). Each shader is linked
// with a GLSL ES 3.00 copy of the fixed-function fragment shader whose uniforms keep their names, so
// the draw paths set them through the same globals: every program has its own values for them
// (loc_query) and use_draw_program() swaps them in (loc_apply). A programmable shader computes clip
// space itself, so the fixed-function matrix/lighting uniforms are absent there (location -1, ignored).
#include "internal.h"

GLuint g_shader_program = 0;
#define D3D9W_DEF_A(n) GLint g_##n##_location = -1;
#define D3D9W_DEF_U(n) GLint g_##n##_uniform = -1;
D3D9W_ATTRIBS(D3D9W_DEF_A)
D3D9W_UNIFORMS(D3D9W_DEF_U)

struct Loc {
    GLint *var;
    const char *name;
    bool attrib;
};
#define D3D9W_ROW_A(n) { &g_##n##_location, "a_" #n, true },
#define D3D9W_ROW_U(n) { &g_##n##_uniform, "u_" #n, false },
static const Loc k_locs[] = { D3D9W_ATTRIBS(D3D9W_ROW_A) D3D9W_UNIFORMS(D3D9W_ROW_U) };
static const int k_nlocs = (int)(sizeof k_locs / sizeof k_locs[0]);

std::vector<GLint> g_ff_locs;
static const std::vector<GLint> *g_applied_locs = nullptr;

void loc_query(GLuint p, std::vector<GLint> &v) {
    v.resize(k_nlocs);
    for (int i = 0; i < k_nlocs; ++i)
        v[i] = k_locs[i].attrib ? glGetAttribLocation(p, k_locs[i].name) : glGetUniformLocation(p, k_locs[i].name);
}

void loc_apply(const std::vector<GLint> &v) {
    if (g_applied_locs == &v) return;
    for (int i = 0; i < k_nlocs; ++i) *k_locs[i].var = v[i];
    g_applied_locs = &v;
}
