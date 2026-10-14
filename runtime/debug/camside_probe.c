/* camside_probe.c -- RT_MS_CAMSIDE=N: every N frames of a match, where the game's camera stands and what
 * it follows: its name, eye and target (x, z in cm), the ball's holder team and position, and each team's
 * human-controlled player (slot 0) and position. To learn which side an end camera takes with humans on
 * both teams (a party is a couch match in every game) and whether it follows the ball or the human. */
#include "rt.h"
#include "debug.h"
#include "match_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dbg_camside_frame(uint32_t frame) {
    static long every = -1;
    if (every < 0) every = getenv("RT_MS_CAMSIDE") ? atol(getenv("RT_MS_CAMSIDE")) : 0;
    if (every <= 0 || frame % (uint32_t)every) return;
    uint8_t c[MS_CAMERA_BYTES];
    ms_ball_t b;
    if (!ms_ball(&b)) return;
    ms_camera_last(c);
    float e[3], t[3];
    memcpy(e, c, sizeof e);
    memcpy(t, c + 0x10, sizeof t);
    char hum[256] = "";
    int n = 0;
    for (int team = 0; team < 2; team++)   /* every human slot of the team: which player it controls */
        for (int slot = 0; slot < 4; slot++) {
            int i = -1;
            ms_player_t p;
            if (ms_controlled(team, slot, &i) && i >= 0 && ms_player(team, i, &p))
                n += snprintf(hum + n, sizeof hum - (size_t)n, " %s%d=%d(%.0f,%.0f)in%X", team ? "A" : "H", slot, i,
                              (double)p.pos[0], (double)p.pos[2],
                              rd32(rd32(ms_team_va(team) + 0xC0 + 4u * (uint32_t)slot) + 0x1C) & 0xFFFFu);
        }
    rt_trace("camside f %u cam %s eye %.0f %.0f target %.0f %.0f ball team %d at %.0f %.0f humans%s", frame,
             ms_camera_name(), (double)e[0], (double)e[2], (double)t[0], (double)t[2], b.holder_team,
             (double)b.pos[0] * 100, (double)b.pos[2] * 100, hum);
}
