/* ui_badge.c -- the party badge ("PARTY K7P2QX 2/4") in a corner of the game's menus while in a party,
 * drawn by the runtime over the frame at Present (render/webgl/overlay.cpp, the 5x7 font of ui_font.c);
 * the party's screens themselves are the game's own (fe_party.c). Keys: typing a party code (fe_party.c). */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "ls_roles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ui_image_t g_img;
static void catchup_row(ui_image_t *im, int slot, int pct, const char *t);
static void you_tag(ui_image_t *im, int slot);

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
    static uint32_t was;
    uint32_t now = ls_members();
    char line[48];
    if (!was) was = now;   /* (whoever is there when this game starts: no news) */
    int left = rt_net_left();   /* this player left (the others are gone from its game), or lost the party */
    if (left == 1) party_toast(frame, "YOU LEFT THE PARTY");
    for (int k = 0; left == 0 && k < 4; k++)
        if (((now ^ was) >> k & 1) && k != rt_net_slot()) {
            snprintf(line, sizeof line, "PLAYER %d %s", k + 1, now >> k & 1 ? "JOINED" : "LEFT");
            party_toast(frame, line);
        }
    was = now;
}

/* Another player's turn: the screen dimmed (or covered) and whose turn it is (ROLE). */
static void turn(ui_image_t *im, int rk, const char *role) {
    int w = im->w, h = im->h;
    if (rk == LS_BANNER_STRIP) {   /* (Team Select: the screen stays, the line at the bottom) */
        int tw = ui_text_width(role, 2) + 24;
        ui_rect(im, (w - tw) / 2, h - 52, tw, 32, UI_PANEL);
        ui_text(im, (w - tw) / 2 + 12, h - 44, 2, role, UI_GOLD);
        return;
    }
    ui_rect(im, 0, 0, w, h, rk == LS_BANNER_COVER ? 0xFF000000u : 0x90000000u);
    int sc = ui_text_width(role, 3) + 24 <= w - 16 ? 3 : 2;   /* (fits the screen) */
    int tw = ui_text_width(role, sc) + 24;
    ui_rect(im, (w - tw) / 2, h / 2 - 22, tw, 44, UI_PANEL);
    ui_text(im, (w - tw) / 2 + 12, h / 2 - 4 * sc, sc, role, UI_GOLD);
    rt_trace("ms: party role %s", role);
}

/* Which player this is (YOU, a corner), the party badge (B, the other corner; NULL none), a toast. */
static void lines(ui_image_t *im, const char *you, const char *b, const char *toast) {
    if (you[0]) {   /* (2: the player's controller colour) */
        static const uint32_t col[4] = { 0xFF3040E0u, 0xFFE07030u, 0xFF40C040u, 0xFF30D0E0u };
        int tw = ui_text_width(you, 2) + 16;
        ui_rect(im, 8, 8, tw, 28, UI_PANEL);
        ui_rect(im, 8, 8, 4, 28, col[rt_net_slot() & 3]);
        ui_text(im, 18, 15, 2, you, UI_WHITE);
    }
    if (b) {
        int bw = ui_text_width(b, 2) + 16;
        ui_rect(im, im->w - bw - 8, 8, bw, 28, UI_PANEL);
        ui_text(im, im->w - bw, 15, 2, b, UI_WHITE);
        rt_trace("ms: party badge %s", b);
    }
    if (toast) {   /* who left, the party ended: top centre, a few seconds */
        int tw = ui_text_width(toast, 3) + 24;
        ui_rect(im, (im->w - tw) / 2, 44, tw, 36, UI_PANEL);
        ui_text(im, (im->w - tw) / 2 + 12, 52, 3, toast, UI_GOLD);
        rt_trace("ms: party toast %s", toast);
    }
}

/* Friends joining (the server's word): how far each got (PCT, -1 none), the first one, how many. */
static int joining(int pct[4], int *first) {
    int n = 0;
    *first = -1;
    for (int k = 0; k < 4; k++) {
        pct[k] = ls_on() && k != rt_net_slot() ? plat_join_progress(k) : -1;
        if (pct[k] >= 0 && *first < 0) *first = k;
        n += pct[k] >= 0;
    }
    return n;
}

/* This player's row is on the party screen (lockstep): its "YOU" tag. */
static int own_row(void) {
    return ls_on() && (ls_members() >> rt_net_slot() & 1) && fe_in_menus() && fe_screen_id() == FE_SCR_PARTY;
}

