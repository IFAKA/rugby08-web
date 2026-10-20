// interp.cpp -- fps.interp: frames between the game's 60 steps a second, for displays faster than 60 Hz.
// RT64's way for N64 recomps: each step's device calls are recorded (draws and Clear are not run, the
// frames come from replays); every refresh replays the last step with each draw's world, view and
// projection matrices blended from the same draw in the step before. A setter's last value per slot
// (the shadow) opens each step's log, so a replay starts from that step's own state. Particles and
// 2D (vertices the game writes each step) move at the step rate, as in RT64.
#include "internal.h"
#include "device.h"
#include <functional>
#include <unordered_map>

namespace {
struct Rec {
    std::function<void(D3D9Device *)> f;
    const void *obj;   // a resource the call names (forgotten if it is destroyed)
    int draw;          // index into Step::draws, or -1
};
struct DrawInfo {
    uint64_t key;      // the same draw in the previous step: call, primitives, buffers, first texture
    D3DMATRIX m[4];    // world, world 1, view, projection when it was drawn
    int match = -1;    // the same draw in the step before (index), or -1
};
struct Snap {        // a buffer's contents before the step rewrote it, for the draws it recorded before that
    size_t until;    // recs before this one read `copy` where they bound `from`
    GLuint from, copy;
    int index;       // an index buffer's copy (its pool)
};
struct Step {
    std::vector<Rec> recs;
    std::vector<DrawInfo> draws;
    std::vector<Snap> snaps;
    bool ok = false;   // complete (ended by Present) and every resource still alive
};
const D3DTRANSFORMSTATETYPE kTs[4] = { D3DTS_WORLD, (D3DTRANSFORMSTATETYPE)257, D3DTS_VIEW, D3DTS_PROJECTION };
std::unordered_map<uint64_t, Rec> g_shadow;   // setter slot -> its last call
D3DMATRIX g_last[4];                          // the transforms' last real values
Step g_older, g_prev, g_cur;   // a refresh shows g_prev blended from g_older; g_cur records
bool g_on, g_replaying;
D3D9Device *g_dev;
float g_eye[3];   // the camera of the last frame drawn (fps_interp_check: is it between the steps?)
bool g_eye_ok;
int g_rewrites;   // textures a step rewrote after its earlier draws read them (replayed with the last contents)
int g_copies;     // buffers copied for that (Snap)
std::unordered_map<const void *, int> g_used;   // resources this step's log names
std::unordered_map<GLuint, size_t> g_copied;    // buffer -> this step's draws when it was last copied, + 1
struct Pool {   // copies no step needs, per target (WebGL fixes a buffer's target)
    std::vector<GLuint> v[2];
} g_pool;   // (vectors only: freeing them at exit makes no GL calls)
size_t g_at;                                    // the rec being replayed
const Step *g_rp = &g_prev;                     // the step being replayed (g_cur when flushed)
size_t g_flushed;                               // g_cur's recs already run (a readback needed them)

void step_drop(Step &s) {
    for (const Snap &x : s.snaps) g_pool.v[x.index].push_back(x.copy);
    s = Step();
}
void step_free(Step &s) noexcept {   // the same, from a destructor: its copies deleted, not pooled
    for (const Snap &x : s.snaps) glDeleteBuffers(1, &x.copy);
    s = Step();
}
GLuint snap(GLuint id) {   // what the replayed rec reads for buffer `id`: the first copy made after it
    GLuint best = id;
    size_t until = SIZE_MAX;
    for (const Snap &s : g_rp->snaps)
        if (s.from == id && s.until > g_at && s.until < until) best = s.copy, until = s.until;
    return best;
}

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
}   // namespace

// A setter: kept as its slot's last value; in a step's log while recording. Runs as usual (false).
bool interp_rec(uint64_t slot, const void *obj, std::function<void(D3D9Device *)> f) {
    if (g_replaying) return false;
    if (g_on) {
        g_cur.recs.push_back({ f, obj, -1 });
        if (obj) g_used[obj] = 1;
    }
    g_shadow[slot] = { std::move(f), obj, -1 };
    return false;
}

void interp_transform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX &m) {
    if (g_replaying) return;
    for (int i = 0; i < 4; i++)
        if (kTs[i] == s) g_last[i] = m;
}

