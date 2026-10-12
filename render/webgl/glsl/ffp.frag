    precision mediump float;
    varying vec4 v_color;
    varying vec2 v_texcoord;
    varying vec2 v_texcoord1;
    varying vec4 v_position_world;
    varying vec4 v_specular;
    uniform bool u_specular_add;
    uniform sampler2D u_texture;
    uniform bool u_clip_plane_enabled;
    uniform vec4 u_clip_plane;
    uniform sampler2D u_texture_lm;
    uniform bool u_use_texture;
    uniform bool u_use_lightmap;
    uniform int u_stage0_colorop;
    uniform int u_stage0_colorarg1;
    uniform int u_stage0_colorarg2;
    uniform int u_stage0_alphaop;
    uniform int u_stage0_alphaarg1;
    uniform int u_stage0_alphaarg2;
    uniform int u_stage1_op;
    uniform int u_ts[18];      // stages 0-2: COLOROP, COLORARG1, COLORARG2, ALPHAOP, ALPHAARG1, ALPHAARG2
    uniform bool u_alpha_test_enabled;
    uniform float u_alpha_ref;
    uniform vec4 u_texture_factor;
    uniform float u_lod_bias;

    // Fog (per-pixel, matches D3D9 table fog)
    uniform bool u_fog_enabled;
    uniform float u_fog_start;
    uniform float u_fog_end;
    uniform vec3 u_fog_color;
    varying float v_eye_depth;

    // Helper function to select texture argument source
    vec4 selectTextureArg(int arg) {
        // D3DTA_DIFFUSE = 0, D3DTA_CURRENT = 1, D3DTA_TEXTURE = 2, D3DTA_TFACTOR = 3
        // Note: In GLSL ES 2.0, we can't use bitwise operations, but D3DTA values are already 0-6
        if (arg == 0) return v_color; // D3DTA_DIFFUSE
        if (arg == 1) return v_color; // D3DTA_CURRENT (for stage 0, same as DIFFUSE)
        if (arg == 2) { // D3DTA_TEXTURE
            // Only sample texture if it's actually being used
            if (u_use_texture) {
                return texture2D(u_texture, v_texcoord, u_lod_bias);
            } else {
                return vec4(1.0);
            }
        }
        if (arg == 3) return u_texture_factor; // D3DTA_TFACTOR
        return vec4(1.0); // Default
    }

    // D3DTA_*: DIFFUSE 0, CURRENT 1, TEXTURE 2, TFACTOR 3, SPECULAR 4, TEMP 5; | COMPLEMENT 0x10,
    // | ALPHAREPLICATE 0x20 (complement first). No bitwise ops in GLSL ES 1.0: div/mod arithmetic.
    vec4 ts_arg(int arg, vec4 tex, vec4 cur, vec4 temp) {
        int sel = arg - (arg / 16) * 16;
        int fl = arg / 16;
        vec4 v = temp;
        if (sel == 0) v = v_color;
        else if (sel == 1) v = cur;
        else if (sel == 2) v = tex;
        else if (sel == 3) v = u_texture_factor;
        else if (sel == 4) v = v_specular;
        if (fl - (fl / 2) * 2 == 1) v = vec4(1.0) - v;
        if ((fl / 2) - (fl / 4) * 2 == 1) v = vec4(v.a);
        return v;
    }
    // D3DTEXTUREOP on (arg1, arg2); the blend ops take their factor from the stage's own texture,
    // the diffuse colour, TFACTOR or CURRENT as D3D defines them.
    vec4 ts_op(int op, vec4 a1, vec4 a2, vec4 tex, vec4 cur) {
        if (op == 2) return a1;
        if (op == 3) return a2;
        if (op == 4) return a1 * a2;
        if (op == 5) return a1 * a2 * 2.0;
        if (op == 6) return a1 * a2 * 4.0;
        if (op == 7) return a1 + a2;
        if (op == 8) return a1 + a2 - 0.5;
        if (op == 9) return (a1 + a2 - 0.5) * 2.0;
        if (op == 10) return a1 - a2;
        if (op == 11) return a1 + a2 - a1 * a2;
        if (op == 12) return mix(a2, a1, v_color.a);
        if (op == 13) return mix(a2, a1, tex.a);
        if (op == 14) return mix(a2, a1, u_texture_factor.a);
        if (op == 15) return a1 + a2 * (1.0 - tex.a);
        if (op == 16) return mix(a2, a1, cur.a);
        if (op == 18) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);
        if (op == 19) return vec4(a1.rgb * a2.rgb + a1.a, a1.a);
        if (op == 20) return vec4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);
        if (op == 21) return vec4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a);
        if (op == 24) return vec4(clamp(dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0), 0.0, 1.0));
        return a1 * a2;   // PREMODULATE, bump mapping, MULTIPLYADD/LERP (need ARG0): modulate
    }
    void ts_stage(int cop, int ca1, int ca2, int aop, int aa1, int aa2, vec4 tex,
                  inout vec4 cur, inout vec4 temp, inout bool done) {
        if (done) return;
        if (cop == 1) { done = true; return; }
        vec3 c = ts_op(cop, ts_arg(ca1, tex, cur, temp), ts_arg(ca2, tex, cur, temp), tex, cur).rgb;
        float a = aop == 1 ? cur.a : ts_op(aop, ts_arg(aa1, tex, cur, temp), ts_arg(aa2, tex, cur, temp), tex, cur).a;
        cur = clamp(vec4(c, a), 0.0, 1.0);
    }

    void main() {
        // Clip plane test (shader-based, replaces hardware clip planes for WebGL)
        if (u_clip_plane_enabled) {
            float clip_dist = dot(v_position_world.xyz, u_clip_plane.xyz) + u_clip_plane.w;
            if (clip_dist < 0.0) discard;
        }

        // D3D texture stage cascade, stages 0-2 (stage 0's CURRENT is the diffuse colour). Stage 0
        // samples u_texture with texcoord 0, stage 1 u_texture_lm with texcoord 1; a stage without a
        // bound texture reads TEXTURE as opaque white. COLOROP DISABLE ends the cascade.
        vec4 t0 = u_use_texture ? texture2D(u_texture, v_texcoord, u_lod_bias) : vec4(1.0);
        vec4 t1 = u_use_lightmap ? texture2D(u_texture_lm, v_texcoord1, u_lod_bias) : vec4(1.0);
        vec4 cur = v_color;
        vec4 temp = vec4(0.0);
        bool done = false;
        ts_stage(u_ts[0], u_ts[1], u_ts[2], u_ts[3], u_ts[4], u_ts[5], t0, cur, temp, done);
        ts_stage(u_ts[6], u_ts[7], u_ts[8], u_ts[9], u_ts[10], u_ts[11], t1, cur, temp, done);
        // Stage 2 stays off until the game's light colours are right: players and the ball use
        // MODULATE2X(DIFFUSE, CURRENT) there, and the lit colour is still black in this build.
        vec4 final_color = cur;
        if (u_specular_add) final_color.rgb = clamp(final_color.rgb + v_specular.rgb, 0.0, 1.0);

        if (u_alpha_test_enabled) {
            if (final_color.a < (u_alpha_ref / 255.0)) {
                discard;
            }
        }

        // Apply fog (per-pixel linear fog, matches D3D9 table fog)
        if (u_fog_enabled) {
            float fog_factor = clamp((u_fog_end - v_eye_depth) / (u_fog_end - u_fog_start), 0.0, 1.0);
            final_color.rgb = mix(u_fog_color, final_color.rgb, fog_factor);
        }

        gl_FragColor = final_color;
    }
