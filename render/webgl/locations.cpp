// locations.cpp -- attribute/uniform locations: one global per name, swapped per GL program.
#include "internal.h"

// Shader program
GLuint g_shader_program = 0;
GLint g_position_location = -1;
GLint g_blendweight_location = -1, g_blend_uniform = -1, g_mvp1_uniform = -1;
GLint g_diffuse_from_vertex_uniform = -1, g_ambient_from_vertex_uniform = -1, g_light0_type_uniform = -1, g_light0_direction_uniform = -1, g_light1_type_uniform = -1, g_light1_direction_uniform = -1, g_light2_type_uniform = -1, g_light2_direction_uniform = -1;
GLint g_color_location = -1;
GLint g_texcoord_location = -1;
GLint g_texcoord1_location = -1;
GLint g_texture_uniform = -1;
GLint g_texture_lm_uniform = -1;
GLint g_use_texture_uniform = -1;
GLint g_use_lightmap_uniform = -1;
GLint g_mvp_uniform = -1;
GLint g_tex_matrix_uniform = -1;
GLint g_use_tex_matrix_uniform = -1;
GLint g_clip_plane_uniform = -1;
GLint g_clip_plane_enabled_uniform = -1;
GLint g_fbo_flip_y_uniform = -1;
GLint g_stage0_colorop_uniform = -1;
GLint g_stage0_colorarg1_uniform = -1;
GLint g_stage0_colorarg2_uniform = -1;
GLint g_stage0_alphaop_uniform = -1;
GLint g_stage0_alphaarg1_uniform = -1;
GLint g_stage0_alphaarg2_uniform = -1;
GLint g_stage1_op_uniform = -1;
GLint g_ts_uniform = -1;
GLint g_material_emissive_uniform = -1, g_material_specular_uniform = -1, g_matsrc_uniform = -1, g_specular_enabled_uniform = -1, g_camera_pos_uniform = -1, g_nlights_uniform = -1, g_lpos_uniform = -1, g_ldir_uniform = -1, g_ldiff_uniform = -1, g_lamb_uniform = -1, g_lspec_uniform = -1, g_latt_uniform = -1, g_lspot_uniform = -1, g_specular_add_uniform = -1;       // u_ts: stage 0-2 ops and args (the fragment combiner)
GLint g_alpha_test_enabled_uniform = -1;
GLint g_alpha_ref_uniform = -1;
GLint g_texture_factor_uniform = -1;
GLint g_lod_bias_uniform = -1;

// Fog uniforms
GLint g_fog_enabled_uniform = -1;
GLint g_fog_start_uniform = -1;
GLint g_fog_end_uniform = -1;
GLint g_fog_color_uniform = -1;
GLint g_view_model_uniform = -1;

// FFP Lighting uniforms
GLint g_normal_location = -1;
GLint g_world_uniform = -1;
GLint g_lighting_enabled_uniform = -1;
GLint g_material_diffuse_uniform = -1;
GLint g_material_ambient_uniform = -1;
GLint g_global_ambient_uniform = -1;
GLint g_light0_enabled_uniform = -1;
GLint g_light0_position_uniform = -1;
GLint g_light0_diffuse_uniform = -1;
GLint g_light0_ambient_uniform = -1;
GLint g_light0_attenuation_uniform = -1;
GLint g_light0_range_uniform = -1;
GLint g_light1_enabled_uniform = -1;
GLint g_light1_position_uniform = -1;
GLint g_light1_diffuse_uniform = -1;
GLint g_light1_ambient_uniform = -1;
GLint g_light1_attenuation_uniform = -1;
GLint g_light1_range_uniform = -1;
GLint g_light2_enabled_uniform = -1;
GLint g_light2_position_uniform = -1;
GLint g_light2_diffuse_uniform = -1;
GLint g_light2_ambient_uniform = -1;
GLint g_light2_attenuation_uniform = -1;
GLint g_light2_range_uniform = -1;

