// program_ff.cpp -- the fixed-function GL program and per-draw program selection.
#include "internal.h"

const char* vertex_shader_source =
#include "ffp_vert.inc"
;
const char* fragment_shader_source =
#include "ffp_frag.inc"
;

void str_replace_all(std::string& s, const std::string& a, const std::string& b) {
    for (size_t at = s.find(a); at != std::string::npos; at = s.find(a, at + b.size())) s.replace(at, a.size(), b);
}

// Every draw path selects its program here: the fixed-function one, or the programmable shader in
// use with its locations swapped in and the constant registers it reads uploaded.
void use_draw_program() {
    D3D9WProgVS* p = g_progvs_active;
    if (g_progps_active) {
        D3D9WLinked* L = progps_link(p, g_progps_active);
        if (L) {
            glUseProgram(L->prog);
            g_cached_program = L->prog;
            loc_apply(L->locs);
            if (p && L->vconsts != -1 && g_progvs_consts && !p->regs.empty()) {
                float buf[96 * 4];
                size_t n = std::min(p->regs.size(), (size_t)96);
                for (size_t i = 0; i < n; ++i) memcpy(buf + 4 * i, g_progvs_consts[p->regs[i] < 96 ? p->regs[i] : 0], 16);
                glUniform4fv(L->vconsts, (GLsizei)n, buf);
            }
            const std::vector<unsigned char>& pr = g_progps_active->regs;
            if (L->pconsts != -1 && g_progps_consts && !pr.empty()) {
                float buf[8 * 4];
                size_t n = std::min(pr.size(), (size_t)8);
                for (size_t i = 0; i < n; ++i) memcpy(buf + 4 * i, g_progps_consts[pr[i] < 8 ? pr[i] : 0], 16);
                glUniform4fv(L->pconsts, (GLsizei)n, buf);
            }
            for (int st = 0; st < 4; ++st) {   // ps.1.x: sampler n reads texture stage n
                if (L->samp[st] == -1) continue;
                glActiveTexture(GL_TEXTURE0 + st);
                glBindTexture(GL_TEXTURE_2D, g_texture_stages[st]);
                if (g_texture_stages[st]) progps_sampler(st);
                glUniform1i(L->samp[st], st);
            }
            glActiveTexture(GL_TEXTURE0);
            g_last_bound_texture_id = g_texture_stages[0];
            g_last_bound_lightmap_id = g_texture_stages[1];
            g_cached_texture = g_texture_stages[0];
            return;
        }
    }
    GLuint prog = p ? p->prog : g_shader_program;
    glUseProgram(prog);
    g_cached_program = prog;
    loc_apply(p ? p->locs : g_ff_locs);
    if (g_ts_uniform != -1) {
        static const int k[6] = { D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2 };
        GLint v[18];
        for (int st = 0; st < 3; ++st)
            for (int i = 0; i < 6; ++i) v[st * 6 + i] = (GLint)g_texture_stage_states[st][k[i]];
        glUniform1iv(g_ts_uniform, 18, v);
    }
    if (g_specular_add_uniform != -1) glUniform1i(g_specular_add_uniform, g_render_state_specular ? 1 : 0);
    if (p && p->consts != -1 && g_progvs_consts && !p->regs.empty()) {
        float buf[96 * 4];
        size_t n = std::min(p->regs.size(), (size_t)96);
        for (size_t i = 0; i < n; ++i) memcpy(buf + 4 * i, g_progvs_consts[p->regs[i] < 96 ? p->regs[i] : 0], 16);
        glUniform4fv(p->consts, (GLsizei)n, buf);
    }
}

