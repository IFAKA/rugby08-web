/* FUN_00457690 (thiscall on PlayBack::PlayBackCommander, no arguments): whether a replay is playing.
 * Replaced so that, while a friend's game poses a player from the host's frame (pose_player.c), the
 * posing code takes its replay branch; everywhere else the original answers (on.match_puppet). */
#include "game.h"
#include "match_state.h"

GAME_OVERRIDE(sub_00457690) {
    if (ms_puppet_forcing()) GAME_RETURN(1, 0);
    sub_00457690_lifted();
}
