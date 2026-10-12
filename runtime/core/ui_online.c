/* ui_online.c -- the in-game Online screens (on.ingame_ui), in every game. F2 opens them over whatever the
 * game shows: ONLINE (HOST A GAME / JOIN A GAME / BACK), the room code to give friends, typing a friend's
 * code; closed, a badge shows the room. Keys pressed while they are open never reach the game (a key
 * the game saw go down still reaches it when it goes up). Rooms: ui_lobby.c. Drawing: an RGBA image of
 * the back buffer's size (render/webgl/overlay.cpp). Checks read "ms: ui" trace lines. */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_online = 1;   /* RT_ONLINE=0: without the screens (checks of the original game) */

enum { CLOSED, MENU, HOSTING, JOIN };
enum { SC_A = 4, SC_Z = 29, SC_1 = 30, SC_0 = 39, SC_RETURN = 40, SC_ESC = 41, SC_BACKSPACE = 42, SC_F2 = 59 };
enum { SC_DOWN = 81, SC_UP = 82, SC_S = 22, SC_W = 26, SC_KP_ENTER = 88 };

static int g_screen = CLOSED, g_sel, g_dirty = 1;
static char g_typed[8];
static uint8_t g_mine[512];   /* keys whose press the screens took */
static ui_image_t g_img;

static const char *const k_menu[] = { "HOST A GAME", "JOIN A GAME", "BACK" };

static void go(int screen) {
    g_screen = screen;
    g_sel = 0;
    g_dirty = 1;
    rt_trace("ms: ui screen %d", screen);
}

static void menu_key(int sc) {
    if (sc == SC_UP || sc == SC_DOWN) g_sel = (g_sel + (sc == SC_UP ? 2 : 1)) % 3;
    if (sc == SC_ESC || sc == SC_W || ((sc == SC_RETURN || sc == SC_S || sc == SC_KP_ENTER) && g_sel == 2)) go(CLOSED);
    else if ((sc == SC_RETURN || sc == SC_S || sc == SC_KP_ENTER) && g_sel == 0) {
        if (ui_lobby_state() != LOBBY_IN) ui_lobby_host();
        go(HOSTING);
    } else if ((sc == SC_RETURN || sc == SC_S || sc == SC_KP_ENTER) && g_sel == 1) {
        g_typed[0] = 0;
        go(JOIN);
    }
}

static void join_key(int sc) {
    size_t n = strlen(g_typed);
    char c = sc >= SC_A && sc <= SC_Z ? (char)('A' + sc - SC_A) : sc >= SC_1 && sc < SC_0 ? (char)('1' + sc - SC_1) : 0;
    if (sc == SC_0) c = '0';
    if (c && n < 6) g_typed[n] = c, g_typed[n + 1] = 0;
    if (sc == SC_BACKSPACE && n) g_typed[n - 1] = 0;
    if (sc == SC_ESC) go(MENU);
    if ((sc == SC_RETURN || sc == SC_KP_ENTER) && n == 6) ui_lobby_join(g_typed);
}

int ui_key(int sc, int down) {
    if (!rt_online || sc < 0 || sc >= 512) return 0;
    if (!down) {   /* only the releases of presses the screens took */
        int mine = g_mine[sc];
        g_mine[sc] = 0;
        return mine;
    }
    if (g_screen == CLOSED && sc != SC_F2) return 0;
    g_mine[sc] = 1;
    g_dirty = 1;
    if (sc == SC_F2) go(g_screen == CLOSED ? (ui_lobby_state() == LOBBY_IN ? HOSTING : MENU) : CLOSED);
    else if (g_screen == MENU) menu_key(sc);
    else if (g_screen == JOIN) join_key(sc);
    else if (g_screen == HOSTING && (sc == SC_ESC || sc == SC_W)) go(CLOSED);
    else if (g_screen == HOSTING && (sc == SC_RETURN || sc == SC_S) && ui_lobby_state() == LOBBY_IN) {
        ui_lobby_leave();
        go(MENU);
    }
    return 1;
}