int ui_present(uint32_t frame, int w, int h, const uint8_t **img, int *changed) {
    ui_party_view_capture(frame, w, h);   /* (the party screen, before anything is drawn over it) */
    const char *msg = plat_page_message();   /* the page's news: a join refused, a lost connection */
    if (msg) party_toast(frame, msg);
    if (ls_on()) ls_toasts(frame);
    const char *toast = party_toast_text(frame);
    int ls = ls_on() && count(ls_members()) > 1;
    int badge = (ls || ui_lobby_state() == LOBBY_IN) && in_menus();
    char you[32] = "", role[64] = "";   /* in a party: which player this is; the host's or a pauser's turn */
    int rk = ls ? ls_roles_banner(role, sizeof role) : 0;
    if (ls) snprintf(you, sizeof you, "YOU: PLAYER %d", rt_net_slot() + 1);
    int jslot, join = fe_party_joining(&jslot) && fe_in_menus() && fe_screen_id() == FE_SCR_PARTY;
    int pct[4], first, joiners = joining(pct, &first);
    int mine = !join && own_row();   /* (a friend too: the host on the PARTY screen is not covered) */
    if (w <= 0 || h <= 0 || (!badge && !toast && !ls && !join && !joiners && !mine)) return 0;
    static char last[256];
    char b[64], all[256];
    if (ls) snprintf(b, sizeof b, "PARTY %s %d/4", ls_code(), count(ls_members()));
    else snprintf(b, sizeof b, "PARTY %s %d/4", ui_lobby_code(), ui_lobby_members());
    if (first >= 0)   /* (anywhere in the menus: who is joining) */
        snprintf(b + strlen(b), sizeof b - strlen(b), " - PLAYER %d JOINING %d%%", first + 1, pct[first]);
    int rows = joiners && fe_in_menus() && fe_screen_id() == FE_SCR_PARTY && rk != LS_BANNER_LOBBY;
    snprintf(all, sizeof all, "%s|%s|%s|%d%s|%d|%u|%d%d%d%d%d%d", badge ? b : "", toast ? toast : "", you, rk, role,
             join, rk == LS_BANNER_LOBBY ? ui_party_view_gen() : 0, rows, pct[0], pct[1], pct[2], pct[3], mine);
    badge = badge || (joiners && in_menus());
    *changed = strcmp(all, last) != 0 || g_img.w != w || g_img.h != h;
    if (g_img.w != w || g_img.h != h) {
        free(g_img.px);
        g_img = (ui_image_t){ w, h, calloc((size_t)w * (size_t)h, 4) };
    }
    if (!g_img.px) return 0;
    if (*changed) {
        snprintf(last, sizeof last, "%s", all);
        memset(g_img.px, 0, (size_t)w * (size_t)h * 4);
        if (join) catchup_row(&g_img, jslot, 0, "JOINING THE PARTY...");   /* until the page reloads */
        for (int k = 0; rows && k < 4; k++)   /* the others' party screen: a joiner's row fills up there too */
            if (pct[k] >= 0) catchup_row(&g_img, k, pct[k], NULL);
        if (mine) you_tag(&g_img, rt_net_slot());
        if (rk == LS_BANNER_LOBBY) ui_party_view(&g_img, role);   /* the host's menus: not this player's */
        else if (rk) turn(&g_img, rk, role);
        lines(&g_img, you, badge ? b : NULL, toast);
    }
    *img = (const uint8_t *)g_img.px;
    return 1;
}

/* Joining, on the party screen (the game's own frame, or the page's picture of it): the joiner's row
   filling up, and on the joiner's own screen (T) the buttons covered by what is going on (front-end units: 640 x 480, drawn 4:3 centred). */
static void catchup_row(ui_image_t *im, int slot, int pct, const char *t) {
    int x0, y, by, sc = im->w >= 960 ? 3 : 2;
    float k = (float)im->w * 3 < (float)im->h * 4 ? (float)im->w / 640.f : (float)im->h / 480.f;
    float ox = ((float)im->w - 640.f * k) / 2, oy = ((float)im->h - 480.f * k) / 2;
    fe_party_layout(3 + slot, &x0, &y, &by);
    int bx = (int)(ox + (float)(x0 + 205) * k), bw = (int)(120.f * k), bh = (int)(8.f * k);
    int ry = (int)(oy + (float)(y + 9) * k) - bh / 2;
    char pc[8];
    snprintf(pc, sizeof pc, "%d%%", pct);
    ui_rect(im, bx - 2, ry - 2, bw + 4, bh + 4, UI_PANEL);
    ui_rect(im, bx, ry, bw * pct / 100, bh, UI_GOLD);
    ui_text(im, bx + bw + 8, ry + bh / 2 - 7 * sc / 2, sc, pc, UI_GOLD);
    rt_trace("ms: joining on the party screen %d%% %s (menu x %d)", pct, t ? t : "(player's row)", x0);
    if (!t) return;   /* (another player's row, seen by the others: the bar only) */
    int px = (int)(ox + (float)(x0 - 8) * k), py = (int)(oy + (float)(by - 4) * k);
    int pw = ui_text_width(t, sc) + 24, ph = (int)(48.f * k);
    ui_rect(im, px, py, pw, ph, 0xF4000000u);
    ui_text(im, px + 12, py + ph / 2 - 7 * sc / 2, sc, t, UI_GOLD);
}

