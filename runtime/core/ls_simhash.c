/* ls_simhash.c -- the state hash of a party whose players each have their own camera (own_camera.c).
 * Their games then differ in what each one draws (camera, culling, the drawing's buffers: measured, 84
 * of 14079 pages at one frame), so the hash covers what the match is, as lockstep games check sync:
 * the match setup (teams, stadium, conditions), the options outside the camera's (and its dirty flag), the front end's
 * screen, and in a match every player (position, heading, state), the ball, score, clock and phase. */
#include "rt.h"
#include "fe_party.h"
#include "match_state.h"
#include <string.h>

#define OPTIONS 0x00A33320u
#define O_SIZE  0x2E0u
#define O_CAM   0x230u /* .. 0x258: each player's camera (own_camera.c) */
#define O_CAM_E 0x258u
#define O_DIRTY 0x2D8u /* the options changed since saved (FUN_006C5E40): this player's own, as the camera's */

static uint64_t mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

uint64_t ls_simhash(void) {
    uint64_t h = 0xCBF29CE484222325ull;
    h = mix(h, G2H(ms_setup_va()), ms_setup_size());
    h = mix(h, G2H(OPTIONS), O_CAM);
    h = mix(h, G2H(OPTIONS + O_CAM_E), O_DIRTY - O_CAM_E);
    h = mix(h, G2H(OPTIONS + O_DIRTY + 1), O_SIZE - O_DIRTY - 1);
    int id = fe_screen_id();
    h = mix(h, &id, sizeof id);
    for (int t = 0; t < MS_TEAMS; t++)
        for (int i = 0; i < MS_PLAYERS; i++) {
            ms_player_t p;
            if (!ms_player(t, i, &p)) continue;
            h = mix(h, p.pos, sizeof p.pos);
            h = mix(h, &p.heading, sizeof p.heading);
            h = mix(h, &p.state_vt, sizeof p.state_vt);
        }
    ms_ball_t b;   /* (field by field: the struct's padding is whatever the stack held, per machine) */
    if (ms_ball(&b)) {
        h = mix(h, b.pos, sizeof b.pos);
        h = mix(h, b.vel, sizeof b.vel);
        h = mix(h, &b.holder_team, 1);
        h = mix(h, &b.holder, 1);
    }
    int s[2] = { ms_score(0), ms_score(1) };
    float c = ms_clock(), t = ms_time();
    h = mix(h, s, sizeof s);
    h = mix(h, &c, sizeof c);
    h = mix(h, &t, sizeof t);
    const char *ph = ms_phase();
    return mix(h, ph, strlen(ph));
}
