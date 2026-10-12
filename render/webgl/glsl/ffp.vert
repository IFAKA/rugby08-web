attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
attribute vec2 a_texcoord;
attribute vec2 a_texcoord1;
attribute float a_blendweight;   // D3DFVF_XYZB1: weight of world matrix 0 (matrix 1 gets 1 - w)
uniform bool u_blend;
uniform mat4 u_mvp1;             // D3DTS_WORLDMATRIX(1) * view * projection
varying vec4 v_color;
varying vec2 v_texcoord;
varying vec2 v_texcoord1;
varying vec4 v_position_world;

uniform mat4 u_mvp;
uniform mat4 u_world;
uniform bool u_fbo_flip_y;

// Fog
uniform bool u_fog_enabled;
uniform mat4 u_view_model;
varying float v_eye_depth;

// D3D fixed-function lighting (world space; camera position for the local-viewer specular)
uniform bool u_lighting_enabled;
uniform vec4 u_material_diffuse;
uniform vec4 u_material_ambient;
uniform vec4 u_material_emissive;
uniform vec4 u_material_specular;     // rgb, power in a
uniform vec3 u_global_ambient;
uniform vec4 u_matsrc;                // diffuse, ambient, emissive, specular: 0 material, 1 vertex colour
uniform bool u_specular_enabled;
uniform vec3 u_camera_pos;
uniform int u_nlights;                // enabled lights, packed
uniform vec4 u_lpos[8];               // position, type (1 point, 2 spot, 3 directional)
uniform vec4 u_ldir[8];               // direction, range
uniform vec4 u_ldiff[8];
uniform vec4 u_lamb[8];
uniform vec4 u_lspec[8];
uniform vec4 u_latt[8];               // attenuation 0/1/2, spot falloff
uniform vec4 u_lspot[8];              // cos(theta/2), cos(phi/2)
varying vec4 v_specular;

void main() {
    // Note: v_position_world is always computed even for XYZRHW (where a_position.w = rhw, not 1.0).
    // Clip plane test uses this value. XYZRHW + clip plane is semantically invalid (XYZRHW coordinates
    // are screen-space, not world-space). If this becomes an issue, clip plane
    // test must skip for XYZRHW vertices (check D3DFVF_XYZRHW in C++ before enabling clip plane).
    v_position_world = u_world * a_position;
    gl_Position = u_blend ? mix(u_mvp1 * a_position, u_mvp * a_position, a_blendweight) : u_mvp * a_position;
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;   // D3D clips z to [0,w], GL to [-w,w]: without this, geometry nearer than the near plane is drawn in front of everything
    if (u_fbo_flip_y) gl_Position.y = -gl_Position.y;
    v_texcoord = a_texcoord;
    v_texcoord1 = a_texcoord1;

    v_specular = vec4(0.0);
    if (u_lighting_enabled) {
        // D3D: colour = Me + Ma*Ga + sum(atten*spot*(Ma*La + Md*Ld*max(N.L,0))), alpha = Md.a;
        // specular = sum(atten*spot*Ms*Ls*(N.H)^P) with the local-viewer half vector
        vec3 P = (u_world * a_position).xyz;
        vec3 nw = mat3(u_world) * a_normal;
        vec3 N = dot(nw, nw) > 0.0 ? normalize(nw) : vec3(0.0);   // no normals: ambient and emissive only
        vec4 Md = u_matsrc.x > 0.5 ? a_color : u_material_diffuse;
        vec4 Ma = u_matsrc.y > 0.5 ? a_color : u_material_ambient;
        vec4 Me = u_matsrc.z > 0.5 ? a_color : u_material_emissive;
        vec4 Ms = u_matsrc.w > 0.5 ? a_color : vec4(u_material_specular.rgb, 1.0);
        vec3 V = normalize(u_camera_pos - P);
        vec3 col = Me.rgb + Ma.rgb * u_global_ambient;
        vec3 spec = vec3(0.0);
        for (int i = 0; i < 8; i++) {
            if (i >= u_nlights) break;
            int type = int(u_lpos[i].w + 0.5);
            vec3 L;
            float att = 1.0;
            if (type == 3) {
                L = normalize(-u_ldir[i].xyz);
            } else {
                vec3 d = u_lpos[i].xyz - P;
                float dist = length(d);
                if (dist > u_ldir[i].w) continue;
                L = d / max(dist, 1e-6);
                att = 1.0 / max(u_latt[i].x + u_latt[i].y * dist + u_latt[i].z * dist * dist, 1e-6);
                if (type == 2) {
                    float rho = dot(normalize(-u_ldir[i].xyz), L);
                    if (rho <= u_lspot[i].y) continue;
                    if (rho < u_lspot[i].x)
                        att *= pow(clamp((rho - u_lspot[i].y) / max(u_lspot[i].x - u_lspot[i].y, 1e-6), 0.0, 1.0), u_latt[i].w);
                }
            }
            float NdotL = max(dot(N, L), 0.0);
            col += att * (Ma.rgb * u_lamb[i].rgb + Md.rgb * u_ldiff[i].rgb * NdotL);
            if (u_specular_enabled && NdotL > 0.0)
                spec += att * Ms.rgb * u_lspec[i].rgb * pow(max(dot(N, normalize(L + V)), 0.0), u_material_specular.a);
        }
        v_color = vec4(clamp(col, 0.0, 1.0), Md.a);
        v_specular = vec4(clamp(spec, 0.0, 1.0), 0.0);
    } else {
        v_color = a_color;
    }

    // Fog: pass eye-space Z to fragment shader for per-pixel fog (D3D9 table fog)
    if (u_fog_enabled) {
        vec4 view_pos = u_view_model * a_position;
        v_eye_depth = abs(view_pos.z);
    } else {
        v_eye_depth = 0.0;
    }
}
