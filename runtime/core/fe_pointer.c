/* fe_pointer.c -- the browser: the menus' cursor where the pointer is (mouse or a touch), at every
 * Present. Elsewhere plat_pointer reports nothing and the game moves its cursor by DirectInput deltas. */
#include "rt.h"
#include "plat.h"
#include <string.h>

#define FE_PTR      0x013834E8u /* the front end: +4 its current FeScreen, +0x31488 its cursor (x, y floats at +8) */
#define GAME_PTR    0x00A7ED28u /* the Game: +0x90 its current state */
#define VT_FRONTEND 0x008CD620u

void fe_pointer_present(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, fe = rd32(FE_PTR);
    if (!st || rd32(st) != VT_FRONTEND || !fe || !rd32(fe + 4)) return;
    float px, py;
    uint32_t cur = rd32(fe + 0x31488);
    if (plat_pointer(&px, &py) && cur) {
        memcpy(G2H(cur + 8), &px, 4);
        memcpy(G2H(cur + 12), &py, 4);
    }
}
