/* match_puppet.c -- a friend's game showing the host's match (on.match_puppet). The host's frames are
 * the game's own replay frames with a head (score, view: match_replay.c record()). In a friend's game
 * CRugby::Update skips the match logic (game/overrides/match/rugby_update.c) and each update shows the
 * next host frame: clock, score, view, then players, poses and ball.
 * Source: the network (net_ha.c, a host-authoritative friend: the newest frame received) or
 *   RT_MS_PUPPET_IN=FILE  a host recording (RT_MS_REC_OUT), one frame per update. */
#include "rt.h"
#include "plat.h"
#include "match_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REC_HEAD   96u   /* match_replay.c record(): 32 header bytes, the view, then the frame */
#define FRAME_SIZE 0x118Cu
#define POS_UNITS  52.5f

static uint8_t *g_rec;
static size_t g_nrec;

static void load(void) {
    static int done;
    const char *p = getenv("RT_MS_PUPPET_IN");
    if (done++ || !p) return;
    FILE *f = fopen(p, "rb");
    if (!f) rt_fatal("ms: cannot read %s", p);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_rec = malloc((size_t)n);
    if (!g_rec || fread(g_rec, 1, (size_t)n, f) != (size_t)n) rt_fatal("ms: cannot read %s", p);
    fclose(f);
    g_nrec = (size_t)n / (REC_HEAD + FRAME_SIZE);
    rt_log("ms: puppet: %zu host frames from %s", g_nrec, p);
}

static const uint8_t *frame_at(size_t i) { return g_rec + i * (REC_HEAD + FRAME_SIZE) + REC_HEAD; }
static float fat(const uint8_t *f, uint32_t off) {
    float v;
    memcpy(&v, f + off, 4);
    return v;
}
static void wrf(uint32_t va, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    wr32(va, u);
}

/* The host frame for match time t: the last one at or before it (frames are in time order). */
static const uint8_t *frame_for(float t) {
    static size_t i;
    while (i + 1 < g_nrec && fat(frame_at(i + 1), 0x116C) <= t + 1e-4f) i++;
    while (i > 0 && fat(frame_at(i), 0x116C) > t + 1e-4f) i--;
    return g_nrec ? frame_at(i) : NULL;
}

int ms_puppet_on(void) {
    load();
    return g_nrec > 0 || rt_net_ha == 2;
}
/* A friend's match: CRugby::Update's logic does not run. */
int ms_puppet_skip_update(void) { return ms_puppet_on() && ms_active(); }

/* The next host record (head, then frame), or NULL before the first. */
static const uint8_t *next_record(void) {
    if (rt_net_ha == 2) {
        int fresh;
        return net_ha_newest(&fresh);
    }
    static size_t n;
    return g_nrec ? g_rec + (n < g_nrec ? n++ : g_nrec - 1) * (REC_HEAD + FRAME_SIZE) : NULL;
}

/* As the game's replay drives its replay actors (FUN_0045A560): heading through the actor's vtable slot
   0x1C, position through FUN_0070ED50(&pos), both in the frame's own units. A live player's actor is
   embedded at CRugbyPlayer+0xD8 (the replay actors' class, vtable 0x008D7068). */
static void apply_actors(const uint8_t *f) {
    static uint32_t vec;
    if (!vec && !(vec = gcalloc(16))) return;
    for (int k = 0; k < MS_TEAMS * MS_PLAYERS; k++) {
        uint32_t p = ms_player_va(k / MS_PLAYERS, k % MS_PLAYERS), actor = p ? p + 0xD8 : 0;
        if (!p) continue;
        const uint8_t *e = f + 0xF00 + 16u * (uint32_t)k;
        uint32_t heading;
        memcpy(&heading, e + 12, 4);
        uint32_t sv = g_ecx;
        g_ecx = actor;
        guest_callv(rd32(rd32(actor) + 0x1C), 1, &heading);
        for (int c = 0; c < 3; c++) wrf(vec + 4u * (uint32_t)c, fat(e, 4u * (uint32_t)c));
        g_ecx = actor;
        guest_callv(0x0070ED50u, 1, &vec);
        g_ecx = sv;
    }
}

/* The host frame of this update, in guest memory: the game's replay code reads it from there. */
static uint32_t g_hostbuf;
static int g_forcing;
static uint32_t g_sv_frame, g_sv_blend;

/* Posing a live player (FUN_004039B0, game/overrides/match/pose_player.c): in a friend's game the pose is
   the host's, decoded by the game's replay path (ReplayPoseSource -> FUN_0045B6D0 -> FUN_0071AC50) from
   player k's 128-byte block. The replay manager's frame A (+0x28) is the host frame for the call, with no
   blend (+8 = 0), and the commander answers "playing" (FUN_00457690, replay_playing.c) only inside it. */
int ms_puppet_pose_begin(uint32_t k) {
    uint32_t m = ms_replay_manager();
    if (k >= MS_TEAMS * MS_PLAYERS || !g_hostbuf || !m || !ms_puppet_skip_update()) return 0;
    g_sv_frame = rd32(m + 0x28);
    g_sv_blend = rd32(m + 8);
    wr32(m + 0x28, g_hostbuf);
    wr32(m + 8, 0);
    g_forcing = 1;
    return 1;
}
void ms_puppet_pose_end(void) {
    uint32_t m = ms_replay_manager();
    g_forcing = 0;
    if (!m) return;
    wr32(m + 0x28, g_sv_frame);
    wr32(m + 8, g_sv_blend);
}
int ms_puppet_forcing(void) { return g_forcing; }

static uint32_t tcall(uint32_t va, uint32_t ecx, uint32_t arg) {   /* thiscall, one argument */
    uint32_t sv = g_ecx;
    g_ecx = ecx;
    uint32_t r = guest_callv(va, 1, &arg);
    g_ecx = sv;
    return r;
}

