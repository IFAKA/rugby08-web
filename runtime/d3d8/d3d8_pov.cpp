// d3d8_pov.cpp -- this player's own camera ("POV"), applied only when drawing.
// The game's camera stays what the game computes (shared by every netplay instance, so a local view
// can never desync the match); here the 3D scene drawn to the back buffer is re-projected to a local
// view: the game camera orbited around the point it looks at on the pitch, nearer or farther, higher
// or lower. Fixed-function draws get the local view matrix; shader draws get a clip-space correction
// (constant registers 96..99, appended to every translated shader by d3d8_vshader.cpp).
// Limits: the game culls what its own camera cannot see, so presets stay close to the game's view.
#include "d3d8_dev.hpp"
#include "fe_party.h"
#include "own_hud.h"
#include "../debug/debug.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef float M4[4][4];
static const struct {
    const char *name;
    float scale, pitch_deg;
} k_presets[] = {
    { "Game camera", 1.0f, 0.0f }, { "Closer", 0.65f, 0.0f }, { "Wider", 1.45f, 0.0f },
    { "Higher", 1.1f, 14.0f },     { "Lower", 0.9f, -7.0f },  { "Overhead", 1.25f, 20.0f },
};
static const int k_npresets = (int)(sizeof k_presets / sizeof k_presets[0]);
extern "C" int rt_pov = 0;
extern "C" float rt_pov_yaw = 0;   // degrees about the vertical through the pivot (checks: RT_POV_YAW)
extern "C" int rt_pov_way = 0;     // +1/-1: drawn looking that way along the pitch (z) at the same point (own_end.c)
static bool pov_on() { return rt_pov || rt_pov_yaw != 0 || rt_pov_way != 0; }

extern "C" const char *rt_pov_name(void) { return k_presets[rt_pov].name; }
extern "C" void rt_pov_set(int i) {
    rt_pov = i >= 0 && i < k_npresets ? i : 0;
    rt_log("camera: %s", rt_pov_name());
}
extern "C" void rt_pov_cycle(void) { rt_pov_set((rt_pov + 1) % k_npresets); }

