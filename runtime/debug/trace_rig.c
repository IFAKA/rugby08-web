/* trace_rig.c -- RT_TRACE_RIG=PATH: one JSON line per match frame for the clean-room measuring rig
 * (../rugby-cleanroom): what the match does, never how. Frame, phase, clock, ball position, velocity and
 * holder, the controlled player per team, and per player position, heading and AI state (class vtable).
 * Units: metres, degrees, game seconds (docs/MATCH_STATE.md). Velocities are derived offline. */
#include "debug.h"
#include "match_state.h"
#include <stdio.h>
#include <stdlib.h>

static FILE *g_out;
static int g_init;

void trace_rig_frame(uint32_t frame) {
    if (!g_init) {
        g_init = 1;
        const char *p = getenv("RT_TRACE_RIG");
        if (p && *p) g_out = fopen(p, "w");
    }
    if (!g_out || !ms_active()) return;
    ms_ball_t b = { { 0 }, { 0 }, -1, -1 };
    ms_ball(&b);
    int c0 = -1, c1 = -1;
    ms_controlled(0, 0, &c0);
    ms_controlled(1, 0, &c1);
    fprintf(g_out,
            "{\"f\":%u,\"phase\":\"%s\",\"clock\":%.3f,\"score\":[%d,%d],\"ball\":[%.3f,%.3f,%.3f],"
            "\"bv\":[%.3f,%.3f,%.3f],\"hold\":[%d,%d],\"ctl\":[%d,%d],\"p\":[",
            frame, ms_phase(), (double)ms_clock(), ms_score(0), ms_score(1), (double)b.pos[0], (double)b.pos[1],
            (double)b.pos[2], (double)b.vel[0], (double)b.vel[1], (double)b.vel[2], b.holder_team, b.holder, c0, c1);
    for (int t = 0; t < MS_TEAMS; t++)
        for (int i = 0; i < MS_PLAYERS; i++) {
            ms_player_t p;
            int ok = ms_player(t, i, &p);
            fprintf(g_out, "%s", t || i ? "," : "");
            if (!ok) fprintf(g_out, "null");
            else
                fprintf(g_out, "[%.3f,%.3f,%.3f,%.1f,%u,\"%08X\"]", (double)p.pos[0], (double)p.pos[1],
                        (double)p.pos[2], (double)p.heading, p.parked, p.state_vt);
        }
    fprintf(g_out, "]}\n");
    if (frame % 600 == 0) fflush(g_out);
}