/* The ball as the game's replay places it (FUN_0045A560 at 0x0045A88E..0x0045A932): position (frame
   +0x111C) through FUN_0044B9E0, orientation (the 4x4 matrix at +0x1128) through FUN_00620800 on the
   BallManager's transform (+0x14). */
/* What the friend's own recorder wrote last (its frame B: actor positions, headings, pose blocks, ball)
   against the host frame of the same match time: the friend shows the host's match when they are equal.
   "pup" field of the RT_MS_POS line (match_measure.c). */
int ms_puppet_check(char *out, int size) {
    uint32_t b = ms_replay_current();
    if (!ms_puppet_on() || !b || !g_hostbuf) return 0;
    const uint8_t *mine = G2H(b), *h = g_nrec ? frame_for(fat(mine, 0x116C)) : G2H(g_hostbuf);
    if (!h || fat(mine, 0x116C) != fat(h, 0x116C)) return 0;   /* the same moment only */
    float worst = 0, hd = 0;
    int blocks = 0;
    for (int k = 0; k < MS_TEAMS * MS_PLAYERS; k++) {
        const uint8_t *e = mine + 0xF00 + 16 * k, *g = h + 0xF00 + 16 * k;
        float dx = fat(e, 0) - fat(g, 0), dz = fat(e, 8) - fat(g, 8), d = 100 * sqrtf(dx * dx + dz * dz) / POS_UNITS;
        float a = fabsf(fat(e, 12) - fat(g, 12));
        if (d > worst) worst = d;
        if (a > hd) hd = a;
        blocks += !memcmp(mine + 0x80 * k, h + 0x80 * k, 0x80);
    }
    float bx = fat(mine, 0x111C) - fat(h, 0x111C), by = fat(mine, 0x1120) - fat(h, 0x1120),
          bz = fat(mine, 0x1124) - fat(h, 0x1124);
    return snprintf(out, (size_t)size, "pup dt %.3f worst %.0f cm heading %.1f blocks %d/30 ball %.0f cm",
                    (double)(fat(mine, 0x116C) - fat(h, 0x116C)), (double)worst, (double)hd, blocks,
                    (double)(100 * sqrtf(bx * bx + by * by + bz * bz)));
}

static void place_ball(uint32_t rugby);
void ms_puppet_place(uint32_t rugby) {
    if (!g_hostbuf || !ms_puppet_skip_update()) return;
    apply_actors(G2H(g_hostbuf));
    place_ball(rugby);
}

static void place_ball(uint32_t rugby) {
    static uint32_t s;
    uint32_t ball = rd32(rugby + 0x98);
    if (!g_hostbuf || !ms_puppet_skip_update() || !ball || (!s && !(s = gcalloc(0x80)))) return;
    memcpy(G2H(s), G2H(g_hostbuf + 0x111C), 12);
    tcall(0x006EF680u, s + 0x10, s);   /* Vector3(pos) */
    tcall(0x0044B9E0u, ball, tcall(0x0045C370u, s + 0x20, s + 0x10));
    memcpy(G2H(s + 0x40), G2H(g_hostbuf + 0x1128), 0x40);
    tcall(0x00620800u, ball + 0x14, s + 0x40);
}

/* The host's score events, added the way the game scores (docs/MATCH_STATE.md "Clock and score"): the
   team's stat byte (tries, conversions, drop goals, penalties: what the scoreboard, crowd, commentary and
   result screens count) through the game's increment, and the score list entry with its CScoreData
   message through FUN_005CE320 at the host's time. One event per record (the latest), so at most one
   score per host frame. */
static void score(const uint8_t *h) {
    static const uint32_t k_inc[4] = { 0x00632D00u, 0x00632D20u, 0x00632D40u, 0x00632D60u };
    uint16_t nhost, scorer;
    uint32_t type, team, when;
    memcpy(&nhost, h + 16, 2);
    memcpy(&scorer, h + 18, 2);
    memcpy(&type, h + 20, 4);
    memcpy(&team, h + 24, 4);
    memcpy(&when, h + 28, 4);
    ms_score_ev_t ev[64];
    if (nhost <= ms_score_events(ev, 64) || type > 3) return;
    int t = team == ms_team_id(0) ? 0 : 1;
    uint32_t tv = ms_team_va(t), stats = tv ? rd32(tv + 0x34) : 0;
    if (!stats) return;
    guest_callv(k_inc[type], 1, &stats);
    uint32_t args[6] = { ms_setup_va(), team, scorer, type, when, 1 };
    guest_callv(0x005CE320u, 6, args);
    rt_trace("ms: puppet score type %u team %u scorer %u at %.2f: %d-%d", type, team, scorer, (double)fat(h, 28),
             ms_score(0), ms_score(1));
}

/* Before the friend's presentation step: the host's clock, score, view and match time; the frame goes
   where the replay code reads it (g_hostbuf), players and ball follow after the step (ms_puppet_place). */
void ms_puppet_after_update(uint32_t rugby) {
    const uint8_t *h = ms_puppet_skip_update() ? next_record() : NULL;
    if (!h) return;   /* nothing from the host yet: the match holds */
    float clock;
    memcpy(&clock, h + 4, 4);
    wrf(ms_setup_va() + 0x18F0, clock);
    score(h);
    wrf(rugby + 0x58, fat(h + REC_HEAD, 0x116C));
    if (!g_hostbuf) g_hostbuf = gcalloc(FRAME_SIZE);
    if (g_hostbuf) memcpy(G2H(g_hostbuf), h + REC_HEAD, FRAME_SIZE);
}
