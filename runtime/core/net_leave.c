/* net_leave.c -- a lockstep party's host, and leaving the party as in online sports games (the user,
 * 2026-10-14): the player who leaves stays in its own game, at its main menu; nothing restarts.
 *   host     the player in the party the longest (the room's creator first): it drives the shared menus
 *            (ls_roles.c), and in the menus its keyboard and mouse are the game's (the desk, net_frame.c).
 *            In a match the desk stays with whoever had it (a friend's keyboard keeps playing its own side).
 *            From the roster and the game's state only: the same in every game.
 *   leave    LEAVE PARTY: the server gives this game a room of its own with the party's log and every other
 *            slot leaving at frame G (server/fork.go). This game waits at G for those 'D's and plays on alone,
 *            open to friends (a new code), and goes to its main menu through its own input (a desk event:
 *            a friend joining later replays it too). The party goes on without it.
 *   offline  the connection lost (or no answer to the leave): this game plays on alone (netplay.c). */
#include "netplay_int.h"
#include "fe_party.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FORK_WAIT_MS 5000

static int g_left;   /* this game left the party (1) or lost it (2): told once (rt_net_left) */
int rt_net_left(void) {
    int l = g_left;
    g_left = 0;
    return l;
}
void net_left(int how) { g_left = how; }

int rt_net_host(void) {
    int best = -1;
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (N.active[k] && (best < 0 || N.stint[k] < N.stint[best])) best = k;
    return best < 0 ? N.slot : best;
}

/* The slot whose desk events (keyboard and mouse) the game takes: every game, at each applied frame. */
int rt_net_desk(void) { return N.desk; }
void net_desk_at(uint32_t frame) {
    int h = rt_net_host();
    if (h == N.desk || !fe_in_menus()) return;
    rt_trace("netplay: player %d has the keyboard and mouse from frame %u", h + 1, frame);
    N.desk = h;
}

void rt_net_leave(void) {
    if (!rt_net || rt_net_ha || N.offline || N.leaving) return;
    rt_trace("netplay: leaving the party");
    N.leaving = 1;   /* at the next frame, before its inputs are applied (net_leave_step) */
}

static uint64_t wall_ms(void) {   /* (only how long to wait for the server, never game state) */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* net_frame, before the frame's inputs are waited for: this game has played up to FRAME - 1. */
void net_leave_step(uint32_t frame) {
    if (N.leaving != 1) return;
    char m[64];
    int n = snprintf(m, sizeof m, "{\"t\":\"fork\",\"frame\":%u}", frame);
    net_tx_send(m, n, 1);
    N.leaving = 2;
    uint64_t t0 = wall_ms();
    while (!N.forked && !N.offline && wall_ms() - t0 < FORK_WAIT_MS) {
        if (net_tx_forked()) N.forked = 1;
        net_pump(5);
    }
    N.leaving = 0;
    if (!N.forked) {   /* (an old server, or none): alone, offline */
        rt_trace("netplay: no room of its own from the server: playing on offline");
        net_go_offline();
        g_left = 1;
        return;
    }
    N.forked = 0;
    net_pump(0);   /* (the 'D's came before the answer) */
    N.auth = 1;    /* its own room: it admits newcomers */
    memset(N.want, 0, sizeof N.want);
    N.go_menu = 1;
    rt_trace("netplay: left the party: playing on in room %s from frame %u", plat_session_code(), frame);
    g_left = 1;
}

/* The desk event that takes this game to its main menu after a leave (net_frame.c send_input). */
int net_leave_menu_event(plat_desk_ev_t *e) {
    if (!N.go_menu) return 0;
    N.go_menu = 0;
    *e = (plat_desk_ev_t){ NET_DESK_MENU, 0, 0, 0 };
    return 1;
}
void net_leave_menu_apply(void) {   /* every game of the room, at the frame it was sent for */
    rt_trace("netplay: to the main menu (a leave)");
    if (fe_in_menus()) fe_go(FE_SCR_MAIN);
}
