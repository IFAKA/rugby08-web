/* ls_roles.c -- who drives what in a lockstep party, as online sports games do (the user, 2026-10-13):
 *   host     the shared menus are the host's: player 1, or the lowest player still in the room when
 *            player 1 has left. Other players' controllers do nothing there, except where every player
 *            picks a side for their own controller (Team Select screens). Their screens say the host is
 *            choosing; their Esc asks whether to leave the party (this game only, local).
 *   pause    the player who paused drives the pause menu; the others' screens say who paused.
 * Inputs are filtered where every game applies them (net_frame.c), from the roster and game state only,
 * so every game filters alike. What is drawn for this player only is in ui_badge.c (ls_roles_banner). */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "ls_roles.h"
#include "match_state.h"
#include "pad_attach.h"
#include <stdio.h>
#include <string.h>

#define GAME_PTR    0x00A7ED28u
#define VT_BACKEND  0x008CC6F0u
#define SCR_HUD     0x79       /* in play */
#define SCR_PAUSED  0x71
#define SCR_OPTIONS 0x58      /* a set piece's options: penalty (punt, kick at goal, scrum, run), quick options */
#define SC_ESCAPE   41         /* SDL scancode */

#define PAUSES    3           /* per player and match (online sports games: EA FC 26, 3 a player) */
#define PAUSE_MAX 3600        /* frames (60 s): then play resumes by itself */
#define FN_RESUME 0x00575F90u /* the pause menu's RESUME */

static int g_pauser = -1;     /* the room slot whose Start (or Esc) paused the match; -1: not paused by one */
static int g_paused;          /* the pause menu opened for g_pauser (else: its Start did not pause, yet) */
static int g_armed;           /* frames left for a Start to open the pause menu */
static int g_left[RT_MAX_PADS] = { PAUSES, PAUSES, PAUSES, PAUSES };
static uint32_t g_pause_frames;
static int g_leave_ask;       /* this player's "leave the party?" (local) */

int ls_host(void) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (rt_net_active(k)) return k;
    return 0;
}

static int in_match(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0;
    return st && rd32(st) == VT_BACKEND;
}

/* Screens where every player moves their own controller's icon to a side. */
static int sides_screen(int id) { return id == 0x6 || id == 0xF || id == 0x31D || id == 0x1A0A; }

int ls_roles_may_pause(int slot) {   /* a player with pauses left (no limit outside a party) */
    return !ls_on() || __builtin_popcount(ls_members()) < 2 || (slot >= 0 && slot < RT_MAX_PADS && g_left[slot] > 0);
}
void ls_roles_paused_by(int slot) {   /* pad_pause.c: Start, every game at the same frame */
    if (g_pauser < 0 && in_match()) g_pauser = slot, g_armed = 30;
}
/* Player 1's keyboard Esc pauses too: none once its pauses are used up. */
int ls_roles_desk_event(const plat_desk_ev_t *e) {
    if (!ls_on() || e->type != PLAT_DESK_KEY || e->x != SC_ESCAPE || !e->a || !in_match()) return 1;
    if (fe_screen_id() != SCR_HUD || g_pauser >= 0) return 1;
    if (!ls_roles_may_pause(0)) return 0;
    ls_roles_paused_by(0);
    return 1;
}

/* The pause: opened (counted), held at most PAUSE_MAX frames, over when play is back. */
static void pause_step(int match, int id) {
    if (!match) {
        g_pauser = -1, g_paused = 0;
        for (int k = 0; k < RT_MAX_PADS; k++) g_left[k] = PAUSES;   /* (a new match: pauses again) */
        return;
    }
    if (g_pauser < 0) return;
    if (!g_paused && id == SCR_PAUSED) {
        g_paused = 1, g_pause_frames = 0, g_left[g_pauser]--;
        rt_trace("ms: party paused by player %d (%d pauses left)", g_pauser + 1, g_left[g_pauser]);
    }
    if (!g_paused && --g_armed <= 0) g_pauser = -1;   /* its Start did not pause (a set piece's menu) */
    if (g_paused && id == SCR_HUD) g_pauser = -1, g_paused = 0;
    if (g_paused && ++g_pause_frames == PAUSE_MAX) guest_callv(FN_RESUME, 0, NULL);   /* time is up */
}
int ls_roles_pause_left(void) { return g_paused ? (int)(PAUSE_MAX - g_pause_frames) / 60 : -1; }

/* Every game, once the frame's inputs are in rt_pads: who may act now. Returns whether the keyboard and
   mouse (player 1's, applied as desk events) may. */
int ls_roles_filter(const plat_desk_ev_t *d, int nd) {
    if (!ls_on()) return 1;
    (void)d, (void)nd;
    int match = in_match(), id = fe_screen_id();
    pause_step(match, id);
    int only = -1;   /* the one slot whose controller acts, or -1: everyone */
    if (match && g_paused) only = g_pauser;
    else if (!match && fe_in_menus() && !sides_screen(id)) only = ls_host();
    if (only < 0) return 1;
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (k != only) {
            uint8_t plugged = rt_pads[k].spare;
            memset(&rt_pads[k], 0, sizeof rt_pads[k]);
            rt_pads[k].spare = plugged;
        }
    return only == 0;
}

/* A set piece's options (penalty, quick options: screen 0x58) are for the team with the ball: 1 if that
   is not this player's team (its screen shows the other team choosing, not their menu). */
static int others_choice(int id) {
    if (id != SCR_OPTIONS) return 0;
    ms_ball_t b;
    int me = rt_net_slot();
    char side = me == 0 && rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(me);
    if (!ms_ball(&b) || b.holder_team < 0 || !side) return 0;
    static int said;
    if (!said++) rt_trace("ms: party options for team %d, this player's side %c", b.holder_team, side);
    return (side == 'H') != (b.holder_team == 0);
}

/* This player's own view of the roles (local, drawn by ui_badge.c). */
int ls_roles_banner(char *line, int cap) {
    if (!ls_on() || __builtin_popcount(ls_members()) < 2) return 0;
    int me = rt_net_slot(), id = fe_screen_id();
    if (in_match() && others_choice(id))
        return snprintf(line, (size_t)cap, "THE OTHER TEAM IS CHOOSING"), LS_BANNER_COVER;
    if (in_match() && g_paused && g_pauser != me)
        return snprintf(line, (size_t)cap, "PAUSED BY PLAYER %d  %d S", g_pauser + 1, ls_roles_pause_left()),
               LS_BANNER_COVER;
    if (!in_match() && fe_in_menus() && ls_host() != me && !sides_screen(id)) {
        if (g_leave_ask) return snprintf(line, (size_t)cap, "LEAVE THE PARTY?  S: YES   ESC: NO"), LS_BANNER_COVER;
        return snprintf(line, (size_t)cap, "THE HOST IS CHOOSING   ESC: LEAVE THE PARTY"), LS_BANNER_VEIL;
    }
    return 0;
}

/* This player's own presses in the menus while another player is the host: Esc asks to leave, S says
   yes (the page starts a fresh own game). The presses still go out: the host's game ignores them. */
void ls_roles_local(const rt_pad_t *p) {
    static uint16_t was;
    uint16_t b = p->buttons, down = (uint16_t)(b & ~was);
    was = b;
    if (!ls_on() || in_match() || !fe_in_menus() || ls_host() == rt_net_slot() || sides_screen(fe_screen_id())) {
        g_leave_ask = 0;
        return;
    }
    if (down & RT_PAD_START) g_leave_ask = !g_leave_ask;
    else if (g_leave_ask && (down & RT_PAD_CROSS)) plat_session_leave();
}