/* "YOU" right of this player's row on the party screen: drawn here, never in the game (every game's rows
   are the same text: lockstep, fe_party.c). */
static void you_tag(ui_image_t *im, int slot) {
    int x0, y, by, sc = im->w >= 960 ? 3 : 2;
    float k = (float)im->w * 3 < (float)im->h * 4 ? (float)im->w / 640.f : (float)im->h / 480.f;
    float ox = ((float)im->w - 640.f * k) / 2, oy = ((float)im->h - 480.f * k) / 2;
    fe_party_layout(3 + slot, &x0, &y, &by);
    int tx = (int)(ox + (float)(x0 + 205) * k), ty = (int)(oy + (float)(y + 9) * k) - 7 * sc / 2;
    int tw = ui_text_width("YOU", sc) + 12;
    ui_rect(im, tx - 6, ty - 4, tw, 7 * sc + 8, UI_PANEL);
    ui_text(im, tx, ty, sc, "YOU", UI_GOLD);
    rt_trace("ms: party row tag YOU on row %d", slot + 1);
}

/* The page's picture of the party's screen, taken before the reload (platform/web/join_view.c), W x H:
   NULL none (not joining from the game's JOIN PARTY, or not decoded yet). */
static const uint32_t *backdrop(int w, int h) {
    static ui_image_t bd;
    static int ok;
    if (bd.w != w || bd.h != h) {
        free(bd.px);
        bd = (ui_image_t){ w, h, malloc((size_t)w * (size_t)h * 4) };
        ok = 0;
    }
    if (bd.px && !ok) ok = plat_join_backdrop((uint8_t *)bd.px, w, h);
    return ok ? bd.px : NULL;
}

/* Joining a party: the game catches up with the party's session without drawing it (netplay.c); this
   screen is shown now and then: over the party screen when the game is there (DRAWN), else opaque, with
   how far it got. */
int ui_catchup(int w, int h, int drawn, const uint8_t **img) {
    static ui_image_t im;
    static int last = -1;
    if (w <= 0 || h <= 0) return 0;
    if (im.w != w || im.h != h) {
        free(im.px);
        im = (ui_image_t){ w, h, calloc((size_t)w * (size_t)h, 4) };
        last = -1;
    }
    if (!im.px) return 0;
    /* a match being played replays slower than it is played: whoever joins during one comes in when it
       ends (as online sports games do), and is told so */
    uint32_t g = rd32(0x00A7ED28u), st = g ? rd32(g + 0x90) : 0;
    int match = st && rd32(st) == 0x008CC6F0u && rt_net_progress >= 90;
    const uint32_t *bd = drawn ? NULL : backdrop(w, h);
    int key = rt_net_progress + 1000 * match + 2000 * drawn + 4000 * !!bd;
    if (last != key) {
        char line[48];
        last = key;
        *img = (const uint8_t *)im.px;
        if (drawn || bd) {   /* the party's screen: the game's (drawn) or the page's picture of it */
            if (bd) memcpy(im.px, bd, (size_t)w * (size_t)h * 4);
            else memset(im.px, 0, (size_t)w * (size_t)h * 4);
            catchup_row(&im, rt_net_slot(), rt_net_progress,
                        match ? "THE PARTY IS IN A MATCH: YOU JOIN AFTER IT" : "CATCHING UP WITH THE PARTY...");
            return 1;
        }
        ui_rect(&im, 0, 0, w, h, 0xFF000000u);   /* (no picture of the party's screen: a black one) */
        rt_trace("ms: joining on a black screen %d%%", rt_net_progress);
        const char *t = "JOINING THE PARTY";
        ui_text(&im, (w - ui_text_width(t, 4)) / 2, h / 2 - 70, 4, t, UI_WHITE);
        if (match) rt_trace("ms: joining: the party is in a match");
        if (match) snprintf(line, sizeof line, "THE PARTY IS IN A MATCH: YOU JOIN WHEN IT ENDS");
        else snprintf(line, sizeof line, "CATCHING UP WITH THE GAME: %d PERCENT", rt_net_progress);
        ui_text(&im, (w - ui_text_width(line, 2)) / 2, h / 2 - 10, 2, line, UI_GOLD);
        int bw = w * 2 / 3, bx = (w - bw) / 2, by = h / 2 + 30;
        ui_rect(&im, bx, by, bw, 14, 0xFF303030u);
        ui_rect(&im, bx, by, bw * rt_net_progress / 100, 14, UI_GOLD);
    }
    *img = (const uint8_t *)im.px;
    return 1;
}
