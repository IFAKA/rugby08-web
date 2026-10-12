// state.cpp -- the renderer's global GL/D3D state (declared in internal.h).
#include "internal.h"

const char* g_canvas_selector = "#canvas";

// Current FVF
DWORD g_current_fvf = 0;

// Current texture
GLuint g_texture_stages[8] = { 0 };
GLuint g_last_bound_texture_id = 0xFFFFFFFF;
GLuint g_last_bound_lightmap_id = 0xFFFFFFFF;

// Track which GL textures have a complete mipmap chain
std::unordered_set<GLuint> g_mipmapped_textures;

float g_max_anisotropy = 1.0f; // 1.0 = disabled (no extension)

// FFP State storage
D3DLIGHT9 g_lights[8];
bool g_light_enabled[8] = {false};
D3DMATERIAL9 g_material;

GLuint g_up_vbo = 0;
GLuint g_up_ibo = 0;
size_t g_up_vbo_size = 0;  // Track VBO size for persistent buffer optimization
size_t g_up_ibo_size = 0;  // Track IBO size for persistent buffer optimization

GLuint& g_cached_texture = g_last_bound_texture_id;
GLuint g_cached_program = 0xFFFFFFFF;  // Last used shader program
DWORD g_cached_fvf = 0xFFFFFFFF;       // Last FVF
bool g_cached_vertex_attribs[4] = {false};  // pos, color, tex0, tex1

// Viewport and Scissor state caching (to avoid glGet* calls)
GLint g_cached_viewport[4] = {0, 0, 0, 0};  // Overwritten by first SetViewport
GLboolean g_cached_scissor_test = GL_FALSE;
GLint g_cached_scissor_box[4] = {0, 0, 0, 0};  // Overwritten by first SetScissorRect

DWORD g_texture_stage_states[8][33];
D3DVIEWPORT9 g_viewport;
int g_logical_width = 0;    // App's backbuffer resolution (set from D3DPRESENT_PARAMETERS)
int g_logical_height = 0;

// Texture matrices
D3DMATRIX g_texture_matrices[8];
// Sampler states [Sampler][Type]
DWORD g_sampler_states[8][14]; // Up to D3DSAMP_DMAPOFFSET

// Transform matrices (for GetTransform)
D3DMATRIX g_transform_world;

// Check for OpenGL errors (silent in release builds)
void CheckGLError(const char* label) {
    GLenum err = glGetError();
    while (err != GL_NO_ERROR) {
        // Silently consume errors
        err = glGetError();
    }
}


D3DMATRIX g_transform_view;
D3DMATRIX g_transform_projection;

bool g_render_state_specular = false;   // D3DRS_SPECULARENABLE: add the specular colour after the stages
GLuint g_stream_vb[16];          // SetStreamSource: every stream (the fixed-function paths use stream 0)
GLsizei g_stream_stride[16];
const float (*g_progvs_consts)[4] = nullptr;

// Global FBO for SetRenderTarget
GLuint g_fbo = 0;
