/* match_measure.c -- numbers to compare two games playing the same match (host and friend, on.match_*):
 * tools/net/match_compare.py lines the host's and the friend's "ms: pos" lines up by match time and
 * reports where they first differ.
 *   RT_MS_POS=N  every N frames of a match: "ms: pos f F t T clock C score H-A phase P upd AVG/MAX us
 *                p X,Z X,Z ... (30 players, cm) ball X,Y,Z (cm)". upd is the host CPU time of
 *                CRugby::Update over the frames since the last line (game/overrides/match/rugby_update.c).
 *                A friend (match_puppet.c) adds "pup ...": its own recorded frame against the host's.
 *   RT_MS_SHOT_T=DIR:T1,T2,...  screenshots at match times (see shot_at_time). */
#include "rt.h"
#include "match_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define R_TIME    0x58u     /* CRugby: match time, seconds (the replay frames' time) */
#define RUGBY_PTR 0x00A31988u

static uint64_t g_upd_ns, g_upd_max, g_upd_n, g_t0;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
void ms_measure_update_begin(void) { g_t0 = now_ns(); }
void ms_measure_update_end(void) {
    uint64_t d = now_ns() - g_t0;
    g_upd_ns += d;
    g_upd_n++;
    if (d > g_upd_max) g_upd_max = d;
}

float ms_time(void) {
    uint32_t r = ms_active() ? rd32(RUGBY_PTR) : 0, u = r ? rd32(r + R_TIME) : 0;
    float t;
    memcpy(&t, &u, 4);
    return t;
}

static int cm(float m) { return (int)(m * 100.0f + (m < 0 ? -0.5f : 0.5f)); }

static void pos_line(uint32_t frame) {
    char buf[1024];
    int n = 0;
    for (int k = 0; k < MS_TEAMS * MS_PLAYERS; k++) {
        ms_player_t p;
        if (!ms_player(k / MS_PLAYERS, k % MS_PLAYERS, &p)) n += snprintf(buf + n, sizeof buf - (size_t)n, " -");
        else n += snprintf(buf + n, sizeof buf - (size_t)n, " %d,%d", cm(p.pos[0]), cm(p.pos[2]));
    }
    ms_ball_t b = { { 0 }, { 0 }, -1, -1 };
    ms_ball(&b);
    char pup[160] = "";
    ms_puppet_check(pup, sizeof pup);
    rt_trace("ms: pos f %u t %.3f clock %.2f score %d-%d phase %s upd %llu/%llu us p%s ball %d,%d,%d %s", frame,
             (double)ms_time(), (double)ms_clock(), ms_score(0), ms_score(1), ms_phase(),
             (unsigned long long)(g_upd_n ? g_upd_ns / g_upd_n / 1000 : 0), (unsigned long long)(g_upd_max / 1000), buf,
             cm(b.pos[0]), cm(b.pos[1]), cm(b.pos[2]), pup);
    g_upd_ns = g_upd_max = g_upd_n = 0;
}

/* RT_MS_SHOT_T=DIR:T1,T2,...: a screenshot DIR/t_T.png at the first Present with match time >= T (the
   same moment of the match in two games, whatever their frame numbers). */
static void shot_at_time(void) {
    static const char *spec = (const char *)-1;
    static int next;
    if (spec == (const char *)-1) spec = getenv("RT_MS_SHOT_T");
    if (!spec || !strchr(spec, ':') || !ms_active()) return;
    const char *list = strchr(spec, ':') + 1;
    for (int k = 0; k < next && list; k++) list = strchr(list, ',') ? strchr(list, ',') + 1 : NULL;
    if (!list || !*list || ms_time() < (float)atof(list)) return;
    snprintf(rt_shot_path, sizeof rt_shot_path, "%.*s/t_%.*s.png", (int)(strchr(spec, ':') - spec), spec,
             (int)strcspn(list, ","), list);
    rt_trace("ms: shot t %.3f %s", (double)ms_time(), rt_shot_path);
    next++;
}

void ms_measure_frame(uint32_t frame) {
    shot_at_time();
    static int every = -1;
    if (every < 0) {
        const char *e = getenv("RT_MS_POS");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && frame % (uint32_t)every == 0 && ms_active()) pos_line(frame);
}
