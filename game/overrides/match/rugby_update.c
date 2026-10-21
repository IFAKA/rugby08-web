/* CRugby::Update(dt) -- sub_0062ED50, thiscall, ret 4 (CRugby vtable slot 1, FUN_0062ED30, calls it with
 * 0.02): the match's per-frame step (docs/MATCH_STATE.md "The friend side of the puppet").
 * Replaced so a friend's game can play the host's match (on.match_puppet). In every other game this is
 * the original. In a friend's game the match logic (clock, ball physics, AI, rules, referee, phase
 * machine) does not run: the host's frame is applied instead (ms_puppet_after_update), then the
 * presentation steps of the original run, in its order and with its arguments (the camera director,
 * the actors, 0x62EB50, 0x404C90 -> 0x6231F0, 0x62E900; tail of the lifted body at 0x0062F008). */
#include "game.h"
#include "match_state.h"

static uint32_t tcall(uint32_t va, uint32_t ecx, int n, const uint32_t *args) {   /* thiscall / stdcall */
    uint32_t sv = g_ecx;
    g_ecx = ecx;
    uint32_t r = guest_callv(va, n, args);
    g_ecx = sv;
    return r;
}

/* The tail of the original (0x0062F008..0x0062F084), argument for argument: each pushed dt belongs to the
   last call of its getter chain (0x73F590, 0x401480, 0x62EB50, 0x6231F0). */
static void presentation(uint32_t rugby, uint32_t dt) {
    uint32_t cam = tcall(0x0073C080u, 0, 0, NULL);   /* camera director */
    if (cam) {
        tcall(0x0073F590u, cam, 1, &dt);
        if (tcall(0x00630B60u, tcall(0x0073C080u, 0, 0, NULL), 0, NULL) & 0xFF) tcall(0x0073C6D0u, 0, 0, NULL);
    }
    if (!(tcall(0x0045C310u, rugby, 0, NULL) & 0xFF)) {   /* not paused: the actors */
        uint32_t w = tcall(0x004046F0u, 0, 0, NULL) + 0x48;
        tcall(0x00401480u, tcall(0x00405570u, w, 0, NULL), 1, &dt);
        tcall(0x0062EB50u, rugby, 1, &dt);
    }
    tcall(0x006231F0u, tcall(0x00404C90u, rugby, 0, NULL), 1, &dt);   /* the ball */
    tcall(0x0062E900u, rugby, 0, NULL);
}

GAME_OVERRIDE(sub_0062ED50) {
    uint32_t self = g_ecx, dt = GAME_ARG(0);
    ms_measure_update_begin();
    if (ms_puppet_skip_update()) {   /* a friend's game: the host plays the match */
        ms_puppet_after_update(self);   /* the host frame of this update */
        presentation(self, dt);
        ms_puppet_place(self);   /* players and ball where the host had them, after their own steps */
        ms_measure_update_end();
        GAME_RETURN(0, 4);
    }
    sub_0062ED50_lifted();   /* returns past the argument (ret 4) */
    ms_measure_update_end();
}
