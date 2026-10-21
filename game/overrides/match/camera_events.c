/* FUN_00407810 -- the CameraManager's event listener (thiscall on the listener, vtable 0x8CCC88; ret 4,
 * the event: +4 id, +8 data, +0xC an int): it picks the named camera a match event calls for (Kickoff,
 * Scrum, Lineout ..., Zoom ..., the player's own style on 0x12/0x1B) from the player's own options.
 * Replaced so a host can pass the match's camera events on and a friend's game, whose match logic does
 * not run, can hear them too: each game frames the same match with its own camera settings
 * (runtime/core/match_camera.c, pt.camera). A camera setting another player chose is not heard here
 * (own_camera_personal). */
#include "game.h"
#include "match_state.h"

GAME_OVERRIDE(sub_00407810) {
    uint32_t ev = GAME_ARG(0);
    int own = ms_camera_event(g_ecx, ev);   /* (a camera setting: one game hears it, det_local) */
    if (own) det_local_begin();
    if (own == 2) g_esp += 8;               /* another player's setting: not this camera's (ret 4) */
    else sub_00407810_lifted();             /* pops its argument */
    if (own) det_local_end();
    ms_camera_event_done(ev);
}
