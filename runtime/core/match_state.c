/* match_state.c -- the live match, read from guest memory. Every offset here is documented with its
 * evidence in docs/MATCH_STATE.md (RTTI class, constructor, run where it was measured). Read-only:
 * nothing here changes guest state. RT_MS_LOG=N: one "ms:" trace line every N frames (checks). */
#include "rt.h"
#include "match_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint32_t rt_heap_top(void);   /* heap.c */

#define RUGBY_PTR      0x00A31988u   /* CRugby*, 0 outside a match */
#define SETUP_VA       0x009B6830u   /* match setup (FUN_004215f0) */
#define SETUP_SIZE     0x1924u       /* FUN_005259C0 clears this much */
#define SETUP_CLOCK    0x18F0u       /* float, game seconds */
#define SETUP_SCORE    0x1908u       /* score event list head */
#define SETUP_TEAM_ID0 0xBB8u     /* home team id; away at +0xC60 */
#define TEAM_BLOCK     0xC60u

#define R_TEAM0   0x68u   /* CRugby: home, away CRugbyTeam* */
#define R_BALL    0x98u   /* BallManager* */
#define R_STATES  0x9Cu   /* AiStateMachine<CRugby>*: +4 the current state */
#define T_CONTROL 0xC0u   /* CRugbyTeam: CPlayerControl*[4] */
#define T_PLAYERS 0xD4u   /* CRugbyPlayer*[15] */
#define T_COUNT   0x128u  /* u8 */
#define P_NUMBER  0x78u   /* CRugbyPlayer: u8 shirt number */
#define P_POS     0x84u   /* float[3] */
#define P_HEADING 0xA0u   /* float, degrees */
#define P_STATES  0xBCu   /* PlayerStateMachine*: +4 the current state */
#define C_PLAYER  0x10u   /* CPlayerControl: controlled CRugbyPlayer* */
#define B_POS     0x30u   /* BallManager: float[3] position, then velocity */
#define B_VEL     0x3Cu
#define B_HOLDER  0x114u  /* CRugbyPlayer* or 0 */
#define PARKED    95.25f  /* position of players taken off the field for a cutscene */

static const struct {
    uint32_t vt;
    const char *name;
} k_phases[] = {
    { 0x008ED0A4u, "Kickoff" },   { 0x008ECFF4u, "Formation" },   { 0x008ED194u, "Run" },   { 0x008ED010u, "Free" },
    { 0x008ED0C8u, "LineOut" },   { 0x008ED110u, "Punt" },        { 0x008ED168u, "IKick" }, { 0x008ED088u, "GoalKick" },
    { 0x008ECFA8u, "DropOut22" }, { 0x008ED050u, "QuickOption" }, { 0x008ED134u, "Empty" },
};
/* Provisional points per score type (0 try; 1/2 the place-kick results; 3 a 3-point kick, measured):
   docs/MATCH_STATE.md. */
static const int k_points[4] = { 5, 2, 3, 3 };

