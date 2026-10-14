/* own_end.c -- each player's camera as if playing alone (docs/plans/2026-10-14-online-solo-feel.md,
 * rule 1). Measured (RT_MS_CAMSIDE, RT_MS_CAMEV): the game's end cameras stay behind the human team when
 * only one team has humans (solo, or friends on one team: whoever has the ball) and follow possession when
 * both teams have humans (24/25, 24/26): the couch camera, one view for one TV. Every flip follows the
 * CameraManager's match event 0xAD (possession changed; data: the player who gained the ball); solo hears
 * it for the AI's players too and does not move. So in a party each copy's camera does not hear an 0xAD for
 * a player of another team than this player's: its camera stays at its own team's end, as solo. The
 * camera's event listener and step run in det_local sections (det_local.c): each copy's camera runs its
 * own way, the shared match never sees it and every copy's scheduling stays the same. A player on no team
 * (watching) keeps the game's camera. The camera may draw from the C library's random sequence (rand(),
 * FUN_00874017: the thread's _tiddata +0x14): one copy's own camera must not move the shared sequence, so a
 * party puts the seed back after the camera (measured: a camera event one copy skipped, its match apart 13
 * frames later). RT_MS_RANDCAM=1 traces each time the camera moved it (any game). The same for the copy
 * protection's countdowns (SecuROM: `dec dword [X]; je <check>` in game functions, lifted as written,
 * REVERSE.md; 645 of their 651 counters in [0x01409500, 0x0140AA80)): the camera calls protected getters
 * (FUN_0042A360 from FUN_006E8500 / FUN_006E8180, measured with a watchpoint), so one copy's own camera
 * moved them and its check fired in that copy only: the block is put back after the camera.
 * Trace: "ms: own end team N", "ms: own end: the other team's ball". */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "match_state.h"
#include "pad_attach.h"
#include "../debug/debug.h"
#include <stdlib.h>
#include <string.h>

#define EV_POSSESSION 0xADu

/* Not on in play yet: with each copy's camera skipping another team's possession the copies still part
   (ls.own_view, docs/plans/2026-10-14-online-solo-feel.md "Open"). RT_OWN_END=1 (checks) turns it on. */
static int enabled(void) {
    static int e = -1;
    if (e < 0) e = getenv("RT_OWN_END") != NULL;
    return e;
}
static int on(void) { return enabled() && ls_on() && !rt_ff; }

static int g_in_camera;     /* inside the camera's event listener or step */
static uint32_t g_ptd, g_seed;   /* the thread's _tiddata and its rand() seed when the camera began */
#define GUARD_LO 0x01409500u     /* the copy protection's countdowns */
#define GUARD_HI 0x0140AA80u
static uint8_t g_guard[GUARD_HI - GUARD_LO];

static uint32_t tiddata(void) {   /* FUN_00879BB1 (_getptd), registers kept: the camera's own call follows */
    uint32_t sa = g_eax, sc = g_ecx, sd = g_edx;
    uint32_t p = guest_call(0x00879BB1u, 0);
    g_eax = sa, g_ecx = sc, g_edx = sd;
    return p;
}

/* This player's team: 0 home, 1 away, -1 none (its controller in the middle: it watches). */
static int my_team(void) {
    int me = rt_net_slot();
    char s = me == rt_net_desk() && rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(me);
    return s == 'H' ? 0 : s == 'A' ? 1 : -1;
}

void own_end_begin(uint32_t kind) {
    dbg_camwrites_begin(kind);
    static int trace = -1;
    if (trace < 0) trace = getenv("RT_MS_RANDCAM") != NULL;
    if (on()) det_local_begin();
    if (!g_in_camera++ && (on() || trace)) {
        g_ptd = tiddata();
        g_seed = g_ptd ? rd32(g_ptd + 0x14) : 0;
        memcpy(g_guard, G2H(GUARD_LO), sizeof g_guard);
    }
}
void own_end_end(uint32_t kind) {
    dbg_camwrites_end(kind);
    if (g_in_camera) g_in_camera--;
    if (!g_in_camera && g_ptd) {
        if (rd32(g_ptd + 0x14) != g_seed) {
            rt_trace("ms: the camera moved the random seed at frame %u", rt_present_frame);
            if (on()) wr32(g_ptd + 0x14, g_seed);   /* (the shared sequence does not move) */
        }
        if (on()) memcpy(G2H(GUARD_LO), g_guard, sizeof g_guard);   /* (nor the protection's countdowns) */
    }
    if (!g_in_camera) g_ptd = 0;
    if (on()) det_local_end();
}

/* The camera's match event EVENT: 1 if this copy's camera must not hear it (another team's possession). */
int own_end_skip(uint32_t event) {
    static int said = -2;
    if (!on() || !event || rd32(event + 4) != EV_POSSESSION) return 0;
    int me = my_team(), what, t = ms_team_of(rd32(event + 8), &what);
    if (me != said) rt_trace("ms: own end team %d", me), said = me;
    if (me < 0 || t < 0 || t == me) return 0;
    rt_trace("ms: own end: the other team's ball at frame %u", rt_present_frame);
    return 1;
}
