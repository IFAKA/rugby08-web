/* Team Select's callbacks (screen 6, docs/PARTY_RESEARCH.md "Modes"; names from the callback table at
 * 0x991870): in a party a side's team, category and kit are its captain's, CONTINUE and BACK the
 * host's, and a friend's Team Select only shows the host's (runtime/core/party_sides.c). Each runs the
 * original when allowed, else answers "not handled" (4). cdecl int cb(FeScreenObject *). */
#include "game.h"
#include "fe_party.h"

GAME_OVERRIDE(sub_004FA6C0) {   /* TeamSelectToggleTeamUp */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FA6C0_lifted();
}

GAME_OVERRIDE(sub_004FA960) {   /* TeamSelectToggleTeamDown */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FA960_lifted();
}

GAME_OVERRIDE(sub_004FB8C0) {   /* TeamSelectToggleKitLeft */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FB8C0_lifted();
}

GAME_OVERRIDE(sub_004FB970) {   /* TeamSelectToggleKitRight */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FB970_lifted();
}

GAME_OVERRIDE(sub_004FABD0) {   /* TeamSelectL1 (category) */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FABD0_lifted();
}

GAME_OVERRIDE(sub_004FAE90) {   /* TeamSelectR1 (category) */
    if (!party_ts_allow(PARTY_TS_SIDE)) GAME_RETURN(4, 0);
    sub_004FAE90_lifted();
}

GAME_OVERRIDE(sub_004FA360) {   /* OnTeamSelectDone (CONTINUE) */
    if (!party_ts_allow(PARTY_TS_HOST)) GAME_RETURN(4, 0);
    sub_004FA360_lifted();
}

GAME_OVERRIDE(sub_004F9EC0) {   /* TeamSelectBack */
    if (!party_ts_allow(PARTY_TS_HOST)) GAME_RETURN(4, 0);
    sub_004F9EC0_lifted();
}
