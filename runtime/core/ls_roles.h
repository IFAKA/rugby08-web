/* ls_roles.h -- who drives what in a lockstep party (ls_roles.c). */
#ifndef LS_ROLES_H
#define LS_ROLES_H
#include "plat.h"

enum {
    LS_BANNER_VEIL = 1,
    LS_BANNER_COVER = 2,
    LS_BANNER_LOBBY = 3,
    LS_BANNER_STRIP = 4   /* a line at the bottom, the screen as it is */
};   /* over a dimmed / covered screen;
                                                                         the party view (ui_party_view.c) */
int ls_host(void);                                    /* the room slot that drives the shared menus */
void ls_roles_paused_by(int slot);                    /* that slot's Start paused the match */
int ls_roles_may_pause(int slot);                     /* that slot has pauses left (3 a match in a party) */
int ls_roles_desk_event(const plat_desk_ev_t *e);     /* player 1's desk event may act (its Esc: a pause) */
int ls_roles_pause_left(void);                        /* seconds the pause has left, -1: not paused */
int ls_roles_actor(void);                             /* the slot acting in the menus now, -1: everyone */
int ls_roles_filter(const plat_desk_ev_t *d, int nd); /* every game: other slots' pads off; 1: desk may act */
int ls_roles_banner(char *line, int cap);             /* this player's banner (LS_BANNER_*), 0: none */
void ls_roles_local(const rt_pad_t *p);               /* this player's own presses (leave the party?) */
void ls_ready_frame(int sides, const plat_desk_ev_t *d, int nd, char (*side_of)(int)); /* (ls_ready.c) */
int ls_ready_desk(const plat_desk_ev_t *e);           /* the desk's event may reach the game (Team Select) */
int ls_ready_banner(char *line, int cap);             /* this player's line on Team Select */
int ls_team_of(int slot);                             /* that slot's team: 0 home, 1 away, -1 none (watching) */
int ls_team_has_players(int team);                    /* a party player is on that team */
#endif
