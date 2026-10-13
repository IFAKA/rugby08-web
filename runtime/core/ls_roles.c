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

static int g_pauser = -1;     /* the room slot whose Start (or Esc) paused the match; -1: not paused by one */
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

void ls_roles_paused_by(int slot) {   /* pad_pause.c: Start, every game at the same frame */
    if (g_pauser < 0 && in_match()) g_pauser = slot;
}

/* Every game, once the frame's inputs are in rt_pads: who may act now. Returns whether the keyboard and
   mouse (player 1's, applied as desk events) may. */
int ls_roles_filter(const plat_desk_ev_t *d, int nd) {
    if (!ls_on()) return 1;
    int match = in_match(), id = fe_screen_id();
    if (!match || (g_pauser >= 0 && id == SCR_HUD)) g_pauser = -1;   /* play again (or no match) */
    if (match && g_pauser < 0 && rt_net_active(0))   /* player 1's Esc pauses too */
        for (int i = 0; i < nd; i++)
            if (d[i].type == PLAT_DESK_KEY && d[i].a && d[i].x == SC_ESCAPE && id == SCR_HUD) g_pauser = 0;
    int only = -1;   /* the one slot whose controller acts, or -1: everyone */
    if (match && g_pauser >= 0 && id != SCR_HUD) only = g_pauser;
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
    if (in_match() && g_pauser >= 0 && g_pauser != me && id != SCR_HUD)
        return snprintf(line, (size_t)cap, "PAUSED BY PLAYER %d", g_pauser + 1), LS_BANNER_COVER;
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
