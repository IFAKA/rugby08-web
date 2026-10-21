/* ls_cursor.c -- a party: each screen shows its own player's pointer (the user, 2026-10-14). The menus'
 * cursor is game state, alike in every game: the desk's player's mouse drives it (desk events, applied in
 * every game). A friend's screen draws it where the friend's own pointer is, on its GPU only: the game
 * draws its cursor as a w x h quad the world transform puts at the cursor's x, y (FE +0x31488: x, y, w, h
 * at +8), and the device draws that quad moved by the difference (d3d8_device.cpp DrawPrimitiveUP).
 * Nothing of the friend's pointer reaches the game: every game runs the same code. (Written into the game
 * for its drawing, the pointer changed what the drawing ran, and with it the lockstep's thread switches:
 * measured, RT_DET_SCHED.) */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "ls_roles.h"
#include <string.h>

#define FE_PTR      0x013834E8u /* the front end: +4 its current FeScreen, +0x31488 its cursor */
#define GAME_PTR    0x00A7ED28u /* the Game: +0x90 its current state */
#define VT_FRONTEND 0x008CD620u
#define QUAD_STRIDE 24u         /* xyz, colour, uv */

static uint32_t fe_cursor(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, fe = rd32(FE_PTR);
    if (!st || rd32(st) != VT_FRONTEND || !fe || !rd32(fe + 4)) return 0;
    return rd32(fe + 0x31488);
}

/* A draw of PRIMS primitives from VERTS (STRIDE bytes each) with the world transform at AT (x, y): if it
   is the menus' cursor on a friend's screen, where to move it (DX, DY: off the screen while the party view
   takes its picture). */
int ls_cursor_shift(const float at[2], uint32_t prims, uint32_t stride, const void *verts, float *dx, float *dy) {
    if (rt_ff || !ls_on() || ls_host() == rt_net_slot() || prims != 2 || stride != QUAD_STRIDE) return 0;
    uint32_t cur = fe_cursor();
    float c[4], xy[2] = { -64.f, -64.f };
    if (!cur) return 0;
    memcpy(c, G2H(cur + 8), sizeof c);
    if (at[0] != c[0] || at[1] != c[1]) return 0;
    for (int k = 0; k < 4; k++) {   /* (a w x h quad from 0, 0) */
        float v[2];
        memcpy(v, (const uint8_t *)verts + k * QUAD_STRIDE, sizeof v);
        if ((v[0] != 0.f && v[0] != c[2]) || (v[1] != 0.f && v[1] != c[3])) return 0;
    }
    if (!ui_party_view_due() && !plat_local_pointer(&xy[0], &xy[1])) return 0;
    *dx = xy[0] - c[0], *dy = xy[1] - c[1];
    return 1;
}