static int heap_obj(uint32_t va, uint32_t size) { return va >= RT_HEAP_LO && va + size <= rt_heap_top(); }
static float rdf(uint32_t va) {
    uint32_t u = rd32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint32_t rugby(void) {
    uint32_t r = rd32(RUGBY_PTR);
    return heap_obj(r, 0x100) ? r : 0;
}
static uint32_t team(int t) {
    uint32_t r = rugby();
    uint32_t tm = r && t >= 0 && t < MS_TEAMS ? rd32(r + R_TEAM0 + 4u * (uint32_t)t) : 0;
    return heap_obj(tm, 0x1A4) ? tm : 0;
}
static uint32_t player(int t, int i) {
    uint32_t tm = team(t);
    if (!tm || i < 0 || i >= MS_PLAYERS || i >= rd8(tm + T_COUNT)) return 0;
    uint32_t p = rd32(tm + T_PLAYERS + 4u * (uint32_t)i);
    return heap_obj(p, 0x240) ? p : 0;
}
static uint32_t current_state(uint32_t machine) { return heap_obj(machine, 8) ? rd32(machine + 4) : 0; }
static int find_player(uint32_t p, int *t, int *i) {
    for (int a = 0; a < MS_TEAMS; a++)
        for (int b = 0; b < MS_PLAYERS; b++)
            if (p && player(a, b) == p) {
                *t = a;
                *i = b;
                return 1;
            }
    return 0;
}

int ms_active(void) { return rugby() && team(0) && team(1); }
uint32_t ms_setup_va(void) { return SETUP_VA; }
uint32_t ms_setup_size(void) { return SETUP_SIZE; }
float ms_clock(void) { return rdf(SETUP_VA + SETUP_CLOCK); }
uint32_t ms_team_id(int t) { return rd32(SETUP_VA + SETUP_TEAM_ID0 + (t ? TEAM_BLOCK : 0)); }

uint32_t ms_phase_vt(void) {
    uint32_t r = rugby(), st = r ? current_state(rd32(r + R_STATES)) : 0;
    return heap_obj(st, 4) ? rd32(st) : 0;
}
/* The state's id (state+8, FUN_0068C2C0): what CRugby's change-phase FUN_0062DA80 takes. -1: none. */
int ms_phase_id(void) {
    uint32_t r = rugby(), st = r ? current_state(rd32(r + R_STATES)) : 0;
    return heap_obj(st, 12) ? (int)rd32(st + 8) : -1;
}
const char *ms_phase(void) {
    uint32_t vt = ms_phase_vt();
    for (size_t k = 0; k < sizeof k_phases / sizeof *k_phases; k++)
        if (k_phases[k].vt == vt) return k_phases[k].name;
    return "?";
}

int ms_player(int t, int i, ms_player_t *out) {
    uint32_t p = player(t, i);
    if (!p) return 0;
    for (int k = 0; k < 3; k++) out->pos[k] = rdf(p + P_POS + 4u * (uint32_t)k);
    out->heading = rdf(p + P_HEADING);
    out->number = rd8(p + P_NUMBER);
    out->parked = out->pos[0] == PARKED && out->pos[2] == PARKED;
    uint32_t st = current_state(rd32(p + P_STATES));
    out->state_vt = heap_obj(st, 4) ? rd32(st) : 0;
    return 1;
}

int ms_ball(ms_ball_t *out) {
    uint32_t r = rugby(), b = r ? rd32(r + R_BALL) : 0;
    if (!heap_obj(b, 0x1DC)) return 0;
    for (int k = 0; k < 3; k++) {
        out->pos[k] = rdf(b + B_POS + 4u * (uint32_t)k);
        out->vel[k] = rdf(b + B_VEL + 4u * (uint32_t)k);
    }
    int t = -1, i = -1;
    find_player(rd32(b + B_HOLDER), &t, &i);
    out->holder_team = (int8_t)t;
    out->holder = (int8_t)i;
    return 1;
}

int ms_score_events(ms_score_ev_t *out, int max) {
    int n = 0;
    for (uint32_t e = rd32(SETUP_VA + SETUP_SCORE); heap_obj(e, 0x14) && n < 64; e = rd32(e + 0x10), n++)
        if (n < max) out[n] = (ms_score_ev_t){ rd16(e), rd32(e + 4), rd32(e + 8), rdf(e + 0xC) };
    return n;
}

int ms_score(int t) {
    ms_score_ev_t ev[64];
    int n = ms_score_events(ev, 64), pts = 0;
    for (int k = 0; k < n && k < 64; k++)
        if (ev[k].team_id == ms_team_id(t) && ev[k].type < 4) pts += k_points[ev[k].type];
    return pts;
}

int ms_controlled(int t, int slot, int *index) {
    uint32_t tm = team(t), c = tm && slot >= 0 && slot < 4 ? rd32(tm + T_CONTROL + 4u * (uint32_t)slot) : 0;
    int a, b;
    if (!heap_obj(c, 0x20) || !find_player(rd32(c + C_PLAYER), &a, &b) || a != t) return 0;
    *index = b;
    return 1;
}

/* The three most common player states (class vtables) and how many players are in each. */
static void top_states(uint32_t vt[3], int cnt[3]) {
    uint32_t seen[MS_TEAMS * MS_PLAYERS];
    int n[MS_TEAMS * MS_PLAYERS], ns = 0;
    for (int t = 0; t < MS_TEAMS; t++)
        for (int i = 0; i < MS_PLAYERS; i++) {
            ms_player_t p;
            if (!ms_player(t, i, &p)) continue;
            int k = 0;
            while (k < ns && seen[k] != p.state_vt) k++;
            if (k == ns) {
                seen[ns] = p.state_vt;
                n[ns++] = 0;
            }
            n[k]++;
        }
    for (int j = 0; j < 3; j++) {
        int best = -1;
        for (int k = 0; k < ns; k++)
            if (n[k] > 0 && (best < 0 || n[k] > n[best])) best = k;
        vt[j] = best >= 0 ? seen[best] : 0;
        cnt[j] = best >= 0 ? n[best] : 0;
        if (best >= 0) n[best] = 0;
    }
}

/* "ms: f F phase P clock C score H-A ball X,Y,Z holder T/I hd M in N parked N out N ctl I/I st VT:N ..." */
static void log_line(uint32_t frame) {
    int in = 0, parked = 0, out = 0;
    for (int t = 0; t < MS_TEAMS; t++)
        for (int i = 0; i < MS_PLAYERS; i++) {
            ms_player_t p;
            if (!ms_player(t, i, &p)) continue;
            if (p.parked) parked++;
            else if (p.pos[0] >= -40 && p.pos[0] <= 40 && p.pos[2] >= -75 && p.pos[2] <= 75) in++;
            else out++;
        }
    ms_ball_t b = { { 0 }, { 0 }, -1, -1 };
    ms_ball(&b);
    float hd = -1; /* ball to holder, metres across the ground */
    ms_player_t h;
    if (b.holder_team >= 0 && ms_player(b.holder_team, b.holder, &h)) {
        float dx = b.pos[0] - h.pos[0], dz = b.pos[2] - h.pos[2];
        hd = sqrtf(dx * dx + dz * dz);
    }
    int c0 = -1, c1 = -1;
    ms_controlled(0, 0, &c0);
    ms_controlled(1, 0, &c1);
    uint32_t vt[3];
    int cnt[3];
    top_states(vt, cnt);
    rt_trace("ms: f %u phase %s clock %.2f score %d-%d ball %.2f,%.2f,%.2f holder %d/%d hd %.2f in %d parked %d out %d "
             "ctl %d/%d "
             "st %08X:%d %08X:%d %08X:%d",
             frame, ms_phase(), (double)ms_clock(), ms_score(0), ms_score(1), (double)b.pos[0], (double)b.pos[1],
             (double)b.pos[2], b.holder_team, b.holder, (double)hd, in, parked, out, c0, c1, vt[0], cnt[0], vt[1],
             cnt[1], vt[2], cnt[2]);
}

void ms_frame(uint32_t frame) {
    ms_setup_frame(frame);
    ms_replay_frame(frame);
    ms_measure_frame(frame);
    static int every = -1;
    if (every < 0) {
        const char *e = getenv("RT_MS_LOG");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && frame % (uint32_t)every == 0 && ms_active()) log_line(frame);
}

uint32_t ms_player_va(int t, int i) { return player(t, i); }
uint32_t ms_team_va(int t) { return team(t); }

/* The team a match object belongs to: a CRugbyTeam (WHAT 0), one of its human controls (+0xC0..+0xCC,
   1) or one of its players (2); -1 none of them. */
int ms_team_of(uint32_t va, int *what) {
    int t, i;
    for (int k = 0; k < MS_TEAMS && va; k++) {
        if (team(k) == va) return *what = 0, k;
        for (uint32_t c = 0; team(k) && c < 4; c++)
            if (rd32(team(k) + 0xC0 + 4 * c) == va) return *what = 1, k;
    }
    if (find_player(va, &t, &i)) return *what = 2, t;
    return *what = -1, -1;
}
uint32_t ms_ball_va(void) {
    uint32_t r = rugby(), b = r ? rd32(r + R_BALL) : 0;
    return heap_obj(b, 0x1DC) ? b : 0;
}