// Helper: Compile shader
GLuint compile_shader(GLenum type, const char* source) {
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

// Helper: Initialize shaders
void init_shaders() {
    if (g_shader_program != 0) return;
    
    // Initialize default sampler states
    for (int i = 0; i < 8; ++i) {
        g_sampler_states[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        g_sampler_states[i][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
        g_sampler_states[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;  // D3D9 default; engine sets LINEAR for gameplay
    }

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
    
    g_position_location = glGetAttribLocation(g_shader_program, "a_position");
    g_blendweight_location = glGetAttribLocation(g_shader_program, "a_blendweight");
    g_blend_uniform = glGetUniformLocation(g_shader_program, "u_blend");
    g_mvp1_uniform = glGetUniformLocation(g_shader_program, "u_mvp1");
    g_diffuse_from_vertex_uniform = glGetUniformLocation(g_shader_program, "u_diffuse_from_vertex");
    g_ambient_from_vertex_uniform = glGetUniformLocation(g_shader_program, "u_ambient_from_vertex");
    g_light0_type_uniform = glGetUniformLocation(g_shader_program, "u_light0_type");
    g_light0_direction_uniform = glGetUniformLocation(g_shader_program, "u_light0_direction");
    g_light1_type_uniform = glGetUniformLocation(g_shader_program, "u_light1_type");
    g_light1_direction_uniform = glGetUniformLocation(g_shader_program, "u_light1_direction");
    g_light2_type_uniform = glGetUniformLocation(g_shader_program, "u_light2_type");
    g_light2_direction_uniform = glGetUniformLocation(g_shader_program, "u_light2_direction");
    g_normal_location = glGetAttribLocation(g_shader_program, "a_normal");
    g_color_location = glGetAttribLocation(g_shader_program, "a_color");
    g_texcoord_location = glGetAttribLocation(g_shader_program, "a_texcoord");
    g_texcoord1_location = glGetAttribLocation(g_shader_program, "a_texcoord1");
    g_texture_uniform = glGetUniformLocation(g_shader_program, "u_texture");
    g_texture_lm_uniform = glGetUniformLocation(g_shader_program, "u_texture_lm");
    g_use_texture_uniform = glGetUniformLocation(g_shader_program, "u_use_texture");
    g_use_lightmap_uniform = glGetUniformLocation(g_shader_program, "u_use_lightmap");
    g_mvp_uniform = glGetUniformLocation(g_shader_program, "u_mvp");
    g_world_uniform = glGetUniformLocation(g_shader_program, "u_world");
    g_tex_matrix_uniform = glGetUniformLocation(g_shader_program, "u_tex_matrix");
    g_use_tex_matrix_uniform = glGetUniformLocation(g_shader_program, "u_use_tex_matrix");
    g_clip_plane_uniform = glGetUniformLocation(g_shader_program, "u_clip_plane");
    g_clip_plane_enabled_uniform = glGetUniformLocation(g_shader_program, "u_clip_plane_enabled");
    g_fbo_flip_y_uniform = glGetUniformLocation(g_shader_program, "u_fbo_flip_y");
    g_stage0_colorop_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorop");
    g_stage0_colorarg1_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorarg1");
    g_stage0_colorarg2_uniform = glGetUniformLocation(g_shader_program, "u_stage0_colorarg2");
    g_stage0_alphaop_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaop");
    g_stage0_alphaarg1_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaarg1");
    g_stage0_alphaarg2_uniform = glGetUniformLocation(g_shader_program, "u_stage0_alphaarg2");
    g_stage1_op_uniform = glGetUniformLocation(g_shader_program, "u_stage1_op");
    g_alpha_test_enabled_uniform = glGetUniformLocation(g_shader_program, "u_alpha_test_enabled");
    g_alpha_ref_uniform = glGetUniformLocation(g_shader_program, "u_alpha_ref");
    g_texture_factor_uniform = glGetUniformLocation(g_shader_program, "u_texture_factor");
    g_lod_bias_uniform = glGetUniformLocation(g_shader_program, "u_lod_bias");
    // Fog uniforms
    g_fog_enabled_uniform = glGetUniformLocation(g_shader_program, "u_fog_enabled");
    g_fog_start_uniform = glGetUniformLocation(g_shader_program, "u_fog_start");
    g_fog_end_uniform = glGetUniformLocation(g_shader_program, "u_fog_end");
    g_fog_color_uniform = glGetUniformLocation(g_shader_program, "u_fog_color");
    g_view_model_uniform = glGetUniformLocation(g_shader_program, "u_view_model");
    // FFP Lighting uniforms
    g_lighting_enabled_uniform = glGetUniformLocation(g_shader_program, "u_lighting_enabled");
    g_material_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_material_diffuse");
    g_material_ambient_uniform = glGetUniformLocation(g_shader_program, "u_material_ambient");
    g_global_ambient_uniform = glGetUniformLocation(g_shader_program, "u_global_ambient");
    g_light0_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light0_enabled");
    g_light0_position_uniform = glGetUniformLocation(g_shader_program, "u_light0_position");
    g_light0_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light0_diffuse");
    g_light0_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light0_ambient");
    g_light0_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light0_attenuation");
    g_light0_range_uniform = glGetUniformLocation(g_shader_program, "u_light0_range");
    g_light1_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light1_enabled");
    g_light1_position_uniform = glGetUniformLocation(g_shader_program, "u_light1_position");
    g_light1_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light1_diffuse");
    g_light1_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light1_ambient");
    g_light1_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light1_attenuation");
    g_light1_range_uniform = glGetUniformLocation(g_shader_program, "u_light1_range");
    g_light2_enabled_uniform = glGetUniformLocation(g_shader_program, "u_light2_enabled");
    g_light2_position_uniform = glGetUniformLocation(g_shader_program, "u_light2_position");
    g_light2_diffuse_uniform = glGetUniformLocation(g_shader_program, "u_light2_diffuse");
    g_light2_ambient_uniform = glGetUniformLocation(g_shader_program, "u_light2_ambient");
    g_light2_attenuation_uniform = glGetUniformLocation(g_shader_program, "u_light2_attenuation");
    g_light2_range_uniform = glGetUniformLocation(g_shader_program, "u_light2_range");
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    loc_query(g_shader_program, g_ff_locs);
    loc_apply(g_ff_locs);
}