// A draw or Clear: recorded and not run while the refreshes draw the frames (true).
bool D3D9Device::rec_draw(unsigned op, unsigned a, unsigned b, std::function<void(D3D9Device *)> f) {
    if (g_replaying || !g_on) return false;
    g_dev = this;
    DrawInfo di;
    di.key = ((uint64_t)op << 56) ^ ((uint64_t)a << 36) ^ ((uint64_t)b << 20) ^
             ((uint64_t)m_current_vertex_buffer << 40) ^ ((uint64_t)m_current_index_buffer << 8) ^
             (uint64_t)g_texture_stages[0] * 0x9E3779B97F4A7C15ull;
    di.m[0] = m_world, di.m[1] = m_world1, di.m[2] = m_view, di.m[3] = m_projection;
    if (g_progvs_active || g_progps_active) {   // shader constants are read at the draw: theirs, as they were
        D3D9WProgVS *vp = g_progvs_active;
        D3D9WProgPS *pp = g_progps_active;
        std::vector<float> vc(vp && g_progvs_consts ? 400 : 0), pc(pp && g_progps_consts ? 32 : 0);
        if (!vc.empty()) memcpy(vc.data(), g_progvs_consts, vc.size() * 4);
        if (!pc.empty()) memcpy(pc.data(), g_progps_consts, pc.size() * 4);
        f = [f = std::move(f), vp, pp, vc, pc](D3D9Device *d) {
            const float (*ov)[4] = g_progvs_consts, (*op)[4] = g_progps_consts;
            if (!vc.empty()) d3d9w_progvs_use(vp, (const float (*)[4])vc.data());
            if (!pc.empty()) d3d9w_progps_use(pp, (const float (*)[4])pc.data());
            f(d);
            if (!vc.empty()) d3d9w_progvs_use(vp, ov);
            if (!pc.empty()) d3d9w_progps_use(pp, op);
        };
    }
    f = [f = std::move(f)](D3D9Device *d) {   // the buffers as they were at this draw (Snap)
        if (g_rp->snaps.empty()) return f(d);
        GLuint vb = d->m_current_vertex_buffer, ib = d->m_current_index_buffer, sv[16];
        memcpy(sv, g_stream_vb, sizeof sv);
        d->m_current_vertex_buffer = snap(vb), d->m_current_index_buffer = snap(ib);
        for (GLuint &s : g_stream_vb) s = snap(s);
        f(d);
        d->m_current_vertex_buffer = vb, d->m_current_index_buffer = ib;
        memcpy(g_stream_vb, sv, sizeof sv);
    };
    g_cur.recs.push_back({ std::move(f), nullptr, (int)g_cur.draws.size() });
    g_cur.draws.push_back(di);
    return true;
}

size_t prim_vertices(D3DPRIMITIVETYPE t, UINT n) {
    switch (t) {
    case D3DPT_POINTLIST: return n;
    case D3DPT_LINELIST: return 2 * (size_t)n;
    case D3DPT_LINESTRIP: return n + 1;
    case D3DPT_TRIANGLELIST: return 3 * (size_t)n;
    default: return n + 2;   // strips and fans
    }
}

static void step_open() {
    step_drop(g_cur);
    g_used.clear();
    g_copied.clear();
    g_flushed = 0;
    g_cur.recs.reserve(g_shadow.size() + 2048);
    for (auto &kv : g_shadow) g_cur.recs.push_back(kv.second);   // the state the step starts from
    for (auto &r : g_cur.recs)
        if (r.obj) g_used[r.obj] = 1;
}

void interp_present() {   // D3D9Device::Present: the step's log is complete
    if (!g_on || g_replaying) return;
    g_cur.ok = true;
    step_drop(g_older);
    g_older = std::move(g_prev);
    g_prev = std::move(g_cur);
    g_cur = Step();
    step_open();
    std::unordered_map<uint64_t, std::vector<int>> before;   // each draw's twin: the same key, the same turn
    for (size_t i = 0; i < g_older.draws.size(); i++) before[g_older.draws[i].key].push_back((int)i);
    std::unordered_map<uint64_t, size_t> seen;
    for (DrawInfo &d : g_prev.draws) {
        auto it = before.find(d.key);
        size_t n = seen[d.key]++;
        d.match = it != before.end() && n < it->second.size() ? it->second[n] : -1;
    }
}

