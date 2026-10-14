/* fe_party.c -- what the party's screens do (pt.menu; built by fe_screens.c). ONLINE in the main menu:
 * CREATE PARTY (the code to give friends, the players, CONTINUE to the menus or LEAVE PARTY), JOIN PARTY
 * (the code typed on a letter grid with pad, mouse or touch, or on the keyboard), and a friend's lobby
 * (the code, the players, what the host is doing, LEAVE PARTY). Buttons are the game's own (its
 * navigation, mouse picking, highlight and sounds); their X press comes here by the button's text.
 * The info lines are rewritten at every Present through the game's own text setter. Rooms: ui_lobby.c.
 * Trace: "ms: party screen ID", "ms: party action TEXT", "ms: party code TYPED". */
#include "rt.h"
#include "../debug/debug.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "own_camera.h"
#include "match_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FE_PTR      0x013834E8u /* the front end: +4 its current FeScreen (id word at +4) */
#define GAME_PTR    0x00A7ED28u
#define VT_FRONTEND 0x008CD620u
#define SET_SCREEN  0x004F1380u /* FeSetCurrentScreen(id, 1) */
#define SET_TEXT    0x0083BAB0u /* thiscall on a TextButton's +0x158: its text (the game keeps a copy) */
#define BTN_TEXT    0x158u
#define INFO_WINDOW 3
enum { SC_A = 4, SC_Z = 29, SC_1 = 30, SC_0 = 39, SC_RETURN = 40, SC_BACKSPACE = 42, SC_KP_ENTER = 88 };

static char g_typed[8], g_status[64];
static uint8_t g_mine[512];

int fe_online(void) {
    static int on = -1;
    if (on < 0) on = getenv("RT_ONLINE") ? atoi(getenv("RT_ONLINE")) != 0 : !rt_replay_path[0];
    return on;
}

int fe_screen_id(void) {
    uint32_t fe = rd32(FE_PTR), s = fe ? rd32(fe + 4) : 0;
    return s ? rd16(s + 4) : -1;
}

int fe_in_menus(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0;
    return st && rd32(st) == VT_FRONTEND && fe_screen_id() >= 0;
}

/* A friend's lobby: what the host is doing, from its front-end screen (docs/PARTY_RESEARCH.md "Modes"). */
static const char *host_doing(void) {
    int s = net_ha_host_screen();
    if (s < 0) return "WAITING FOR THE HOST...";
    if (s == 0) return "THE HOST IS IN A MATCH";
    if (s == 6 || s == 0x15F) return "THE HOST IS PICKING TEAMS (PLAY NOW)";
    if (s == 7) return "THE HOST IS PICKING THE STADIUM";
    if (s == 10 || s == 0xB || s == 0x78) return "THE HOST IS CHOOSING A GAME MODE";
    if (s == 14) return "THE HOST IS IN MY RUGBY";
    if (s == 0xF || s == 0x31D) {   /* a fixture's Team Select: the host's screen, this player's side */
        int m = party_sides_mine();
        return m == 'H'   ? "TEAM SELECT: YOU PLAY HOME (LEFT/RIGHT TO MOVE)"
               : m == 'A' ? "TEAM SELECT: YOU PLAY AWAY (LEFT/RIGHT TO MOVE)"
                          : "TEAM SELECT: PRESS LEFT OR RIGHT TO PLAY";
    }
    if ((s >= 0x79 && s <= 0x13A) || s == 0x15E) return "THE HOST IS IN A TOURNAMENT";
    if (s >= 0x320 && s <= 0x335) return "THE HOST IS IN CHALLENGE MODE";
    return "THE HOST IS IN THE MENUS";
}

void fe_go(int id) {
    rt_trace("ms: party screen %d", id);
    guest_call(SET_SCREEN, 2, (uint32_t)id, 1u);
}

static int in_party(void) {
    if (ls_on()) return (ls_members() & (ls_members() - 1)) != 0;   /* lockstep: someone else plays here */
    return ui_lobby_state() == LOBBY_IN || ui_lobby_state() == LOBBY_WAITING;
}

const char *const *fe_party_items(int id) {
    static const char *const online[] = { "CREATE PARTY", "JOIN PARTY", "BACK", NULL };
    static const char *const host[] = { "CONTINUE", "LEAVE PARTY", NULL };
    static const char *const friend_[] = { "LEAVE PARTY", NULL };
    static const char *const none[] = { NULL };
    if (id == FE_SCR_ONLINE) return online;
    if (id == FE_SCR_PARTY) return !ls_on() && ui_lobby_state() == LOBBY_IN && !ui_lobby_is_host() ? friend_ : host;
    return none;
}

static void type(char c) {
    size_t n = strlen(g_typed);
    if (c && n < 6) g_typed[n] = c, g_typed[n + 1] = 0;
    if (!c && n) g_typed[n - 1] = 0;
    g_status[0] = 0;
    rt_trace("ms: party code %s", g_typed);
}

