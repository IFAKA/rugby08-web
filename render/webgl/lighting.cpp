// lighting.cpp -- fixed-function lighting uniforms (material, ambient, up to 8 lights).
#include "internal.h"

// Helper: Set FFP lighting uniforms (material, ambient, lights 0-2)
void SetFFPLightingUniforms(const D3DMATRIX *world, const DWORD *render_states, bool vertex_color) {
    gl_set_matrix_uniform(g_world_uniform, world);
    // D3DRS_COLORVERTEX (141) with a vertex colour: DIFFUSE/SPECULAR/AMBIENT/EMISSIVEMATERIALSOURCE
    // (145-148) = D3DMCS_COLOR1 take that colour (COLOR2, the specular vertex colour, is not passed)
    bool cv = vertex_color && render_states[141];
    glUniform4f(g_matsrc_uniform, cv && render_states[145] == 1 ? 1.f : 0.f, cv && render_states[147] == 1 ? 1.f : 0.f,
                cv && render_states[148] == 1 ? 1.f : 0.f, cv && render_states[146] == 1 ? 1.f : 0.f);
    const D3DMATERIAL9 &m = g_material;
    glUniform4f(g_material_diffuse_uniform, m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a);
    glUniform4f(g_material_ambient_uniform, m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a);
    glUniform4f(g_material_emissive_uniform, m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Emissive.a);
    glUniform4f(g_material_specular_uniform, m.Specular.r, m.Specular.g, m.Specular.b, m.Power);
    DWORD amb = render_states[D3DRS_AMBIENT];
    glUniform3f(g_global_ambient_uniform, ((amb >> 16) & 0xFF) / 255.0f, ((amb >> 8) & 0xFF) / 255.0f,
                (amb & 0xFF) / 255.0f);
    glUniform1i(g_specular_enabled_uniform, render_states[29] ? 1 : 0);   // D3DRS_SPECULARENABLE
    {   // camera position in world space: the view matrix is a rotation and translation (row vectors)
        const D3DMATRIX &v = g_transform_view;
        float c[3];
        for (int j = 0; j < 3; ++j) c[j] = -(v.m[3][0] * v.m[j][0] + v.m[3][1] * v.m[j][1] + v.m[3][2] * v.m[j][2]);
        glUniform3f(g_camera_pos_uniform, c[0], c[1], c[2]);
    }
    float pos[32], dir[32], dif[32], am[32], sp[32], att[32], spot[32];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        if (!g_light_enabled[i]) continue;
        const D3DLIGHT9 &l = g_lights[i];
        float *q;
        q = pos + 4 * n;
        q[0] = l.Position.x;
        q[1] = l.Position.y;
        q[2] = l.Position.z;
        q[3] = (float)l.Type;
        q = dir + 4 * n;
        q[0] = l.Direction.x;
        q[1] = l.Direction.y;
        q[2] = l.Direction.z;
        q[3] = l.Range;
        q = dif + 4 * n;
        q[0] = l.Diffuse.r;
        q[1] = l.Diffuse.g;
        q[2] = l.Diffuse.b;
        q[3] = l.Diffuse.a;
        q = am + 4 * n;
        q[0] = l.Ambient.r;
        q[1] = l.Ambient.g;
        q[2] = l.Ambient.b;
        q[3] = l.Ambient.a;
        q = sp + 4 * n;
        q[0] = l.Specular.r;
        q[1] = l.Specular.g;
        q[2] = l.Specular.b;
        q[3] = l.Specular.a;
        q = att + 4 * n;
        q[0] = l.Attenuation0;
        q[1] = l.Attenuation1;
        q[2] = l.Attenuation2;
        q[3] = l.Falloff;
        q = spot + 4 * n;
        q[0] = cosf(l.Theta * 0.5f);
        q[1] = cosf(l.Phi * 0.5f);
        q[2] = 0;
        q[3] = 0;
        ++n;
    }
    glUniform1i(g_nlights_uniform, n);
    if (n) {
        glUniform4fv(g_lpos_uniform, n, pos);
        glUniform4fv(g_ldir_uniform, n, dir);
        glUniform4fv(g_ldiff_uniform, n, dif);
        glUniform4fv(g_lamb_uniform, n, am);
        glUniform4fv(g_lspec_uniform, n, sp);
        glUniform4fv(g_latt_uniform, n, att);
        glUniform4fv(g_lspot_uniform, n, spot);
    }
}
