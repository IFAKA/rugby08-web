/* fe_screens.c -- the party's screens in the game's own front end (pt.menu). The main menu (screen 50,
 * read from the user's own data as the game loads it) gets an ONLINE item after PLAY NOW; the party's
 * screens are built from that same menu (its panel, highlight bar, frame, button style, animations)
 * with other buttons, and handed to the game's screen loader as XML text (fe_xml.c). Every button of
 * ours sends its X press to one game callback, "HeadTesterRotatePC" (an unused developer screen's,
 * game/overrides/menus/party_action.c), which runs fe_party.c by the button's text. */
#include "rt.h"
#include "fe_party.h"
#include "fe_xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALLBACK "HeadTesterRotatePC"
#define STEP     22   /* menu line spacing: 8 items fit the panel the game drew for 7 at 25 */

static fe_node *g_menu;   /* the main menu as the game shipped it (a copy) */
static char g_anim[7][2][40];   /* its items' TransIn / TransOut animation names, by position */
static int g_nanim;
static int g_back;   /* the screen Triangle goes back to, for the buttons being made */

static void numf(char *s, size_t n, double v) { snprintf(s, n, "%.2f", v); }

/* A menu button like PLAY NOW: id, position, text, neighbours, our callback. */
static fe_node *button(const fe_node *like, int id, int x, int y, int w, const char *text, int up, int down, int left,
                       int right, int pos) {
    fe_node *b = fe_clone(like);
    if (!b) return NULL;
    char v[64];
    fe_free(b->child);
    b->child = NULL;
    snprintf(v, sizeof v, "%d", id), fe_set(b, "ID", v);
    numf(v, sizeof v, x), fe_set(b, "xPos", v);
    numf(v, sizeof v, y), fe_set(b, "yPos", v);
    snprintf(v, sizeof v, "%d %d %d 20", x, y, w), fe_set(b, "PickRegion", v);
    fe_set(b, "Text", text);
    snprintf(v, sizeof v, "%d", up), fe_set(b, "Up", v);
    snprintf(v, sizeof v, "%d", down), fe_set(b, "Down", v);
    snprintf(v, sizeof v, "%d", left), fe_set(b, "Left", v);
    snprintf(v, sizeof v, "%d", right), fe_set(b, "Right", v);
    fe_set(b, "MsgLeftPress", "FeSelectableObjectOnLeft");
    fe_set(b, "MsgRightPress", "FeSelectableObjectOnRight");
    fe_set(b, "MsgXPress", CALLBACK);
    if (g_back) {   /* Triangle (Esc): back, with the game's own script */
        snprintf(v, sizeof v, "fez:GoToScreen(%d)", g_back);
        fe_set(b, "MsgTrianglePress", v);
    }
    if (pos >= 0 && g_nanim) {
        int k = pos < g_nanim ? pos : g_nanim - 1;
        for (int t = 0; t < 2; t++) fe_set(fe_add(b, NULL, fe_new("Animation")), "Name", g_anim[k][t]);
    }
    return b;
}

/* A line of text (not selectable) in the info window. */
static fe_node *line(const fe_node *like, int id, int y, int size, const char *text) {
    fe_node *t = fe_clone(like);
    if (!t) return NULL;
    char v[32];
    fe_free(t->child);
    t->child = NULL;
    fe_del(t, "Msg*");
    fe_del(t, "MouseHighlightable");
    fe_del(t, "PickRegion");
    snprintf(v, sizeof v, "%d", id), fe_set(t, "ID", v);
    fe_set(t, "xPos", "0.00");
    numf(v, sizeof v, y), fe_set(t, "yPos", v);
    snprintf(v, sizeof v, "%d", size), fe_set(t, "FontSize", v);
    fe_set(t, "Width", "420.000000");
    fe_set(t, "Text", text);
    return t;
}

/* The main menu as loaded: remember it, then put ONLINE second (all items 22 apart). */
void fe_menu_edit(fe_node *screen) {
    fe_node *items = fe_find(screen, "Window", "0"), *play = items ? fe_find(items, "TextButton", "0") : NULL;
    if (!play) return;
    if (!g_menu) {
        g_menu = fe_clone(screen);
        int k = 0;
        for (fe_node *b = items->child; b && k < 7; b = b->next, k++) {
            const fe_node *a = b->child, *z = a ? a->next : NULL;
            snprintf(g_anim[k][0], sizeof g_anim[k][0], "%s", a ? fe_get(a, "Name") : "");
            snprintf(g_anim[k][1], sizeof g_anim[k][1], "%s", z ? fe_get(z, "Name") : "");
        }
        g_nanim = k;
    }
    fe_node *online = fe_add(items, play, button(play, 1, 0, STEP, 220, "ONLINE", 0, 2, 1, 1, 1));
    fe_node *second = online ? online->next : NULL;
    if (!online || !second) return;
    fe_set(play, "Down", "1");
    fe_set(second, "Up", "1");
    int k = 0;
    char v[64];
    for (fe_node *b = items->child; b; b = b->next, k++) {
        const char *pr = fe_get(b, "PickRegion");
        int w = 220;
        if (pr) sscanf(pr, "%*d %*d %d", &w);
        numf(v, sizeof v, STEP * k), fe_set(b, "yPos", v);
        snprintf(v, sizeof v, "0 %d %d 20", STEP * k, w), fe_set(b, "PickRegion", v);
        fe_node *a = b->child;
        if (a && a->next && k < 8) {
            int j = k < g_nanim ? k : g_nanim - 1;
            fe_set(a, "Name", g_anim[j][0]);
            fe_set(a->next, "Name", g_anim[j][1]);
        }
    }
    rt_trace("ms: party main menu has ONLINE (%d items)", k);
}