static void mul(const M4 a, const M4 b, M4 r) {
    M4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[i][k] * b[k][j];
            t[i][j] = s;
        }
    memcpy(r, t, sizeof t);
}
static bool inv(const M4 m, M4 r) {   // Gauss-Jordan with partial pivoting
    float a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) a[i][j] = j < 4 ? m[i][j] : (j - 4 == i);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int i = c + 1; i < 4; i++)
            if (fabsf(a[i][c]) > fabsf(a[p][c])) p = i;
        if (fabsf(a[p][c]) < 1e-12f) return false;
        if (p != c)
            for (int j = 0; j < 8; j++) {
                float t = a[c][j];
                a[c][j] = a[p][j];
                a[p][j] = t;
            }
        float d = a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] /= d;
        for (int i = 0; i < 4; i++)
            if (i != c) {
                float f = a[i][c];
                for (int j = 0; j < 8; j++) a[i][j] -= f * a[c][j];
            }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r[i][j] = a[i][j + 4];
    return true;
}
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float *a, const float *b, float *r) {
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}
static void norm3(float *v) {
    float l = sqrtf(dot3(v, v));
    if (l > 0) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

// The local view for game view V and projection P (D3D row-vector convention: view = world * V).
// P's _34 is the handedness: +1 left-handed (camera looks along +z), -1 right-handed (-z, this game).
static bool local_view(const M4 V, const M4 P, M4 out) {
    if (!pov_on()) return false;
    M4 C;
    if (!inv(V, C)) return false;   // camera to world: rows right, up, z, position
    const float *u = C[1], *pos = C[3];
    float hs = P[2][3] > 0 ? 1.0f : -1.0f;
    float look[3] = { hs * C[2][0], hs * C[2][1], hs * C[2][2] }, r[3] = { C[0][0], C[0][1], C[0][2] };
    norm3(look);
    norm3(r);
    int ax = 0;                     // world up: the world axis closest to the camera's up
    for (int k = 1; k < 3; k++)
        if (fabsf(u[k]) > fabsf(u[ax])) ax = k;
    float up[3] = { 0, 0, 0 };
    up[ax] = u[ax] > 0 ? 1.0f : -1.0f;
    float ld = dot3(look, up),
          t = 3000.0f;   // pivot: where the view ray meets the ground (up . x = 0); world units are cm
    if (ld < -0.05f) {
        t = -dot3(pos, up) / ld;
        if (t < 100) t = 100;
        if (t > 20000) t = 20000;
    }
    float piv[3] = { pos[0] + t * look[0], pos[1] + t * look[1], pos[2] + t * look[2] };
    float off[3] = { pos[0] - piv[0], pos[1] - piv[1], pos[2] - piv[2] };
    // orbit about the horizontal axis through the pivot (Rodrigues); positive pitch = higher camera
    float k[3];
    cross3(up, off, k);
    norm3(k);
    float th = k_presets[rt_pov].pitch_deg * 3.14159265f / 180.0f, rot[3];
    for (int pass = 0; pass < 2; pass++) {
        float c = cosf(th), sn = sinf(th), kx[3], kd = dot3(k, off);
        cross3(k, off, kx);
        for (int i = 0; i < 3; i++) rot[i] = off[i] * c + kx[i] * sn + k[i] * kd * (1 - c);
        if ((th > 0) == (dot3(rot, up) > dot3(off, up)) || th == 0) break;
        th = -th;                   // wrong way round: the other direction raises the camera
    }
    if (rt_pov_yaw != 0) {   // the same point from another side: about the world's up (Rodrigues)
        float c = cosf(rt_pov_yaw * 3.14159265f / 180.0f), sn = sinf(rt_pov_yaw * 3.14159265f / 180.0f), ux[3];
        float ud = dot3(up, rot), q[3];
        cross3(up, rot, ux);
        for (int i = 0; i < 3; i++) q[i] = rot[i] * c + ux[i] * sn + up[i] * ud * (1 - c);
        memcpy(rot, q, sizeof q);   // (left and right turn with the view: no fix below)
    }
    if (rt_pov_way && ax == 1) {   // straight down the pitch, rot's height and distance kept: a turning
        float l = sqrtf(rot[0] * rot[0] + rot[2] * rot[2]);   // game camera is held still (own_end.c)
        rot[0] = 0, rot[2] = -(float)rt_pov_way * l;
    }
    float sc = k_presets[rt_pov].scale, eye[3];
    for (int i = 0; i < 3; i++) eye[i] = piv[i] + sc * rot[i];
    float z[3] = { piv[0] - eye[0], piv[1] - eye[1], piv[2] - eye[2] }, x[3], y[3];
    norm3(z);
    for (int i = 0; i < 3; i++) z[i] *= hs;   // camera z: the look direction (LH) or its opposite (RH)
    cross3(up, z, x);
    norm3(x);
    cross3(z, x, y);
    if (rt_pov_yaw == 0 && !rt_pov_way && dot3(x, r) < 0)
        for (int i = 0; i < 3; i++) { x[i] = -x[i]; }   // keep the game's left/right
    float m[4][4] = { { x[0], y[0], z[0], 0 },
                      { x[1], y[1], z[1], 0 },
                      { x[2], y[2], z[2], 0 },
                      { -dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1 } };
    memcpy(out, m, sizeof m);
    return true;
}

static bool perspective(const M4 P) { return fabsf(P[2][3]) > 1e-6f && fabsf(P[3][3]) < 1e-6f; }

// Fixed function: the view to use for this draw (false: the game's).
extern "C" bool pov_ff_view(const D3DMATRIX &V, const D3DMATRIX &P, D3DMATRIX *out) {
    if (!pov_on() || !perspective(P.m)) return false;
    return local_view(V.m, P.m, out->m);
}

// Shaders: clip' = clip * P^-1 V^-1 V' P, as 4 columns (registers 96..99); identity if no local view.
extern "C" void pov_clip_columns(float cols[4][4]) {
    static const float id[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
    memcpy(cols, id, sizeof id);
    if (!pov_on()) return;
    M4 V, P, Vl, Vi, Pi, M;
    for (int i = 0; i < 16; i++) {
        uint32_t w = rd32(0xA60B68 + 4u * i), q = rd32(0xA60C28 + 4u * i);   // the game's camera
        memcpy(&V[i / 4][i % 4], &w, 4);
        memcpy(&P[i / 4][i % 4], &q, 4);
    }
    if (!perspective(P) || !local_view(V, P, Vl) || !inv(P, Pi) || !inv(V, Vi)) return;
    mul(Pi, Vi, M);
    mul(M, Vl, M);
    mul(M, P, M);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) cols[j][i] = M[i][j];
}

// ---- around each draw (d3d8_device.cpp) ---------------------------------------------------------
// For 3D drawn to the back buffer: shader draws read the clip correction from registers 96..99,
// fixed-function draws get the local view for the draw. Returns whether pov_post must restore the view.
bool d3d8_pov_pre(const uint32_t *a) {
    static int env = 0;   // RT_POV=N: start with camera preset N (checks)
    if (!env) {
        env = 1;
        if (const char *e = getenv("RT_POV")) { rt_pov_set(atoi(e)); }
        if (const char *e = getenv("RT_POV_YAW")) rt_pov_yaw = (float)atof(e);   // (checks: the other end)
    }
    DevState *s = S(a);
    Obj *rt = d3d8_obj(s->rt), *bb = d3d8_obj(s->backbuffer);   // the scene, on screen or in a screen-sized target
    bool to_screen = rt && bb && rt->w == bb->w && rt->h == bb->h;
    if (s->progvs) {
        if (to_screen) pov_clip_columns(&s->vsc[96]);
        else {
            memset(&s->vsc[96], 0, 64);
            s->vsc[96][0] = s->vsc[97][1] = s->vsc[98][2] = s->vsc[99][3] = 1;
        }
        return false;
    }
    if (!pov_on() || !to_screen) return false;
    auto v = s->xf.find(D3DTS_VIEW), p = s->xf.find(D3DTS_PROJECTION);
    D3DMATRIX lv;
    if (v == s->xf.end() || p == s->xf.end() || !pov_ff_view(v->second, p->second, &lv)) return false;
    D(a)->SetTransform(D3DTS_VIEW, &lv);
    return true;
}
void d3d8_pov_post(const uint32_t *a, bool set) {   // (after every draw: also ends d3d8_left_out's verdict)
    if (set) D(a)->SetTransform(D3DTS_VIEW, &S(a)->xf[D3DTS_VIEW]);
    S(a)->skip = false;
}

// A party: the menus' cursor (a quad the world transform places) drawn at this screen's own pointer,
// on this GPU only (runtime/core/ls_cursor.c). Returns whether cursor_post must restore the world.
bool d3d8_cursor_pre(const uint32_t *a) {   // DrawPrimitiveUP (this, type, primCount, data, stride)
    DevState *s = S(a);
    auto w = s->xf.find(D3DTS_WORLD);
    float dx, dy;
    if (w == s->xf.end() || !ls_cursor_shift(&w->second._41, a[2], a[4], G2H(a[3]), &dx, &dy)) return false;
    D3DMATRIX m = w->second;
    m._41 += dx, m._42 += dy;
    D(a)->SetTransform(D3DTS_WORLD, &m);
    return true;
}
void d3d8_cursor_post(const uint32_t *a, bool set) {
    if (set) D(a)->SetTransform(D3DTS_WORLD, &S(a)->xf[D3DTS_WORLD]);
}

// A party leaves another team's prompts out of this player's picture (own_hud.c, by the vertex buffer
// drawn from); RT_DRAW_WHO / RT_DRAW_HIDE_VB (runtime/debug/draw_who.c) tell which draw is which.
bool d3d8_left_out(const uint32_t *a, int kind, uint32_t prims) {
    DevState *s = S(a);
    bool dbg = dbg_draw_who(a, kind, prims, s->stream[0], s->ib, s->tex[0], s->vshader);
    Obj *vb = kind == 0 || kind == 1 ? d3d8_obj(s->stream[0]) : nullptr;
    return dbg || (vb && own_hud_hidden(vb->part));
}
