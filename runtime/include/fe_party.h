/* fe_party.h -- the party in the game's own menus (pt.menu): fe_screens.c builds the screens, fe_party.c
 * runs them. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct fe_node;
enum { FE_SCR_MAIN = 50, FE_SCR_ONLINE = 7100, FE_SCR_PARTY = 7101, FE_SCR_JOIN = 7102 };
#define FE_PARTY_ALPHABET "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" /* the server's codes (server/main.go newCode) */
#define FE_PARTY_LINES    7                                  /* info lines: title, code, status, 4 players */

/* fe_screens.c */
void fe_menu_edit(struct fe_node *screen);           /* the main menu as it loads: ONLINE second */
char *fe_party_screen_xml(int id, size_t *len);      /* our screen's XML (malloc'd), NULL if not ours */
void fe_party_layout(int line, int *x, int *y, int *buttons_y); /* PARTY: where things are (640 x 480) */

/* fe_party.c */
int fe_online(void);                                 /* the ONLINE item exists (not in scripted runs) */
const char *const *fe_party_items(int id);           /* a screen's buttons, NULL-terminated */
uint32_t fe_party_action(uint32_t button);           /* game callback: one of our buttons was pressed */
void fe_party_present(uint32_t frame);               /* every Present */
/* a draw that is the menus' cursor on a friend's screen: where to move it (ls_cursor.c), on its GPU only */
int ls_cursor_shift(const float at[2], uint32_t prims, uint32_t stride, const void *verts, float *dx, float *dy);
int fe_party_joining(int *slot);                     /* JOIN accepted, before the reload: as SLOT (join_view.c) */
int fe_party_key(int scancode, int down);            /* 1: typing the code took the key */
int fe_screen_id(void);                              /* the front end's current screen id, -1 none */
int fe_in_menus(void);                               /* the game is in its front end (not loading, not a match) */
void fe_go(int id);                                  /* FeSetCurrentScreen(id) */

/* ls_party.c: the party in lockstep (the session is the party) */
int ls_on(void);                                     /* a lockstep session runs */
uint32_t ls_members(void);                           /* bit k: room slot k plays (the roster: every game alike) */
const char *ls_code(void);                           /* the room's code */
int ls_sender(void);                                 /* the room slot whose controller the menus handle */
void ls_join(int sender, const char *code);          /* JOIN PARTY: that player's page joins the room */
void ls_leave(int sender);                           /* LEAVE PARTY: that player's page starts its own game */

/* party_sides.c: Team Select on every party member's screen */
enum { PARTY_TS_SIDE, PARTY_TS_HOST };
int party_ts_allow(int what);                        /* the game's Team Select callbacks ask */
void party_sides_rx(const uint8_t *p, size_t len);   /* friend: the host's 'T' */
int party_sides_mine(void);                          /* friend: its side there ('H', 'A', 0; -1 none yet) */
void party_sides_present(uint32_t frame);

/* party_leave.c: leaving, the host ending the party, the messages everyone sees */
void party_leave_present(uint32_t frame);
void party_leave_quit(uint32_t frame);
void party_rebuild_teams(void);
/* the teams' player controls from the controllers' sides */ /* a friend left the host's match by itself */
void party_toast(uint32_t frame, const char *s);
const char *party_toast_text(uint32_t frame); /* NULL: none now */
#ifdef __cplusplus
}
#endif
