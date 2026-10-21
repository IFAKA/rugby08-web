/* FUN_004039B0 (thiscall, ret 0xC: actor, channel, k): poses one actor for the frame. When the replay
 * commander is playing (FUN_00457690) and k < 30, the pose is a ReplayPoseSource for player k (that
 * player's block of the replay frame); otherwise the actor's own animation (PoseSource).
 * Replaced so a friend's game shows the host's poses on its own players (on.match_puppet,
 * docs/MATCH_STATE.md): for the call, the replay manager holds the host's frame (ms_puppet_pose_begin). */
#include "game.h"
#include "match_state.h"

GAME_OVERRIDE(sub_004039B0) {
    if (!ms_puppet_pose_begin(GAME_ARG(2))) {
        sub_004039B0_lifted();
        return;
    }
    sub_004039B0_lifted();
    ms_puppet_pose_end();
}