static void join(void) {
    if (strlen(g_typed) != 6) {
        snprintf(g_status, sizeof g_status, "THE CODE HAS 6 LETTERS");
        return;
    }
    snprintf(g_status, sizeof g_status, "JOINING...");
    if (ls_on()) ls_join(ls_sender(), g_typed);
    else ui_lobby_join(g_typed);
}

uint32_t fe_party_action(uint32_t button) {
    const char *t = button ? gstr(rd32(button + BTN_TEXT)) : NULL;
    int at = fe_screen_id();
    if (!t) return 4;
    rt_trace("ms: party action %s on screen %d", t, at);
    if (!strcmp(t, "ONLINE")) fe_go(in_party() ? FE_SCR_PARTY : FE_SCR_ONLINE);
    else if (!strcmp(t, "CREATE PARTY")) {
        if (!ls_on() && !in_party()) ui_lobby_host();
        fe_go(FE_SCR_PARTY);
    } else if (!strcmp(t, "JOIN PARTY")) {
        g_typed[0] = g_status[0] = 0;
        fe_go(FE_SCR_JOIN);
    } else if (!strcmp(t, "BACK")) fe_go(at == FE_SCR_JOIN ? FE_SCR_ONLINE : FE_SCR_MAIN);
    else if (!strcmp(t, "CONTINUE")) fe_go(FE_SCR_MAIN);
    else if (!strcmp(t, "LEAVE PARTY")) {
        if (ls_on()) ls_leave(ls_sender());
        else ui_lobby_leave();
        fe_go(FE_SCR_MAIN);
    } else if (!strcmp(t, "DEL")) type(0);
    else if (!strcmp(t, "OK")) join();
    else if (at == FE_SCR_JOIN && strlen(t) == 1) type(t[0]);
    return 0;   /* handled (the game plays its accept sound) */
}

/* the object with this id in a FE list ({+4 id, +8 object, +0x10 next}, circular) */
static uint32_t find(uint32_t head, int id) {
    for (uint32_t n = head; n; n = rd32(n + 0x10)) {
        uint32_t o = rd32(n + 8);
        if (o && rd16(o + 4) == id) return o;
        if (rd32(n + 0x10) == head) break;
    }
    return 0;
}

/* A line's text, through the game's own setter when it changed. Every game of a party writes the same
   text at the same frames: the game's memory (and the code drawing it) stays alike in all of them, so the
   lockstep clock does too. What is this player's own ("YOU") is drawn over it (ui_badge.c). */
static void set_text(uint32_t screen, int id, const char *s) {
    static uint32_t buf;
    static char last[FE_PARTY_LINES][64];
    static uint32_t last_screen;
    if (screen != last_screen) memset(last, 0, sizeof last), last_screen = screen;
    if (id < 0 || id >= FE_PARTY_LINES || !strcmp(last[id], s)) return;
    if (id >= 2) rt_trace(id == 2 ? "ms: party status %s" : "ms: party row %s", s);
    uint32_t w = find(screen + 0x10, INFO_WINDOW), b = w ? find(w + 0xA0, id) : 0;
    if (!b || (!buf && !(buf = gcalloc(64)))) return;
    snprintf(last[id], sizeof last[id], "%s", s);
    snprintf(G2H(buf), 64, "%s", s[0] ? s : " ");
    uint32_t sv = g_ecx;
    g_ecx = b + BTN_TEXT;
    guest_call(SET_TEXT, 1, buf);
    g_ecx = sv;
}

/* JOIN PARTY accepted (lockstep): this game shows that party's screen until the page reloads into it
   (platform/web/join_view.c); SLOT, PLAYERS, CODE (16) as the server said, when not NULL. */
static int previewing(int *slot, uint32_t *players, char *code) {
    int s;
    uint32_t p;
    char c[16];
    return ls_on() && plat_join_preview(slot ? slot : &s, players ? players : &p, code ? code : c, 16);
}

int fe_party_joining(int *slot) { return previewing(slot, NULL, NULL); }

/* lockstep: the code and the roster, the same text in every game (this player's "YOU": ui_badge.c). */
static void ls_lines(uint32_t screen) {
    char l[64], pc[16];
    uint32_t mask = ls_members(), was;
    int slot = rt_net_slot();
    if (previewing(&slot, &was, pc)) mask = was | 1u << slot;   /* (before the reload: no game state shared) */
    const char *code = previewing(NULL, NULL, NULL) ? pc : ls_code();
    int host = previewing(NULL, NULL, NULL) ? 0 : rt_net_host();   /* (the roster: alike in every game) */
    set_text(screen, 0, " ");
    set_text(screen, 1, code[0] ? code : "......");
    set_text(screen, 2, code[0] ? "GIVE THIS CODE TO YOUR FRIENDS" : "PLAYING OFFLINE");
    for (int k = 0; k < 4; k++) {
        if (mask >> k & 1) snprintf(l, sizeof l, "%d  PLAYER %d%s", k + 1, k + 1, k == host ? " (HOST)" : "");
        else snprintf(l, sizeof l, "%d  WAITING...", k + 1);
        set_text(screen, 3 + k, l);
    }
}

