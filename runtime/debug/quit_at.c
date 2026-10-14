/* quit_at.c -- RT_QUIT_AT_FRAME=F (checks): at the first Present from frame F on with a match running,
 * quit to the front end the way the pause menu's confirmed quit does (FUN_00576140); also traces the
 * Game's state changes ("ms: game state VT at frame N"). tools/check/quit_sound_check.sh ends a match
 * at a known moment this way instead of playing 80 minutes. */
#include "debug.h"
#include <stdlib.h>

#define GAME_PTR   0x00A7ED28u /* Game: +0x90 its current state */
#define VT_BACKEND 0x008CC6F0u /* the match */

void dbg_quit_frame(uint32_t frame) {
    static long at = -2;
    static uint32_t last_vt;
    static int done;
    if (at == -2) at = getenv("RT_QUIT_AT_FRAME") ? atol(getenv("RT_QUIT_AT_FRAME")) : -1;
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, vt = st ? rd32(st) : 0;
    if (vt != last_vt) rt_trace("ms: game state %08X at frame %u", vt, frame);
    last_vt = vt;
    if (at < 0 || done || (long)frame < at || vt != VT_BACKEND) return;
    done = 1;
    rt_trace("ms: quit at frame %u", frame);
    guest_callv(0x00576140u, 0, NULL);
}
