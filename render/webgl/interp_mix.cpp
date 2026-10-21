// interp_mix.cpp -- fps.interp's blends: a step's transforms and shader constants between the step
// before it and itself at the refresh's time t, unless the two are a cut (a camera switch, a respawn),
// which shows the step as it is.
#include "internal.h"

bool mat_close(const D3DMATRIX &a, const D3DMATRIX &b) {   // no cut between them (a camera switch, a respawn)
    float dt = 0, dr = 0;
    for (int i = 0; i < 3; i++) dt += (a.m[3][i] - b.m[3][i]) * (a.m[3][i] - b.m[3][i]);
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 3; i++) dr += (a.m[r][i] - b.m[r][i]) * (a.m[r][i] - b.m[r][i]);
    float s = 0;
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 3; i++) s += b.m[r][i] * b.m[r][i];
    return dt < 400.0f * 400.0f && dr < 0.25f * s;   // under 4 m and about 30 degrees in a step
}
D3DMATRIX mat_mix(const D3DMATRIX &a, const D3DMATRIX &b, float t) {
    if (!mat_close(a, b)) return b;
    D3DMATRIX o;
    for (int r = 0; r < 4; r++)
        for (int i = 0; i < 4; i++) o.m[r][i] = a.m[r][i] + (b.m[r][i] - a.m[r][i]) * t;
    return o;
}

// A shader draw's vertex shader constants (n floats): blended into `out`, or false at a cut (a
// register jumped by more than half its size; small values may move freely).
bool consts_mix(const float *a, const float *b, size_t n, float t, float *out) {
    for (size_t r = 0; r + 4 <= n; r += 4) {
        float d = 0, s = 0;
        for (size_t i = r; i < r + 4; i++) d += (b[i] - a[i]) * (b[i] - a[i]), s += a[i] * a[i] + b[i] * b[i];
        if (d > 1e-4f && d > 0.25f * s) return false;
    }
    for (size_t i = 0; i < n; i++) out[i] = a[i] + (b[i] - a[i]) * t;
    return true;
}

bool floats_differ(const float *a, const float *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return true;
    return false;
}
