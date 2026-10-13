/* sub_00574380 -- "HeadTesterRotatePC", a developer screen's callback (returns 0; that screen is never
 * shown; referenced only from the callback table at 0x991C84). The party's buttons name it as their X
 * press (runtime/core/fe_screens.c): front-end callbacks are cdecl int cb(FeScreenObject *button). */
#include "game.h"
#include "fe_party.h"

GAME_OVERRIDE(sub_00574380) { GAME_RETURN(fe_party_action(GAME_ARG(0)), 0); }
