/* ls_roles.c -- who drives what in a lockstep party, as online sports games do (the user, 2026-10-13):
 *   host     the shared menus are the host's: the player in the party the longest (player 1 first,
 *            net_leave.c). Other players' controllers do nothing there, except where every player
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
#include "own_hud.h"
#include <stdio.h>
#include <string.h>

#define GAME_PTR    0x00A7ED28u
#define VT_BACKEND  0x008CC6F0u
#define SCR_HUD     0x79       /* in play */
#define SCR_PAUSED  0x71
#define SCR_QUICK   0x58      /* a set piece's quick options */
#define SCR_PENALTY 0x5F      /* penalty options (punt, kick at goal, scrum, run) */
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

int ls_host(void) { return rt_net_host(); }

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
    if (g_pauser < 0 && in_match()) g_pauser = slot, g_armed = 120;   /* (the menu opens ~32 frames later) */
}
/* The desk's keyboard Esc pauses too (its player's pause): none once its pauses are used up. */
int ls_roles_desk_event(const plat_desk_ev_t *e) {
    if (!ls_on() || e->type != PLAT_DESK_KEY || e->x != SC_ESCAPE || !e->a || !in_match()) return 1;
    if (fe_screen_id() != SCR_HUD || g_pauser >= 0) return 1;
    if (!ls_roles_may_pause(rt_net_desk())) return 0;
    ls_roles_paused_by(rt_net_desk());
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

/* The player whose controller acts in the menus now: the pauser in a pause, the host in the shared menus,
   else -1 (open play, Team Select: everyone). Settings changed then are that player's (own_camera.c). */
int ls_roles_actor(void) {
    if (!ls_on()) return -1;
    if (in_match()) return g_paused ? g_pauser : -1;
    return fe_in_menus() && !sides_screen(fe_screen_id()) ? ls_host() : -1;
}

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
    return only == rt_net_desk();
}

/* The side of slot K's player: its keyboard's if it has the desk, else its pad's ('H', 'A', 0). */
static char side_of(int k) { return k == rt_net_desk() && rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(k); }
int ls_team_of(int slot) { return side_of(slot) == 'H' ? 0 : side_of(slot) == 'A' ? 1 : -1; }
int ls_team_has_players(int team) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (rt_net_active(k) && ls_team_of(k) == team) return 1;
    return 0;
}

/* A set piece's options (penalty, quick options) are for one team (ms_setpiece_team: the game's own): if
   that is not this player's team, LINE says who of the players there is choosing; 0: it is this player's,
   or nobody's here (the AI's team, unknown). */
static int others_choice(int id, char *line, int cap) {
    if (id != SCR_QUICK && id != SCR_PENALTY) return 0;
    int t = ms_setpiece_team(), me = rt_net_slot(), n = 0, w = 0, from_acting = t < 0;
    if (from_acting) t = own_hud_acting();   /* (penalty options: the team rule is measured on quick options only) */
    char want = t == 0 ? 'H' : 'A', who[24] = "";
    if (t < 0 || !side_of(me) || side_of(me) == want) return 0;
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (rt_net_active(k) && side_of(k) == want && w < (int)sizeof who - 8)
            w += snprintf(who + w, sizeof who - (size_t)w, "%s%d", n++ ? " AND " : "", k + 1);
    if (!n) return 0;
    static int said = -1;
    if (said != t)
        rt_trace("ms: party options for team %d%s, this player's side %c", t, from_acting ? " (acting)" : "",
                 side_of(me)),
            said = t;
    snprintf(line, (size_t)cap, "PLAYER%s %s %s CHOOSING", n > 1 ? "S" : "", who, n > 1 ? "ARE" : "IS");
    return 1;
}

/* This player's own view of the roles (local, drawn by ui_badge.c). */
int ls_roles_banner(char *line, int cap) {
    if (!ls_on() || __builtin_popcount(ls_members()) < 2) return 0;
    int me = rt_net_slot(), id = fe_screen_id();
    if (in_match() && others_choice(id, line, cap)) return LS_BANNER_COVER;
    if (in_match() && g_paused && g_pauser != me)
        return snprintf(line, (size_t)cap, "PAUSED BY PLAYER %d  %d S", g_pauser + 1, ls_roles_pause_left()),
               LS_BANNER_COVER;
    if (!in_match() && fe_in_menus() && ls_host() != me && !sides_screen(id)) {
        if (g_leave_ask) return snprintf(line, (size_t)cap, "LEAVE THE PARTY?  S: YES   ESC: NO"), LS_BANNER_LOBBY;
        return snprintf(line, (size_t)cap, "THE HOST IS CHOOSING  -  ESC: LEAVE THE PARTY"), LS_BANNER_LOBBY;
    }
    return 0;
}

/* This player's own presses in the menus while another player is the host: Esc asks to leave, S says
   yes (this game plays on alone: net_leave.c). The presses still go out: the host's game ignores them. */
void ls_roles_local(const rt_pad_t *p) {
    static uint16_t was;
    uint16_t b = p->buttons, down = (uint16_t)(b & ~was);
    was = b;
    if (!ls_on() || in_match() || !fe_in_menus() || ls_host() == rt_net_slot() || sides_screen(fe_screen_id())) {
        g_leave_ask = 0;
        return;
    }
    if (down & RT_PAD_START) g_leave_ask = !g_leave_ask;
    else if (g_leave_ask && (down & RT_PAD_CROSS)) g_leave_ask = 0, rt_net_leave();
}
