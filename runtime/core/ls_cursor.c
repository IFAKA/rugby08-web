/* ls_cursor.c -- a party: each screen shows its own player's pointer (the user, 2026-10-14). The menus'
 * cursor is game state, alike in every game: player 1's mouse drives it (desk events, applied in every
 * game). A friend's game draws it where the friend's own pointer is: from the frame's first BeginScene
 * to its Present the cursor holds the friend's pointer, and the shared position is back at Present before
 * anything else reads it (fe_party_present, the next frame's logic). The friend's pointer never reaches
 * the game state nor another game; its clicks do nothing in the shared menus, as before. */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "ls_roles.h"
#include <string.h>

#define FE_PTR      0x013834E8u /* the front end: +4 its current FeScreen, +0x31488 its cursor (x, y floats at +8) */
#define GAME_PTR    0x00A7ED28u /* the Game: +0x90 its current state */
#define VT_FRONTEND 0x008CD620u

static uint32_t g_cur;      /* the cursor holding this player's pointer until Present; 0: none */
static uint8_t g_shared[8]; /* its shared x, y */

static uint32_t fe_cursor(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, fe = rd32(FE_PTR);
    if (!st || rd32(st) != VT_FRONTEND || !fe || !rd32(fe + 4)) return 0;
    return rd32(fe + 0x31488);
}

void ls_cursor_scene(void) {   /* IDirect3DDevice8::BeginScene */
    float xy[2] = { -64.f, -64.f };   /* off the screen: the party view's picture is taken without it */
    if (g_cur || rt_ff || !ls_on() || ls_host() == rt_net_slot()) return;
    if (!ui_party_view_due() && !plat_local_pointer(&xy[0], &xy[1])) return;
    uint32_t cur = fe_cursor();
    if (!cur) return;
    memcpy(g_shared, G2H(cur + 8), 8);
    memcpy(G2H(cur + 8), xy, 8);
    g_cur = cur;
}

void ls_cursor_present(void) {   /* IDirect3DDevice8::Present, first */
    if (!g_cur) return;
    if (fe_cursor() == g_cur) memcpy(G2H(g_cur + 8), g_shared, 8);
    g_cur = 0;
}
