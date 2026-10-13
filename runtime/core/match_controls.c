/* match_controls.c -- a friend's game marks the humans' players as the host's does (the HUD's player bars,
 * the rings under controlled players): its teams hold the party's player controls (built at the match's
 * start from the party's controllers, party_sides.c), and every frame each control gets the player the
 * host's control has, from the host's frame head (8 bytes: the shirt number of each control's player,
 * home 4 then away 4; 0 none). The friend's match logic does not run, so nothing else would move them;
 * its presentation puts each control's ring ([+0x18]: +8 shown, +0xC position) at the player. Trace:
 * "ms: controls host|friend home VT:shirt,... away ..." on change. */
#include "rt.h"
#include "match_state.h"
#include <stdio.h>
#include <string.h>

#define T_CONTROLS 0xC0u       /* CRugbyTeam: CPlayerControl*[4] */
#define VT_CONTROL 0x008EE9D8u /* CPlayerControl */
#define C_PLAYER   0x10u       /* CPlayerControl: the controlled CRugbyPlayer* */
#define P_NUMBER   0x78u       /* CRugbyPlayer: u8 shirt number */

static uint8_t g_shirts[MS_CONTROLS_BYTES];

static uint32_t control(int t, uint32_t i) {
    uint32_t team = ms_team_va(t), c = team ? rd32(team + T_CONTROLS + 4 * i) : 0;
    return c && c < 0x40000000u && rd32(c) == VT_CONTROL ? c : 0;
}

static void trace(const char *who) {   /* each team's controls (their vtables and players), on change */
    static char last[2][96];
    char line[96];
    int w = 0, me = who[0] == 'f';
    for (int t = 0; t < 2; t++)
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t team = ms_team_va(t), c = team ? rd32(team + T_CONTROLS + 4 * i) : 0;
            uint32_t p = c && c < 0x40000000u ? rd32(c + C_PLAYER) : 0;
            w += snprintf(line + w, sizeof line - (size_t)w, "%s%06X:%u",
                          i   ? ","
                          : t ? " away "
                              : "home ",
                          c && c < 0x40000000u ? rd32(c) & 0xFFFFFFu : 0, p && p < 0x40000000u ? rd8(p + P_NUMBER) : 0);
        }
    if (!strcmp(line, last[me])) return;
    strcpy(last[me], line);
    rt_trace("ms: controls %s %s (count %u, %u)", who, line, ms_team_va(0) ? rd8(ms_team_va(0) + 0xD0) : 0,
             ms_team_va(1) ? rd8(ms_team_va(1) + 0xD0) : 0);
}

void ms_controls_last(uint8_t *out) {
    trace("host");
    for (int t = 0; t < 2; t++)
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t c = control(t, i), p = c ? rd32(c + C_PLAYER) : 0;
            out[4 * t + i] = p && p < 0x40000000u ? rd8(p + P_NUMBER) : 0;
        }
}

void ms_controls_host(const uint8_t *shirts) { memcpy(g_shirts, shirts, sizeof g_shirts); }

void ms_puppet_controls(void) {
    trace("friend");
    for (int t = 0; t < 2; t++)
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t c = control(t, i), pv = 0;
            uint8_t shirt = g_shirts[4 * t + i];
            if (!c) continue;
            ms_player_t p;
            for (int k = 0; shirt && k < 15 && !pv; k++)
                if (ms_player(t, k, &p) && p.number == shirt) pv = ms_player_va(t, k);
            if (rd32(c + C_PLAYER) != pv) wr32(c + C_PLAYER, pv);
        }
}
