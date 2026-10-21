/* own_end.c -- each player sees the match from their own team's end, as if playing alone
 * (docs/plans/2026-10-14-online-solo-feel.md, rule 1). The game's camera is the same in every game (the
 * match reads the camera: a camera of each game's own parted the games), and with humans on both teams
 * its end cameras turn behind whichever team has the ball (a couch match, one TV). Measured (RT_MS_CAMEV,
 * ls_own_view, 2026-10-14): not only on its possession event 0xAD; leaving out 0xAD, 0xAE and 0x1A still
 * left it turning, and turning it back 180 degrees showed the turn (the user's play test). So in open play
 * on an end camera each game draws the game's view looking straight down the pitch along its own player's
 * team's way, from the same point, height and distance (runtime/d3d8/d3d8_pov.cpp, rt_pov_way): the game's
 * camera may turn, the picture does not. Each team's way is read off the game's camera while that team
 * holds the ball (the camera stands behind it). Set pieces' and events' cameras are shown as they are; a
 * player on no team (watching) sees the game's camera. This player's stick turns with the picture
 * (own_camera.c). Drawing only: no game memory, no guest code (match_camera.c ms_camera_name is a local
 * section: one game asking alone moved the lockstep's thread switches, the user's play test).
 * Trace: "ms: own end team N way W at frame F". */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "match_state.h"
#include "pad_attach.h"
#include "own_camera.h"
#include "ls_roles.h"
#include <string.h>

#define SETTLE 45   /* frames a team holds the ball with the camera behind it before its way is read */

static int g_way[2];   /* each team's way along the pitch (z): +1, -1; 0 not known yet */

static int on(void) { return ls_on() && own_camera_on(); }

/* The game's camera's way along the pitch (z): +1, -1; 0 across it. */
static int camera_way(void) {
    uint8_t c[MS_CAMERA_BYTES];
    float e[3], t[3];
    ms_camera_game(c);
    memcpy(e, c, sizeof e);
    memcpy(t, c + 0x10, sizeof t);
    float dx = t[0] - e[0], dz = t[2] - e[2];
    return dz * dz > 4 * dx * dx ? (dz > 0 ? 1 : -1) : 0;
}

static int end_camera(void) {
    const char *n = ms_camera_name();
    return !ms_camera_directed() && (strstr(n, "Classic") || strstr(n, "End to End") || strstr(n, "Truck"));
}

/* While a team with players in the party holds the ball and the end camera has looked one way for SETTLE
   frames, that is the team's way (the other plays the other way; ends change at half time: read every time). */
static void learn(int end, int way) {
    static int team = -1, seen, run;
    ms_ball_t b;
    int t = end && way && ms_ball(&b) && b.holder_team >= 0 && ls_team_has_players(b.holder_team) ? b.holder_team : -1;
    if (t < 0 || t != team || way != seen) {
        team = t, seen = way, run = 0;
        return;
    }
    if (++run == SETTLE && g_way[t] != way) {
        g_way[t] = way, g_way[1 - t] = -way;
        rt_trace("ms: own end team %d plays %+d along the pitch at frame %u", t, way, rt_present_frame);
    }
}

/* Every Present, every game: this game's view of the match (drawn only). */
void own_end_present(void) {
    static int said = 2;
    int way = 0;
    if (on() && !rt_ff && ms_active()) {
        int end = end_camera(), me = ls_team_of(rt_net_slot());
        learn(end, camera_way());
        if (end && me >= 0) way = g_way[me];
    } else if (!ms_active()) g_way[0] = g_way[1] = 0;
    if (way != said)
        rt_trace("ms: own end team %d way %+d at frame %u", ls_team_of(rt_net_slot()), way, rt_present_frame);
    said = way;
    rt_pov_way = way;
}
