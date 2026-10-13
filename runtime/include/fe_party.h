/* fe_party.h -- the party in the game's own menus (pt.menu): fe_screens.c builds the screens, fe_party.c
 * runs them. */
#pragma once
#include <stddef.h>
#include <stdint.h>

struct fe_node;
enum { FE_SCR_MAIN = 50, FE_SCR_ONLINE = 7100, FE_SCR_PARTY = 7101, FE_SCR_JOIN = 7102 };
#define FE_PARTY_ALPHABET "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" /* the server's codes (server/main.go newCode) */
#define FE_PARTY_LINES    7                                  /* info lines: title, code, status, 4 players */

/* fe_screens.c */
void fe_menu_edit(struct fe_node *screen);           /* the main menu as it loads: ONLINE second */
char *fe_party_screen_xml(int id, size_t *len);      /* our screen's XML (malloc'd), NULL if not ours */

/* fe_party.c */
int fe_online(void);                                 /* the ONLINE item exists (not in scripted runs) */
const char *const *fe_party_items(int id);           /* a screen's buttons, NULL-terminated */
uint32_t fe_party_action(uint32_t button);           /* game callback: one of our buttons was pressed */
void fe_party_present(uint32_t frame);               /* every Present */
int fe_party_key(int scancode, int down);            /* 1: typing the code took the key */
int fe_screen_id(void);                              /* the front end's current screen id, -1 none */
int fe_in_menus(void);                               /* the game is in its front end (not loading, not a match) */
void fe_go(int id);                                  /* FeSetCurrentScreen(id) */
