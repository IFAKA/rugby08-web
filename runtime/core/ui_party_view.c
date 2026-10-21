/* ui_party_view.c -- a party: while the host is in its own menus a friend's screen stays on the party
 * screen (CLAUDE.md: share only what online sports games share), never the host's menus or cursor.
 * The party screen as this game last drew it (read back at Present, before the overlays) is shown
 * instead, with what is going on in a panel over its buttons; the players' rows are written anew when
 * the roster changed since. Drawing only: every game still runs the same session (lockstep). */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "ls_roles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int d3d9w_frame_read(unsigned char *rgba, int w, int h);   /* render/webgl/overlay.cpp */

static ui_image_t g_shot;       /* the party screen as this game last drew it */
static uint32_t g_shot_members; /* the roster in it */
static int g_have;
static uint32_t g_gen;          /* a new picture (ui_party_view_gen) */
static uint32_t g_next;         /* the frame the next Present shows */
#define EVERY 60                /* frames between pictures */

/* A friend's game on the party screen: the frame the next Present shows is read back (ls_cursor.c
   keeps this player's arrow out of it). */
int ui_party_view_due(void) {
    return ls_on() && ls_host() != rt_net_slot() && fe_in_menus() && fe_screen_id() == FE_SCR_PARTY &&
           g_next % EVERY == 0;
}

/* At Present, before the overlays: keep the party screen while this friend's game shows it. */
void ui_party_view_capture(uint32_t frame, int w, int h) {
    int due = ui_party_view_due() && frame % EVERY == 0;   /* (g_next: this frame, said at the last Present) */
    g_next = frame + 1;
    if (!due || w <= 0 || h <= 0) return;
    if (g_shot.w != w || g_shot.h != h) {
        free(g_shot.px);
        g_shot = (ui_image_t){ w, h, malloc((size_t)w * (size_t)h * 4) };
        g_have = 0;
    }
    if (g_shot.px && d3d9w_frame_read((unsigned char *)g_shot.px, w, h)) {
        if (!g_have) rt_trace("ms: party view picture %dx%d", w, h);
        g_have = 1, g_shot_members = ls_members(), g_gen++;
    }
}
uint32_t ui_party_view_gen(void) { return g_gen + (uint32_t)(fe_screen_id() == FE_SCR_PARTY) * 0x80000000u; }

/* The front end's 640 x 480 units in this image: drawn 4:3, centred. */
static void fe_rect(const ui_image_t *im, float x, float y, float w, float h, int r[4]) {
    float k = (float)im->w * 3 < (float)im->h * 4 ? (float)im->w / 640.f : (float)im->h / 480.f;
    float ox = ((float)im->w - 640.f * k) / 2, oy = ((float)im->h - 480.f * k) / 2;
    r[0] = (int)(ox + x * k), r[1] = (int)(oy + y * k), r[2] = (int)(w * k), r[3] = (int)(h * k);
}

static void rows(ui_image_t *im, int sc) {   /* the players, as the party screen writes them (fe_party.c) */
    uint32_t mask = ls_members();
    for (int k = 0; k < 4; k++) {
        int x, y, by, r[4];
        char l[48];
        fe_party_layout(3 + k, &x, &y, &by);
        fe_rect(im, (float)x - 8, (float)y - 2, 330, 20, r);
        ui_rect(im, r[0], r[1], r[2], r[3], 0xFF000000u);   /* (opaque: the host's menu is under it) */
        if (mask >> k & 1)
            snprintf(l, sizeof l, "%d  PLAYER %d%s%s", k + 1, k + 1, k == rt_net_host() ? " (HOST)" : "",
                     k == rt_net_slot() ? " (YOU)" : "");
        else snprintf(l, sizeof l, "%d  WAITING...", k + 1);
        static int said = -1;   /* (the check's trace, as ui_badge.c's you_tag on the party screen) */
        if ((mask >> k & 1) && k == rt_net_slot() && said != k)
            rt_trace("ms: party row tag YOU on row %d", (said = k) + 1);
        ui_text(im, r[0] + 8, r[1] + r[3] / 2 - 7 * sc / 2, sc, l, mask >> k & 1 ? UI_WHITE : UI_GREY);
    }
}

/* This friend's screen while the host chooses: the party screen (live, or as last drawn) and MSG. */
void ui_party_view(ui_image_t *im, const char *msg) {
    int sc = im->w >= 960 ? 3 : 2, x, y, by, r[4];
    if (fe_screen_id() != FE_SCR_PARTY) {   /* the host's menus are not this player's: covered */
        if (g_have && g_shot.w == im->w && g_shot.h == im->h)
            memcpy(im->px, g_shot.px, (size_t)im->w * (size_t)im->h * 4);
        else ui_rect(im, 0, 0, im->w, im->h, 0xFF100C08u);
        if (!g_have || g_shot_members != ls_members()) rows(im, sc);
    }
    fe_party_layout(0, &x, &y, &by);
    fe_rect(im, (float)x - 8, (float)by - 4, 0, 56, r);
    int pw = ui_text_width(msg, sc) + 24;
    if (r[0] + pw > im->w - 8 && sc > 1) sc--, pw = ui_text_width(msg, sc) + 24;   /* (fits the screen) */
    ui_rect(im, r[0], r[1], pw, r[3], 0xFF000000u);   /* (opaque: no ghost of the host's menu) */
    ui_text(im, r[0] + 12, r[1] + r[3] / 2 - 7 * sc / 2, sc, msg, UI_GOLD);
    rt_trace("ms: party view %s", msg);
}
