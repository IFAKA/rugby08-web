/* ui_camera.c -- each player's own view of the match. Every player of a party sees the same match
 * (lockstep), each from their own camera: runtime/d3d8/d3d8_pov.cpp re-projects the game's camera when
 * drawing only, so it is never game state. C in a match cycles the views (the game's keyboard map
 * leaves C unbound: docs/CONTROLS.md); the key never reaches the game, and a toast names the view. */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include <stdio.h>

#define SC_C       6 /* SDL scancode */
#define GAME_PTR   0x00A7ED28u
#define VT_BACKEND 0x008CC6F0u

static int in_match(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0;
    return st && rd32(st) == VT_BACKEND;
}

int ui_camera_key(int sc, int down) {
    static int mine;
    if (sc != SC_C) return 0;
    if (!down) {
        int m = mine;
        mine = 0;
        return m;
    }
    if (!in_match()) return 0;
    rt_pov_cycle();
    char t[48];
    snprintf(t, sizeof t, "CAMERA: %s", rt_pov_name());
    for (char *p = t; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    party_toast(rt_present_frame, t);
    mine = 1;
    return 1;
}
