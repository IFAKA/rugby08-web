/* FUN_00407810 -- the CameraManager's event listener (thiscall on the listener, vtable 0x8CCC88; ret 4,
 * the event: +4 id, +8 data, +0xC an int): it picks the named camera a match event calls for (Kickoff,
 * Scrum, Lineout ..., Zoom ..., the player's own style on 0x12/0x1B) from the player's own options.
 * Replaced so a host can pass the match's camera events on and a friend's game, whose match logic does
 * not run, can hear them too: each game frames the same match with its own camera settings
 * (runtime/core/match_camera.c, pt.camera). */
#include "game.h"
#include "match_state.h"

GAME_OVERRIDE(sub_00407810) {
    ms_camera_event(g_ecx, GAME_ARG(0));
    sub_00407810_lifted();   /* pops its argument */
}
