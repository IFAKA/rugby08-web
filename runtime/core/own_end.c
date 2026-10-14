/* own_end.c -- each player sees the match from their own team's end, as if playing alone
 * (docs/plans/2026-10-14-online-solo-feel.md, rule 1). Measured (RT_MS_CAMSIDE, RT_MS_CAMEV): the game's end
 * cameras stay behind the human team when only one team has humans (solo, or friends on one team) and,
 * with humans on both teams, turn behind whichever team gains the ball (its event 0xAD: a couch match,
 * one view for one TV). Running a different camera in each game parts the games (the match reads the
 * camera: measured, ls_own_view), so the camera stays the game's, the same in every game, and each game
 * draws it turned to its own player's end: in open play on an end camera (not a set piece's or event's
 * camera, ms_camera_directed), when the camera is behind the other team, the view is drawn from the other
 * side of the point it looks at (runtime/d3d8/d3d8_pov.cpp, rt_pov_yaw 180; screenshots: players and
 * pitch complete), and this player's stick turns with it (own_camera.c). Which way along the pitch each
 * team plays is read off the camera itself after each 0xAD (the camera turns behind the team that gained
 * the ball). Drawing only: no game memory. Trace: "ms: own end team N turned 0/180". */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "match_state.h"
#include "pad_attach.h"
#include "own_camera.h"
#include <stdlib.h>
#include <string.h>

#define EV_POSSESSION 0xADu

static int g_gained = -1;   /* the team that gained the ball at the last 0xAD, until the camera shows it */
static int g_way[2];        /* each team's way along the pitch's length as the camera looks (+1, -1; 0 unknown) */

static int on(void) { return ls_on() && !rt_ff && own_camera_on(); }

/* This player's team: 0 home, 1 away, -1 none (its controller in the middle: it watches). */
static int my_team(void) {
    int me = rt_net_slot();
    char s = me == rt_net_desk() && rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(me);
    return s == 'H' ? 0 : s == 'A' ? 1 : -1;
}

/* Every game, after the camera heard a match event (game/overrides/match/camera_events.c). */
void own_end_heard(uint32_t event) {
    int what;
    if (event && rd32(event + 4) == EV_POSSESSION) g_gained = ms_team_of(rd32(event + 8), &what);
}

/* The game's camera's way along the pitch's length (z): +1, -1; 0 across the pitch or none. */
static int camera_way(void) {
    uint8_t c[MS_CAMERA_BYTES];
    float e[3], t[3];
    ms_camera_game(c);   /* (the game's own view: the camera object also carries a party's shared look) */
    memcpy(e, c, sizeof e);
    memcpy(t, c + 0x10, sizeof t);
    float dx = t[0] - e[0], dz = t[2] - e[2];
    return dz * dz > dx * dx ? (dz > 0 ? 1 : -1) : 0;
}

static int end_camera(void) {
    const char *n = ms_camera_name();
    return !ms_camera_directed() && (strstr(n, "Classic") || strstr(n, "End to End") || strstr(n, "Truck"));
}

/* Every Present, every game: this game's view of the match (drawn only). */
void own_end_present(void) {
    static float said = -1;
    float yaw = 0;
    if (on() && ms_active()) {
        int way = camera_way(), end = end_camera();
        ms_ball_t b;   /* before the first 0xAD (a kick-off): the camera stands behind the team with the ball */
        if (!g_way[0] && g_gained < 0 && ms_ball(&b) && b.holder_team >= 0) g_gained = b.holder_team == 1;
        if (g_gained >= 0 && end && way) {   /* (the camera has turned behind the team that gained the ball) */
            g_way[g_gained] = way, g_way[1 - g_gained] = -way;
            g_gained = -1;
        }
        int me = my_team();
        if (end && way && me >= 0 && g_way[me] && way != g_way[me]) yaw = 180;
        if (yaw != said) rt_trace("ms: own end team %d turned %.0f at frame %u", me, (double)yaw, rt_present_frame);
    }
    said = yaw;
    rt_pov_yaw = yaw;
}
