// internal.h -- what the renderer's sources share: GL state, attribute/uniform locations, programs
// and small helpers. Nothing outside render/webgl includes this; the public API is d3d9.h.
#pragma once
#include "d3d9.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include "plat.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <string>
#include <array>
#include <functional>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <cmath>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#ifdef DEBUG_TEXTURE_TEST
#define CHECK_GL_ERROR(op)                                                                                             \
    {                                                                                                                  \
        op;                                                                                                            \
        CheckGLError(#op);                                                                                             \
    }
#else
#define CHECK_GL_ERROR(op) op
#endif
void CheckGLError(const char *label);

// ---- state.cpp -------------------------------------------------------------------------------
extern const char *g_canvas_selector;
extern DWORD g_current_fvf;
extern GLuint g_texture_stages[8];
extern GLuint g_last_bound_texture_id;
extern GLuint g_last_bound_lightmap_id;
extern std::unordered_set<GLuint> g_mipmapped_textures;   // GL textures with a complete mipmap chain
extern float g_max_anisotropy;                            // 1.0 = no anisotropic filtering
extern D3DLIGHT9 g_lights[8];
extern bool g_light_enabled[8];
extern D3DMATERIAL9 g_material;
extern GLuint g_up_vbo, g_up_ibo;                         // draw-from-pointer (UP) buffers
extern size_t g_up_vbo_size, g_up_ibo_size;
extern GLuint &g_cached_texture;                          // = g_last_bound_texture_id
extern GLint g_cached_viewport[4];
extern GLboolean g_cached_scissor_test;
extern GLint g_cached_scissor_box[4];
extern DWORD g_texture_stage_states[8][33];
extern D3DVIEWPORT9 g_viewport;
extern int g_logical_width, g_logical_height;             // the app's back buffer size
extern DWORD g_sampler_states[8][14];
extern D3DMATRIX g_transform_view;                        // the camera, for specular lighting
extern bool g_render_state_specular;                      // D3DRS_SPECULARENABLE
extern GLuint g_stream_vb[16];                            // SetStreamSource, every stream
extern GLsizei g_stream_stride[16];
extern GLuint g_fbo;                                      // the FBO render targets attach to

// ---- locations.cpp: one global per attribute/uniform name, swapped in per GL program --------------
// a_<name> -> g_<name>_location, u_<name> -> g_<name>_uniform. Every name the shaders declare (the
// fixed-function ones in glsl/, the alpha test and fog that program_ps.cpp adds to pixel shaders).
#define D3D9W_ATTRIBS(X) X(position) X(blendweight) X(normal) X(color) X(texcoord) X(texcoord1)
// clang-format off
#define D3D9W_UNIFORMS(X) \
    X(blend) X(mvp1) X(texture) X(texture_lm) X(use_texture) X(use_lightmap) X(mvp) X(world) \
    X(clip_plane) X(clip_plane_enabled) X(fbo_flip_y) X(alpha_test_enabled) X(alpha_ref) X(texture_factor) X(lod_bias) X(fog_enabled) \
    X(fog_start) X(fog_end) X(fog_color) X(view_model) X(lighting_enabled) X(material_diffuse) X(material_ambient) X(global_ambient) \
    X(ts) X(material_emissive) X(material_specular) X(matsrc) X(specular_enabled) X(camera_pos) X(nlights) X(lpos) \
    X(ldir) X(ldiff) X(lamb) X(lspec) X(latt) X(lspot) X(specular_add)
// clang-format on
#define D3D9W_DECL_A(n)  extern GLint g_##n##_location;
#define D3D9W_DECL_U(n)  extern GLint g_##n##_uniform;
D3D9W_ATTRIBS(D3D9W_DECL_A)
D3D9W_UNIFORMS(D3D9W_DECL_U)
extern GLuint g_shader_program;                           // the fixed-function program
extern std::vector<GLint> g_ff_locs;                      // its locations
void loc_query(GLuint p, std::vector<GLint> &v);
void loc_apply(const std::vector<GLint> &v);

// ---- programs ----------------------------------------------------------------------------------
struct D3D9WProgVS {
    std::string vsrc;                   // processed GLSL, relinked with each pixel shader used with it
    GLuint prog = 0;
    std::vector<GLint> locs;
    GLint consts = -1;
    std::vector<unsigned char> regs;      // constant register of each vs_uniforms_vec4 slot
    std::vector<D3D9WElem> elems;
};
struct D3D9WLinked {                    // one vertex stage (programmable or fixed) + one pixel shader
    GLuint prog = 0;
    std::vector<GLint> locs;
    GLint vconsts = -1, pconsts = -1, samp[4] = { -1, -1, -1, -1 };
};
struct D3D9WProgPS {
    std::string fsrc;
    std::vector<unsigned char> regs;    // constant register of each ps_uniforms_vec4 slot
    std::map<D3D9WProgVS *, D3D9WLinked *> links;   // nullptr key: the fixed-function vertex stage
};
extern const char *vertex_shader_source;                  // glsl/ffp.vert
extern const char *fragment_shader_source;                // glsl/ffp.frag
extern D3D9WProgVS *g_progvs_active;
extern const float (*g_progvs_consts)[4];
extern D3D9WProgPS *g_progps_active;
extern const float (*g_progps_consts)[4];
GLuint compile_shader(GLenum type, const char *source);
void init_shaders();
void str_replace_all(std::string &s, const std::string &a, const std::string &b);
void use_draw_program();                                  // program_ff.cpp
void ff_blend_attrib(size_t base, GLsizei stride);       // program_vs.cpp
void progvs_attribs(size_t base, GLsizei stride, bool fbo, int base_vertex);
D3D9WLinked *progps_link(D3D9WProgVS *vsp, D3D9WProgPS *psp);   // program_ps.cpp
void SetFFPLightingUniforms(const D3DMATRIX *world, const DWORD *render_states, bool vertex_color);   // lighting.cpp
void apply_sampler_states(DWORD st, bool aniso);           // sampler.cpp
void reset_sampler_states();                              // program_ff.cpp
void EnsureUPBuffers();                                   // buffers.cpp

// ---- trace.cpp ---------------------------------------------------------------------------------
struct TraceTex {
    unsigned w, h, fmt;
};
extern int g_trace_draws;
extern std::map<GLuint, TraceTex> g_trace_texinfo;       // GL texture -> size and D3D format (trace only)
void trace_fb_diff(bool report);

// ---- FVF helpers -------------------------------------------------------------------------------
/* D3D FVF texture-coordinate size bits use 0/1/2/3 for FLOAT2/FLOAT3/FLOAT4/FLOAT1 respectively. D3D8
 * declarations preserve these widths; treating every coordinate as FLOAT2 shifts subsequent attributes. */
static inline int fvf_tex_components(DWORD fvf, int stage) {
    int code = (int)((fvf >> (16 + 2 * stage)) & 3u);
    return code == 3 ? 1 : code == 1 ? 3 : code == 2 ? 4 : 2;
}
/* D3DFVF_POSITION_MASK (0xE): XYZ 2, XYZRHW 4, XYZB1..XYZB5 6..E. The skinned kinds share bit 4
 * with XYZRHW: testing `fvf & D3DFVF_XYZRHW` drew every skinned mesh (the players) as screen-space
 * vertices at their model coordinates. */
static inline bool fvf_rhw(DWORD f) { return (f & 0xE) == 0x4; }
static inline bool fvf_xyz(DWORD f) {
    DWORD k = f & 0xE;
    return k == 0x2 || k >= 0x6;
}
static inline int fvf_blend_weights(DWORD f) {
    DWORD k = f & 0xE;
    return k >= 0x6 ? (int)(k - 4) / 2 : 0;
}

// ---- matrix helpers ----------------------------------------------------------------------------
// Helper: Determinant of upper-left 3x3 sub-matrix (for orientation/winding detection)
// D3D9 automatically detects mirrored transforms (negative determinant) and flips culling.
// WebGL doesn't, so we compute this per draw call to set glFrontFace correctly.
static inline float det3x3(const D3DMATRIX &m) {
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

// Helper: Matrix multiplication
static inline void matrix_multiply(D3DMATRIX *result, const D3DMATRIX *a, const D3DMATRIX *b) {
    D3DMATRIX temp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp.m[i][j] = 0;
            for (int k = 0; k < 4; k++) { temp.m[i][j] += a->m[i][k] * b->m[k][j]; }
        }
    }
    *result = temp;
}