// ---- D3D8 programmable vertex shaders -------------------------------------------------------
// runtime/d3d8/d3d8_vshader.cpp translates vs.1.x to GLSL ES 3.00 (MojoShader). Each shader is
// linked with a GLSL ES 3.00 copy of fragment_shader_source whose uniforms keep their names, so the
// draw paths set them through the same location globals: every program has its own values for
// them (k_loc_*, generated from init_shaders' lookups) and use_draw_program() swaps them in. The
// shader computes clip space itself, so the fixed-function matrix/lighting uniforms are absent
// (location -1, ignored); the render-target Y flip is applied as the fixed-function shader does.
GLint* const k_loc_var[] = {
    &g_position_location,
    &g_blendweight_location,
    &g_blend_uniform,
    &g_mvp1_uniform,
    &g_diffuse_from_vertex_uniform,
    &g_ambient_from_vertex_uniform,
    &g_light0_type_uniform,
    &g_light0_direction_uniform,
    &g_light1_type_uniform,
    &g_light1_direction_uniform,
    &g_light2_type_uniform,
    &g_light2_direction_uniform,
    &g_normal_location,
    &g_color_location,
    &g_texcoord_location,
    &g_texcoord1_location,
    &g_texture_uniform,
    &g_texture_lm_uniform,
    &g_use_texture_uniform,
    &g_use_lightmap_uniform,
    &g_mvp_uniform,
    &g_world_uniform,
    &g_tex_matrix_uniform,
    &g_use_tex_matrix_uniform,
    &g_clip_plane_uniform,
    &g_clip_plane_enabled_uniform,
    &g_fbo_flip_y_uniform,
    &g_stage0_colorop_uniform,
    &g_stage0_colorarg1_uniform,
    &g_stage0_colorarg2_uniform,
    &g_stage0_alphaop_uniform,
    &g_stage0_alphaarg1_uniform,
    &g_stage0_alphaarg2_uniform,
    &g_stage1_op_uniform,
    &g_alpha_test_enabled_uniform,
    &g_alpha_ref_uniform,
    &g_texture_factor_uniform,
    &g_lod_bias_uniform,
    &g_fog_enabled_uniform,
    &g_fog_start_uniform,
    &g_fog_end_uniform,
    &g_fog_color_uniform,
    &g_view_model_uniform,
    &g_lighting_enabled_uniform,
    &g_material_diffuse_uniform,
    &g_material_ambient_uniform,
    &g_global_ambient_uniform,
    &g_light0_enabled_uniform,
    &g_light0_position_uniform,
    &g_light0_diffuse_uniform,
    &g_light0_ambient_uniform,
    &g_light0_attenuation_uniform,
    &g_light0_range_uniform,
    &g_light1_enabled_uniform,
    &g_light1_position_uniform,
    &g_light1_diffuse_uniform,
    &g_light1_ambient_uniform,
    &g_light1_attenuation_uniform,
    &g_light1_range_uniform,
    &g_light2_enabled_uniform,
    &g_light2_position_uniform,
    &g_light2_diffuse_uniform,
    &g_light2_ambient_uniform,
    &g_light2_attenuation_uniform,
    &g_light2_range_uniform,
    &g_ts_uniform,
    &g_material_emissive_uniform,
    &g_material_specular_uniform,
    &g_matsrc_uniform,
    &g_specular_enabled_uniform,
    &g_camera_pos_uniform,
    &g_nlights_uniform,
    &g_lpos_uniform,
    &g_ldir_uniform,
    &g_ldiff_uniform,
    &g_lamb_uniform,
    &g_lspec_uniform,
    &g_latt_uniform,
    &g_lspot_uniform,
    &g_specular_add_uniform,
};
const char* const k_loc_name[] = {
    "a_position",
    "a_blendweight",
    "u_blend",
    "u_mvp1",
    "u_diffuse_from_vertex",
    "u_ambient_from_vertex",
    "u_light0_type",
    "u_light0_direction",
    "u_light1_type",
    "u_light1_direction",
    "u_light2_type",
    "u_light2_direction",
    "a_normal",
    "a_color",
    "a_texcoord",
    "a_texcoord1",
    "u_texture",
    "u_texture_lm",
    "u_use_texture",
    "u_use_lightmap",
    "u_mvp",
    "u_world",
    "u_tex_matrix",
    "u_use_tex_matrix",
    "u_clip_plane",
    "u_clip_plane_enabled",
    "u_fbo_flip_y",
    "u_stage0_colorop",
    "u_stage0_colorarg1",
    "u_stage0_colorarg2",
    "u_stage0_alphaop",
    "u_stage0_alphaarg1",
    "u_stage0_alphaarg2",
    "u_stage1_op",
    "u_alpha_test_enabled",
    "u_alpha_ref",
    "u_texture_factor",
    "u_lod_bias",
    "u_fog_enabled",
    "u_fog_start",
    "u_fog_end",
    "u_fog_color",
    "u_view_model",
    "u_lighting_enabled",
    "u_material_diffuse",
    "u_material_ambient",
    "u_global_ambient",
    "u_light0_enabled",
    "u_light0_position",
    "u_light0_diffuse",
    "u_light0_ambient",
    "u_light0_attenuation",
    "u_light0_range",
    "u_light1_enabled",
    "u_light1_position",
    "u_light1_diffuse",
    "u_light1_ambient",
    "u_light1_attenuation",
    "u_light1_range",
    "u_light2_enabled",
    "u_light2_position",
    "u_light2_diffuse",
    "u_light2_ambient",
    "u_light2_attenuation",
    "u_light2_range",
    "u_ts",
    "u_material_emissive",
    "u_material_specular",
    "u_matsrc",
    "u_specular_enabled",
    "u_camera_pos",
    "u_nlights",
    "u_lpos",
    "u_ldir",
    "u_ldiff",
    "u_lamb",
    "u_lspec",
    "u_latt",
    "u_lspot",
    "u_specular_add",
};
const bool k_loc_attrib[] = {
    true,
    true,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    true,
    true,
    true,
    true,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
};
const int k_nlocs = (int)(sizeof k_loc_var / sizeof k_loc_var[0]);
std::vector<GLint> g_ff_locs;
const std::vector<GLint>* g_applied_locs = nullptr;
void loc_query(GLuint p, std::vector<GLint>& v) {
    v.resize(k_nlocs);
    for (int i = 0; i < k_nlocs; ++i)
        v[i] = k_loc_attrib[i] ? glGetAttribLocation(p, k_loc_name[i]) : glGetUniformLocation(p, k_loc_name[i]);
}
void loc_apply(const std::vector<GLint>& v) {
    if (g_applied_locs == &v) return;
    for (int i = 0; i < k_nlocs; ++i) *k_loc_var[i] = v[i];
    g_applied_locs = &v;
}
