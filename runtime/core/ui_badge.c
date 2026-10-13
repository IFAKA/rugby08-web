/* ui_badge.c -- the party badge ("PARTY K7P2QX 2/4") in a corner of the game's menus while in a party,
 * drawn by the runtime over the frame at Present (render/webgl/overlay.cpp, the 5x7 font of ui_font.c);
 * the party's screens themselves are the game's own (fe_party.c). Keys: typing a party code (fe_party.c). */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ui_image_t g_img;

int ui_key(int sc, int down) { return fe_party_key(sc, down); }
int ui_open(void) { return 0; }

static int in_menus(void) { return fe_in_menus() && fe_screen_id() != FE_SCR_PARTY; }

static int count(uint32_t m) {
    int n = 0;
    for (; m; m &= m - 1) n++;
    return n;
}

/* lockstep: who joined or left, from the roster (drawn here only: never game state) */
static void ls_toasts(uint32_t frame) {
    static uint32_t was = 1;
    uint32_t now = ls_members();
    char line[48];
    for (int k = 1; k < 4; k++)
        if ((now ^ was) >> k & 1) {
            snprintf(line, sizeof line, "PLAYER %d %s", k + 1, now >> k & 1 ? "JOINED" : "LEFT");
            party_toast(frame, line);
        }
    was = now;
}

int ui_present(uint32_t frame, int w, int h, const uint8_t **img, int *changed) {
    if (ls_on()) ls_toasts(frame);
    const char *toast = party_toast_text(frame);
    int ls = ls_on() && count(ls_members()) > 1;
    int badge = (ls || ui_lobby_state() == LOBBY_IN) && in_menus();
    if (w <= 0 || h <= 0 || (!badge && !toast)) return 0;
    static char last[128];
    char b[48], all[128];
    if (ls) snprintf(b, sizeof b, "PARTY %s %d/4", ls_code(), count(ls_members()));
    else snprintf(b, sizeof b, "PARTY %s %d/4", ui_lobby_code(), ui_lobby_members());
    snprintf(all, sizeof all, "%s|%s", badge ? b : "", toast ? toast : "");
    *changed = strcmp(all, last) != 0 || g_img.w != w || g_img.h != h;
    if (g_img.w != w || g_img.h != h) {
        free(g_img.px);
        g_img = (ui_image_t){ w, h, calloc((size_t)w * (size_t)h, 4) };
    }
    if (!g_img.px) return 0;
    if (*changed) {
        snprintf(last, sizeof last, "%s", all);
        memset(g_img.px, 0, (size_t)w * (size_t)h * 4);
        if (badge) {
            int bw = ui_text_width(b, 2) + 16;
            ui_rect(&g_img, w - bw - 8, 8, bw, 28, UI_PANEL);
            ui_text(&g_img, w - bw, 15, 2, b, UI_WHITE);
            rt_trace("ms: party badge %s", b);
        }
        if (toast) {   /* who left, the party ended: top centre, a few seconds */
            int tw = ui_text_width(toast, 3) + 24;
            ui_rect(&g_img, (w - tw) / 2, 44, tw, 36, UI_PANEL);
            ui_text(&g_img, (w - tw) / 2 + 12, 52, 3, toast, UI_GOLD);
            rt_trace("ms: party toast %s", toast);
        }
    }
    *img = (const uint8_t *)g_img.px;
    return 1;
}