static inline void matrix_identity(D3DMATRIX *m) {
    memset(m, 0, sizeof(D3DMATRIX));
    m->m[0][0] = 1.0f;
    m->m[1][1] = 1.0f;
    m->m[2][2] = 1.0f;
    m->m[3][3] = 1.0f;
}

// Helper: Set matrix uniform with CPU-side transpose
// Helper: Set matrix uniform.
// [IMPORTANT] D3D memory (Row-Major, v*M) is binary-equivalent to GL (Column-Major, M*v).
// Do NOT transpose manually here if using standard D3D matrix math.
static inline void gl_set_matrix_uniform(GLint location, const D3DMATRIX *matrix) {
    if (location == -1 || !matrix) return;
    glUniformMatrix4fv(location, 1, GL_FALSE, (const float *)matrix);
}

// interp.cpp (fps.interp): device calls recorded per game step, replayed between steps
class D3D9Device;
bool interp_rec(uint64_t slot, const void *obj, std::function<void(D3D9Device *)> f);
void interp_transform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX &m);
void interp_present();
void interp_forget(const void *obj) noexcept;
void interp_upload(const void *obj, GLuint buf = 0, UINT len = 0, GLenum target = 0, const uint8_t *old = nullptr);
size_t prim_vertices(D3DPRIMITIVETYPE t, UINT prims);

