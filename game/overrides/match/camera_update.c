/* FUN_006E8260 (thiscall on the camera manager's controller, ret 0x10: manager, dt, LookAtCamera,
 * projection): computes the match view every Present (FUN_006E9180, shake/zoom modifiers) and applies
 * it to the camera and the projection, keeping the 16 floats at +0x54. Replaced so the host's view can
 * be recorded and a friend's game can show it (runtime/core/match_camera.c, on.match_puppet). */
#include "game.h"
#include "match_state.h"

GAME_OVERRIDE(sub_006E8260) {
    uint32_t self = g_ecx, cam = GAME_ARG(2), proj = GAME_ARG(3);
    own_end_begin();   /* (a party: this player's team's view, runtime/core/own_end.c) */
    ms_camera_manager(GAME_ARG(0));
    sub_006E8260_lifted();   /* pops its 0x10 bytes of arguments */
    ms_camera_after(self, cam, proj);
    own_end_end();
}
