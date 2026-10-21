/* sub_005777D0 -- "OnMainMenuReplay", the pause menu's REPLAY (the match replay viewer). A friend's game
 * shows the host's match through its replay system (match_puppet.c): its own viewer would take that
 * over, so in a friend's game it does nothing (4: not handled). Elsewhere the original. */
#include "game.h"
#include "plat.h"

GAME_OVERRIDE(sub_005777D0) {
    if (rt_net_ha == 2) GAME_RETURN(4, 0);
    sub_005777D0_lifted();
}