// cost.cpp: the renderer's time per kind of work this frame (d3d9w_cost_take)
enum { COST_PROGRAM, COST_TEXTURE, COST_BUFFER, COST_DRAW, COST_N };
extern double g_cost_ms[COST_N];
extern int g_cost_n[COST_N];
double cost_clock();
struct CostScope {   // the time until the end of the scope, counted as one call of kind k
    int k;
    double t0;
    explicit CostScope(int kind) : k(kind), t0(cost_clock()) {}
    ~CostScope() { g_cost_ms[k] += cost_clock() - t0, g_cost_n[k]++; }
};
// queue.cpp: the render thread (fps.interp). A call made on the game's thread while it runs is posted:
// RQ_POST(call) copies the arguments, queues the same call and returns D3D_OK; rq_call runs one there
// and waits (calls that return something). Draws copy the shader constants they read (rq_consts).
bool rq_game(void);
void rq_post(std::function<void()> f);
void rq_call(const std::function<void()> &f);
void rq_sync(void);
void rq_consts(void);
void rq_use_consts(const float (*vs)[4], const float (*ps)[4], bool set_vs);
extern float g_rq_vsc[100][4], g_rq_psc[8][4];
#define RQ_POST(...)                                                                                                   \
    do {                                                                                                               \
        if (rq_game()) {                                                                                               \
            rq_post([=]() { (void)(__VA_ARGS__); });                                                                   \
            return D3D_OK;                                                                                             \
        }                                                                                                              \
    } while (0)
#define RQ_CALL(T, ...)                                                                                                \
    do {                                                                                                               \
        if (rq_game()) {                                                                                               \
            T r_{};                                                                                                    \
            rq_call([&]() { r_ = (__VA_ARGS__); });                                                                    \
            return r_;                                                                                                 \
        }                                                                                                              \
    } while (0)

// interp_mix.cpp
bool mat_close(const D3DMATRIX &a, const D3DMATRIX &b);
D3DMATRIX mat_mix(const D3DMATRIX &a, const D3DMATRIX &b, float t);
bool consts_mix(const float *a, const float *b, size_t n, float t, float *out);
bool floats_differ(const float *a, const float *b, size_t n);