static void grid(fe_node *items, const fe_node *like) {   /* JOIN: the code's letters, DEL, OK, BACK */
    const char *abc = FE_PARTY_ALPHABET;
    int n = (int)strlen(abc), cols = 8, rows = n / cols;
    for (int i = 0; i < n; i++) {
        int r = i / cols, c = i % cols;
        int up = r ? i - cols
                   : n + (c < 3   ? 0
                          : c < 6 ? 1
                                  : 2),
            down = r < rows - 1 ? i + cols
                                : n + (c < 3   ? 0
                                       : c < 6 ? 1
                                               : 2);
        int left = c ? i - 1 : i + cols - 1, right = c < cols - 1 ? i + 1 : i - cols + 1;
        char t[2] = { abc[i], 0 };
        fe_add(items, NULL, button(like, i, 30 * c, STEP * r, 22, t, up, down, left, right, -1));
    }
    static const char *const k_end[3] = { "DEL", "OK", "BACK" };
    for (int j = 0; j < 3; j++)
        fe_add(items, NULL,
               button(like, n + j, 80 * j, STEP * rows + 6, 60, k_end[j], n - cols + 3 * j, 3 * j, n + (j + 2) % 3,
                      n + (j + 1) % 3, -1));
}

#define INFO_Y  112   /* the info lines' window: under the panel's logo */
#define ITEMS_Y 252   /* the PARTY screen's buttons */
static int line_y(int i) { return i < 2 ? 0 : i == 2 ? 34 : 54 + 20 * (i - 3); }

/* Where the PARTY screen's info line I and its buttons are, in the front end's 640 x 480 (ui_badge.c). */
void fe_party_layout(int i, int *x, int *y, int *buttons_y) {
    fe_node *items = g_menu ? fe_find(g_menu, "Window", "0") : NULL;
    const char *xp = items ? fe_get(items, "xPos") : NULL;
    *x = xp ? atoi(xp) : 0;
    *y = INFO_Y + line_y(i);
    *buttons_y = ITEMS_Y;
}

/* Our screen id's XML (malloc'd), or NULL. */
char *fe_party_screen_xml(int id, size_t *len) {
    if (!g_menu || (id != FE_SCR_ONLINE && id != FE_SCR_PARTY && id != FE_SCR_JOIN)) return NULL;
    fe_node *s = fe_clone(g_menu), *items = fe_find(s, "Window", "0");
    fe_node *play = items ? fe_clone(fe_find(items, "TextButton", "0")) : NULL;
    if (!play) {
        fe_free(s);
        return NULL;
    }
    char v[16];
    snprintf(v, sizeof v, "%d", id), fe_set(s, "ID", v);
    fe_del(s, "MsgTransitionIn");   /* (MainMenuInit is the main menu's own) */
    fe_free(items->child);
    items->child = NULL;
    fe_node *info = fe_add(s, items, fe_clone(items));   /* the info lines: a window of their own */
    fe_set(info, "ID", "3");
    snprintf(v, sizeof v, "%d.000000", INFO_Y), fe_set(info, "yPos", v);
    g_back = id == FE_SCR_JOIN ? FE_SCR_ONLINE : FE_SCR_MAIN;
    const char *const *menu = fe_party_items(id);
    int n = 0;
    while (menu[n]) n++;
    if (id == FE_SCR_JOIN) {
        fe_set(items, "yPos", "168.000000");
        grid(items, play);
    } else {
        snprintf(v, sizeof v, "%d.000000", id == FE_SCR_ONLINE ? 120 : ITEMS_Y), fe_set(items, "yPos", v);
        for (int i = 0; i < n; i++)
            fe_add(items, NULL, button(play, i, 0, STEP * i, 220, menu[i], (i + n - 1) % n, (i + 1) % n, i, i, i));
    }
    if (id != FE_SCR_ONLINE)
        for (int i = 0; i < FE_PARTY_LINES; i++) fe_add(info, NULL, line(play, i, line_y(i), i == 1 ? 28 : 16, " "));
    fe_free(play);
    g_back = 0;
    char *x = fe_to_xml(s, len);
    fe_free(s);
    return x;
}