static void party_lines(uint32_t screen) {
    char l[64];
    if (ls_on()) {
        ls_lines(screen);
        return;
    }
    int st = ui_lobby_state(), host = st != LOBBY_IN || ui_lobby_is_host();
    uint32_t mask = ui_lobby_member_mask();
    set_text(screen, 0, " ");
    set_text(screen, 1, st == LOBBY_IN ? ui_lobby_code() : "......");
    set_text(screen, 2,
             st == LOBBY_ERROR ? ui_lobby_error()
             : st != LOBBY_IN  ? "CONNECTING..."
             : host            ? "GIVE THIS CODE TO YOUR FRIENDS"
                               : host_doing());
    for (int k = 0; k < 4; k++) {
        int me = st == LOBBY_IN && k == ui_lobby_slot();
        if (st != LOBBY_IN) snprintf(l, sizeof l, " ");
        else if (!(mask >> k & 1)) snprintf(l, sizeof l, "%d  WAITING...", k + 1);
        else snprintf(l, sizeof l, "%d  %s%s", k + 1, k == 0 ? "HOST" : "PLAYER", me ? " (YOU)" : "");
        set_text(screen, 3 + k, l);
    }
}

void fe_party_present(uint32_t frame) {
    dbg_render_writes_end(frame);   /* (RT_RENDER_WRITES: what the drawing wrote) */
    own_camera_options();   /* camera settings changed in the shared menus: this player's only (own_camera.c) */
    own_camera_present();   /* drawn: the match's camera, alike in every game, for the match's next step */
    own_end_present();      /* this player's end of the pitch, drawn only (own_end.c) */
    if (!ls_on()) {   /* host-authoritative party (lockstep: every game runs the same screens itself) */
        ui_lobby_poll();
        party_sides_present(frame);   /* (both also in a match: they check where the game is themselves) */
        party_leave_present(frame);
    }
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, fe = rd32(FE_PTR), screen = fe ? rd32(fe + 4) : 0;
    if (!st || rd32(st) != VT_FRONTEND || !screen) return;
    float px, py;   /* the menus' cursor where the pointer is (the browser: mouse and touch) */
    uint32_t cur = rd32(fe + 0x31488);
    if (plat_pointer(&px, &py) && cur) {
        memcpy(G2H(cur + 8), &px, 4);
        memcpy(G2H(cur + 12), &py, 4);
    }
    int id = rd16(screen + 4);
    static uint32_t asked;   /* the JOIN screen we already moved on from (the switch takes frames) */
    if (id == FE_SCR_JOIN) {
        if (ui_lobby_state() == LOBBY_IN || previewing(NULL, NULL, NULL)) {
            if (asked != screen) fe_go(FE_SCR_PARTY);
            asked = screen;
            return;
        }
        if (ui_lobby_state() == LOBBY_ERROR) snprintf(g_status, sizeof g_status, "%s", ui_lobby_error());
        char shown[16];
        for (int i = 0; i < 6; i++) shown[i] = g_typed[i] ? g_typed[i] : '_';
        shown[6] = 0;
        set_text(screen, 0, " ");
        set_text(screen, 1, shown);
        set_text(screen, 2, g_status[0] ? g_status : "TYPE OR PICK YOUR FRIEND'S CODE");
    } else if (id == FE_SCR_PARTY) {
        party_lines(screen);
        static int settled;   /* the party being joined: its screen, once in (60 frames), is kept by the page */
        int x, y, by;
        fe_party_layout(0, &x, &y, &by);
        if (previewing(NULL, NULL, NULL) && ++settled == 60) plat_join_snapshot(x);
    }
}

/* The keyboard on JOIN PARTY: letters and digits of the code, Backspace (the game sees none of them). */
int fe_party_key(int sc, int down) {
    if (sc < 0 || sc >= 512) return 0;
    if (!down) {
        int mine = g_mine[sc];
        g_mine[sc] = 0;
        return mine;
    }
    if (rt_net_ha == 2 && fe_screen_id() == 6 && fe_in_menus()) { /* a friend's Team Select: its keys go to the host */
        g_mine[sc] = 1;
        return 1;
    }
    if (fe_screen_id() != FE_SCR_JOIN) return 0;
    char c = sc >= SC_A && sc <= SC_Z ? (char)('A' + sc - SC_A) : sc >= SC_1 && sc < SC_0 ? (char)('1' + sc - SC_1) : 0;
    if (c && !strchr(FE_PARTY_ALPHABET, c)) c = 0;
    if (sc == SC_RETURN || sc == SC_KP_ENTER) join(); /* the typed code */
    else if (c || sc == SC_BACKSPACE) type(c);
    else return 0;
    g_mine[sc] = 1;
    return 1;
}