int ui_open(void) { return rt_online && g_screen != CLOSED; }

static void centred(int y, int scale, const char *s, uint32_t rgba) {
    ui_text(&g_img, (g_img.w - ui_text_width(s, scale)) / 2, y, scale, s, rgba);
}

static void draw_room(void) {
    char line[48];
    int st = ui_lobby_state();
    if (st == LOBBY_WAITING) centred(220, 2, "CONNECTING...", UI_WHITE);
    if (st == LOBBY_ERROR) centred(220, 2, ui_lobby_error(), UI_WHITE);
    if (st != LOBBY_IN) return;
    centred(190, 2, ui_lobby_is_host() ? "YOUR CODE" : "IN THE ROOM", UI_GREY);
    const char *code = ui_lobby_code();
    int x = (g_img.w - ui_text_width(code, 5)) / 2;
    ui_text(&g_img, x, 220, 5, code, UI_GOLD);
    rt_trace("ms: ui text \"%s\" %d,%d,%d,%d", code, x, 220, ui_text_width(code, 5), 35);
    snprintf(line, sizeof line, "PLAYERS %d/4", ui_lobby_members());
    centred(275, 2, line, UI_WHITE);
    centred(315, 2, "LEAVE", UI_GOLD);
}

static void draw(void) {
    memset(g_img.px, 0, (size_t)g_img.w * (size_t)g_img.h * 4);
    if (g_screen == CLOSED) {   /* the badge */
        if (ui_lobby_state() != LOBBY_IN) return;
        char b[48];
        snprintf(b, sizeof b, "ONLINE %s %d/4", ui_lobby_code(), ui_lobby_members());
        int w = ui_text_width(b, 2) + 16;
        ui_rect(&g_img, g_img.w - w - 8, 8, w, 28, UI_PANEL);
        ui_text(&g_img, g_img.w - w, 15, 2, b, UI_WHITE);
        return;
    }
    ui_rect(&g_img, 100, 110, g_img.w - 200, 260, UI_PANEL);
    centred(130, 3, "ONLINE", UI_WHITE);
    if (g_screen == MENU)
        for (int i = 0; i < 3; i++) centred(200 + 40 * i, 2, k_menu[i], i == g_sel ? UI_GOLD : UI_WHITE);
    if (g_screen == HOSTING) draw_room();
    if (g_screen == JOIN) {
        char shown[8];
        for (int i = 0; i < 6; i++) shown[i] = g_typed[i] ? g_typed[i] : '_';
        shown[6] = 0;
        centred(190, 2, "TYPE YOUR FRIEND'S CODE", UI_GREY);
        centred(220, 5, shown, UI_GOLD);
        if (ui_lobby_state() == LOBBY_WAITING || ui_lobby_state() == LOBBY_ERROR) draw_room();
        else centred(300, 2, "RETURN: JOIN   ESC: BACK", UI_WHITE);
    }
}

int ui_present(uint32_t frame, int w, int h, const uint8_t **img, int *changed) {
    (void)frame;
    if (!rt_online || w <= 0 || h <= 0) return 0;
    ui_lobby_poll();
    static int last_state = -1, last_members = -1;
    if (ui_lobby_state() != last_state || ui_lobby_members() != last_members) g_dirty = 1;
    if (ui_lobby_state() == LOBBY_IN && g_screen == JOIN) go(HOSTING);   /* joined: show the room */
    last_state = ui_lobby_state();
    last_members = ui_lobby_members();
    if (g_img.w != w || g_img.h != h) {
        free(g_img.px);
        g_img = (ui_image_t){ w, h, calloc((size_t)w * (size_t)h, 4) };
        g_dirty = 1;
    }
    if (!g_img.px) return 0;
    *changed = g_dirty;
    if (g_dirty) draw();
    g_dirty = 0;
    *img = (const uint8_t *)g_img.px;
    return g_screen != CLOSED || ui_lobby_state() == LOBBY_IN;
}