void interp_forget(const void *obj) noexcept {   // a resource is destroyed: no log may name it
    for (auto it = g_shadow.begin(); it != g_shadow.end();)
        it = it->second.obj == obj ? g_shadow.erase(it) : std::next(it);
    if (g_used.count(obj)) step_free(g_cur);   // (not ok: never replayed)
    for (Step *st : { &g_older, &g_prev })
        for (auto &r : st->recs)
            if (r.obj == obj) {
                step_free(*st);
                break;
            }
}

// New contents for a resource this step's draws may have read (before the upload). A buffer (`buf`,
// `len` bytes, `old` its contents) is copied first, so the draws recorded so far replay with what they
// read: uploaded from the CPU (Apple's GL 4.1 lacks a GPU glCopyBufferSubData, WebGL's would wait).
void interp_upload(const void *obj, GLuint buf, UINT len, GLenum target, const uint8_t *old) {
    if (!g_on || g_replaying || !g_used.count(obj) || g_cur.draws.empty()) return;
    if (!buf) {
        g_rewrites++;
        return;
    }
    size_t &seen = g_copied[buf];
    if (seen == g_cur.draws.size() + 1) return;   // no draw since the last copy read these contents
    seen = g_cur.draws.size() + 1;
    int ix = target == GL_ELEMENT_ARRAY_BUFFER;
    std::vector<GLuint> &pool = g_pool.v[ix];
    GLuint c;
    if (pool.empty()) glGenBuffers(1, &c);
    else c = pool.back(), pool.pop_back();
    glBindBuffer(target, c);
    glBufferData(target, len, old, GL_STREAM_DRAW);
    glBindBuffer(target, 0);
    g_cur.snaps.push_back({ g_cur.recs.size(), buf, c, ix });
    g_copies++;
}

extern "C" void d3d9w_interp_mode(int on) {
    if (g_on == !!on) return;
    g_on = !!on;
    step_drop(g_older), step_drop(g_prev), step_drop(g_cur);
    if (g_on) step_open();
}

// The GPU is about to be read (a render-target lock: the stadium's shadow map): this step's calls not
// run yet are run now, in order, so it holds what D3D would have drawn by now. The refreshes replay
// them again later, as every draw.
extern "C" void d3d9w_interp_flush(void) {
    if (!g_on || g_replaying || !g_dev || g_flushed >= g_cur.recs.size()) return;
    g_replaying = true;
    g_rp = &g_cur;
    for (g_at = g_flushed; g_at < g_cur.recs.size(); g_at++) g_cur.recs[g_at].f(g_dev);
    g_flushed = g_cur.recs.size();
    g_rp = &g_prev;
    g_replaying = false;
}

extern "C" int d3d9w_interp_rewrites(int *copies) {
    *copies = g_copies;
    return g_rewrites;
}

extern "C" int d3d9w_interp_eye(float *e) {
    if (g_eye_ok) memcpy(e, g_eye, sizeof g_eye);
    return g_eye_ok;
}

// One refresh: the last step's frame, its draws' transforms blended from the step before by t (0..1).
extern "C" int d3d9w_interp_draw(float t) {
    if (!g_on || !g_prev.ok || !g_dev) return 0;
    g_replaying = true;
    g_eye_ok = false;
    for (g_at = 0; g_at < g_prev.recs.size(); g_at++) {
        const Rec &r = g_prev.recs[g_at];
        if (r.draw >= 0) {
            const DrawInfo &d = g_prev.draws[r.draw];
            const DrawInfo *a = d.match >= 0 && g_older.ok ? &g_older.draws[d.match] : &d;
            for (int i = 0; i < 4; i++) {
                D3DMATRIX m = mat_mix(a->m[i], d.m[i], t);
                g_dev->SetTransform(kTs[i], &m);
                if (i == 2 && !g_eye_ok && d.match >= 0 && (m.m[3][0] || m.m[3][1] || m.m[3][2])) {
                    for (int k = 0; k < 3; k++)   // the eye: -translation times the rotation's transpose
                        g_eye[k] = -(m.m[3][0] * m.m[k][0] + m.m[3][1] * m.m[k][1] + m.m[3][2] * m.m[k][2]);
                    g_eye_ok = true;
                }
            }
        }
        r.f(g_dev);
    }
    for (int i = 0; i < 4; i++) g_dev->SetTransform(kTs[i], &g_last[i]);   // the device's real state again
    g_replaying = false;
    return 1;
}
