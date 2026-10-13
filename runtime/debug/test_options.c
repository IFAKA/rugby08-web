/* test_options.c -- checks set a page's personal options as a player would in its menus (test switches,
 * never for play): RT_TEST_CAMERA=N writes the camera style (options 0xA33320 +0x230 bits 0-3: 0 Truck,
 * 1 Side, 2 Broadcast, 3 Classic; docs/PARTY_RESEARCH.md "Personal settings") while the game is in its
 * menus, so the match's CameraManager starts with it. Each match traces "ms: cam style N" once. */
#include "rt.h"
#include "debug.h"
#include <stdlib.h>

#define OPTIONS  0x00A33320u
#define O_CAMERA 0x230u
#define GAME_PTR 0x00A7ED28u
#define VT_FRONT 0x008CD620u
#define VT_MATCH 0x008CC6F0u

void dbg_test_options_frame(uint32_t frame) {
    static int want = -2;
    static uint32_t last_vt;
    if (want == -2) want = getenv("RT_TEST_CAMERA") ? atoi(getenv("RT_TEST_CAMERA")) : -1;
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, vt = st ? rd32(st) : 0;
    uint32_t v = rd32(OPTIONS + O_CAMERA);
    if (want >= 0 && vt == VT_FRONT) wr32(OPTIONS + O_CAMERA, (v & ~0xFu) | ((uint32_t)want & 0xFu));
    if (vt == VT_MATCH && last_vt != VT_MATCH) rt_trace("ms: cam style %u at frame %u", v & 0xFu, frame);
    last_vt = vt;
}
