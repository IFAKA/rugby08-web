/* ls_ready.c -- Team Select in a party, as online sports games do it (the user, 2026-10-21): every player
 * confirms that they are ready (S or Enter, A on a pad); the host's confirm moves the party on only once
 * every player is ready, and a host who confirmed first waits for the rest. Moving to another side takes
 * a player's ready back. The game sees one confirm: the host's S, pressed for it when the last player is
 * ready. From the frame's applied inputs only (ls_roles_filter): every game alike. */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "ls_roles.h"
#include <stdio.h>
#include <string.h>

#define SC_S        22   /* the game's keyboard accept (docs/CONTROLS.md) */
#define SC_RETURN   40
#define SC_KP_ENTER 88

static int g_screen = -1;            /* the sides screen the readies are for, -1 none */
static int g_ready[RT_MAX_PADS];
static uint16_t g_was[RT_MAX_PADS];  /* each slot's buttons last frame (presses, not holds) */
static char g_side[RT_MAX_PADS];
static int g_release;                /* the confirm pressed for the host is down: up next frame */

static int is_confirm(const plat_desk_ev_t *e) {
    return e->type == PLAT_DESK_KEY && e->a && (e->x == SC_S || e->x == SC_RETURN || e->x == SC_KP_ENTER);
}

static int all_ready(void) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (rt_net_active(k) && !g_ready[k]) return 0;
    return 1;
}

static void confirm(int down) {
    plat_desk_ev_t e = { PLAT_DESK_KEY, (uint8_t)down, SC_S, 0 };
    plat_desk_apply(&e);
}

/* Every game, each frame, once the pads are in: SIDES the sides screen shown (-1: none), D the desk's
   events, SIDE_OF each slot's side. */
void ls_ready_frame(int sides, const plat_desk_ev_t *d, int nd, char (*side_of)(int)) {
    if (g_release) confirm(0), g_release = 0;
    if (sides != g_screen) {   /* (a new screen: nobody ready yet, held buttons are not presses) */
        g_screen = sides;
        memset(g_ready, 0, sizeof g_ready);
        for (int k = 0; k < RT_MAX_PADS; k++) g_was[k] = rt_pads[k].buttons, g_side[k] = side_of(k);
    }
    if (sides < 0 || __builtin_popcount(ls_members()) < 2) return;
    int host = rt_net_desk(), host_now = 0;
    for (int i = 0; i < nd; i++) host_now |= is_confirm(&d[i]);
    for (int k = 0; k < RT_MAX_PADS; k++) {
        uint16_t down = (uint16_t)(rt_pads[k].buttons & ~g_was[k]);
        g_was[k] = rt_pads[k].buttons;
        rt_pads[k].buttons &= (uint16_t)~RT_PAD_CROSS;   /* (the game's confirm is the host's Enter alone) */
        if (!rt_net_active(k)) continue;
        if (side_of(k) != g_side[k]) {
            g_side[k] = side_of(k);
            if (g_ready[k]) rt_trace("ms: ls ready player %d moved: not ready", k + 1), g_ready[k] = 0;
        }
        int now = (down & RT_PAD_CROSS) || (k == host && host_now);
        if (now && !g_ready[k]) g_ready[k] = 1, rt_trace("ms: ls ready player %d", k + 1);
    }
    if (!g_ready[host] || !all_ready()) return;
    rt_trace("ms: ls ready all: the host's confirm goes on");
    confirm(1), g_release = 1;
    memset(g_ready, 0, sizeof g_ready);   /* (the screen may stay: confirm again) */
}

/* The desk's event E: whether it reaches the game (on a sides screen the host's confirm is pressed for it
   once every player is ready). */
int ls_ready_desk(const plat_desk_ev_t *e) {
    return g_screen < 0 || __builtin_popcount(ls_members()) < 2 || !is_confirm(e);
}

/* This player's line on a sides screen (drawn for this player only: ui_badge.c), 0: none. */
int ls_ready_banner(char *line, int cap) {
    if (g_screen < 0 || __builtin_popcount(ls_members()) < 2) return 0;
    int me = rt_net_slot(), n = 0, w = 0;
    char who[32] = "";
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (rt_net_active(k) && !g_ready[k] && k != me && w < (int)sizeof who - 8)
            w += snprintf(who + w, sizeof who - (size_t)w, "%s%d", n++ ? " AND " : "", k + 1);
    if (!g_ready[me]) snprintf(line, (size_t)cap, "PRESS S OR ENTER WHEN READY");
    else if (n) snprintf(line, (size_t)cap, "READY - WAITING FOR PLAYER%s %s", n > 1 ? "S" : "", who);
    else snprintf(line, (size_t)cap, "READY - WAITING FOR THE HOST");
    return LS_BANNER_STRIP;
}
