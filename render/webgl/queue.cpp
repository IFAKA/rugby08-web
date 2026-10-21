// queue.cpp -- the render thread's queue: wined3d's CSMT command stream, at our D3D9 device. The game's
// thread posts each device call as a closure (its pointed-to data copied) and goes on; the render
// thread, which owns the GL context, runs them in order. A step's calls are handed over in one batch at
// Present (one lock a step, not one a call) and the game runs at most one step ahead of the drawing.
// Calls that return what was drawn (readbacks, screenshots) wait for the queue to run dry. Off (no
// render thread), every call runs where it is made, as before.
#include "internal.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {
using Batch = std::vector<std::function<void()>>;
Batch g_fill;               // the game thread's: being posted
Batch g_ready;              // handed over, not taken yet
bool g_has_ready;
uint64_t g_handed, g_done;  // batches handed over / run (both under g_mu)
std::mutex g_mu;
std::condition_variable g_cv;
std::atomic<bool> g_on;
thread_local bool t_render;   // this thread runs the queue

// Shader constants: the game's draws read the D3D8 layer's registers (d3d9w_progvs/ps_use), which the
// game rewrites before the render thread drew; each draw sends what changed since the last one.
const float (*g_game_vsc)[4], (*g_game_psc)[4];
float g_sent_vsc[100][4], g_sent_psc[8][4];
bool g_sent_any;
int g_syncs;   // the game's waits for the render thread since d3d9w_rq_syncs_take

void hand(void) {   // g_fill to the render thread, once it took the batch before
    std::unique_lock<std::mutex> l(g_mu);
    g_cv.wait(l, [] { return !g_has_ready; });
    std::swap(g_ready, g_fill);
    g_fill.clear();
    g_has_ready = true;
    g_handed++;
    g_cv.notify_all();
}
}   // namespace

bool rq_on(void) { return g_on.load(std::memory_order_relaxed); }
bool rq_game(void) { return !t_render && rq_on(); }
float g_rq_vsc[100][4], g_rq_psc[8][4];   // the render thread's copies

template <size_t N> void send_consts(const float (*game)[4], float (&sent)[N][4], float (*to)[4]) {
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): the same bits, not equal values
    if (!game || (g_sent_any && !memcmp(sent, game, sizeof sent))) return;
    memcpy(sent, game, sizeof sent);
    std::array<float, N * 4> v;
    memcpy(v.data(), game, sizeof sent);
    rq_post([v, to] { memcpy(to, v.data(), sizeof v); });
}
void rq_consts(void) {
    send_consts(g_game_vsc, g_sent_vsc, g_rq_vsc);
    send_consts(g_game_psc, g_sent_psc, g_rq_psc);
    g_sent_any = true;
}
void rq_use_consts(const float (*vs)[4], const float (*ps)[4], bool set_vs) {   // d3d9w_prog*s_use, game side
    if (set_vs) g_game_vsc = vs;
    else g_game_psc = ps;
    g_sent_any = false;
    rq_consts();
}

void rq_post(std::function<void()> f) {
    if (!rq_game()) return f();
    g_fill.push_back(std::move(f));
}

void rq_sync(void) {
    if (!rq_game()) return;
    g_syncs++;
    hand();
    std::unique_lock<std::mutex> l(g_mu);
    g_cv.wait(l, [] { return g_done == g_handed; });
}

void rq_call(const std::function<void()> &f) {   // run on the render thread, return when it ran
    if (!rq_game()) return f();
    rq_post([&f] { f(); });
    rq_sync();
}

// The render thread calls this first; from then on the game thread's calls go through the queue.
extern "C" void d3d9w_rq_start(void) {
    t_render = true;
    g_on = true;
}

extern "C" int d3d9w_rq_syncs_take(void) {   // (perf line: how often the game waited for the drawing)
    int n = g_syncs;
    g_syncs = 0;
    return n;
}

extern "C" void d3d9w_rq_hand(void) {   // at Present: the step's calls to the render thread
    if (rq_game() && !g_fill.empty()) hand();
}

// The render thread: run the next batch, waiting for it at most `ms`. How long it ran (ms), < 0 if none.
extern "C" double d3d9w_rq_run(double ms) {
    Batch b;
    {
        std::unique_lock<std::mutex> l(g_mu);
        if (!g_cv.wait_for(l, std::chrono::duration<double, std::milli>(ms > 0 ? ms : 0), [] { return g_has_ready; }))
            return -1;
        std::swap(b, g_ready);
        g_has_ready = false;
        g_cv.notify_all();
    }
    auto t0 = std::chrono::steady_clock::now();
    for (auto &f : b) f();
    double ran = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_done++;
    }
    g_cv.notify_all();
    return ran;
}

// For runtime/: a call on the render thread, in order with the device calls (posted, or waited for).
extern "C" void d3d9w_rq_post(void (*f)(void *), void *arg) {
    rq_post([=] { f(arg); });
}
extern "C" void d3d9w_rq_call(void (*f)(void *), void *arg) {
    rq_call([=] { f(arg); });
}
