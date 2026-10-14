/* own_camera.c -- each player's own camera in a lockstep party (the user, 2026-10-13: every player sees
 * the game as if playing alone). Every game runs the same match from everyone's inputs; the camera is the
 * one thing each game runs its own way:
 *   settings  the camera options (options 0xA33320 +0x230..+0x257: style, height, zoom, presets) are this
 *             player's: a change made on CHANGE CAMERA by the player who paused is kept in that player's
 *             game only (own_camera_options).
 *   camera    the CameraManager runs on them in every game; what it keeps is left out of the state hash
 *             (det_hash_skip). Measured: the match never reads the camera, positions stay equal with
 *             different styles (2 min, 698 samples) -- except for one thing:
 *   sticks    HumanMove (0x6BE760) reads sticks against the camera's horizontal look vector (BackendState
 *             [0x9B65EC] +0x64 -> +0x190). The match must see one vector in every game: player 1's
 *             (whose keyboard is the game's), sent with its input ('look' events, net_frame.c). Between
 *             two camera steps the match sees it; the camera keeps its own across them. A friend's stick
 *             is turned from its own camera's direction into player 1's before it is sent. */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "own_camera.h"
#include "match_state.h"
#include "ui.h"
#include "ls_roles.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OPTIONS   0x00A33320u
#define O_CAM_LO  0x230u
#define O_CAM_HI  0x258u
#define SKIP_OPTS 0      /* det_hash_skip slot */

static uint8_t g_mine[O_CAM_HI - O_CAM_LO], g_seen[O_CAM_HI - O_CAM_LO];
static int g_have;
static float g_ref[2] = { -1.0f, 0.0f };   /* the match's look (x, z): player 1's; across the pitch at first */
static float g_own[3];                      /* this game's camera's look, kept across the match's turn */
static int g_own_set;

int rt_own_camera;   /* --own-camera (the web page's sessions) */
static int on(void) {
    static int test = -1;   /* RT_OWN_CAMERA_TEST (native checks): on outside a party */
    if (test < 0) test = getenv("RT_OWN_CAMERA_TEST") != NULL;
    return test || (rt_own_camera && ls_on());
}
int own_camera_on(void) { return on(); }

/* A screen where a player picks camera settings: in a match the pause menu's CHANGE CAMERA (0x2C). */
static int camera_screen(int id) { return id == 0x2C; }

/* Changes to the camera options since the last look. Made on a camera settings screen they are the acting
   player's own choice (only the pauser acts there: ls_roles.c): kept in that player's game, undone in the
   others. Any other change is the game's own (a profile loaded, a match set up): every game takes it. */
void own_camera_options(void) {
    if (!on()) return;
    uint8_t *o = (uint8_t *)G2H(OPTIONS + O_CAM_LO);
    if (!g_have) {
        memcpy(g_mine, o, sizeof g_mine), memcpy(g_seen, o, sizeof g_seen), g_have = 1;
        det_hash_skip(SKIP_OPTS, OPTIONS + O_CAM_LO, OPTIONS + O_CAM_HI);
    }
    if (!memcmp(o, g_seen, sizeof g_seen)) return;
    int id = fe_screen_id(), actor = ls_roles_actor();
    rt_trace("ms: own camera options changed on screen %04X by player %d", id & 0xFFFF, actor + 1);
    if (!camera_screen(id) || actor < 0 || actor == rt_net_slot()) memcpy(g_mine, o, sizeof g_mine);
    else memcpy(o, g_mine, sizeof g_mine);
    memcpy(g_seen, o, sizeof g_seen);
}

void own_camera_adopt(void) {
    if (!g_have) own_camera_options();
    memcpy(g_mine, G2H(OPTIONS + O_CAM_LO), sizeof g_mine);
    memcpy(g_seen, g_mine, sizeof g_seen);
}

/* The match's camera, every game alike: looking along player 1's look at the ball, from 40 m back and
   10 m up (the game's own look-at, FUN_00789BE0, so all the camera object keeps follows from it). */
#define CAM_BYTES 0x110u   /* the LookAtCamera as the match reads it (eye +0x48, target +0x58, up +0x68, ...) */
#define FN_LOOKAT 0x00789BE0u
static uint32_t g_cam, g_scratch;
static uint8_t g_mine_cam[CAM_BYTES], g_mine_look[12];

/* The look sticks are read against (HumanMove 0x6BE760: BackendState [0x9B65EC] +0x64 -> +0x190). */
static uint32_t look_va(void) {
    uint32_t be = rd32(0x009B65ECu), y = be ? rd32(be + 0x64) : 0;
    return y ? y + 0x190 : 0;
}
static int g_have_mine, g_stepped;

static int in_match(void) {
    uint32_t g = rd32(0x00A7ED28u), st = g ? rd32(g + 0x90) : 0;
    return st && rd32(st) == 0x008CC6F0u;
}

/* Around each camera step (game/overrides/match/camera_update.c via match_camera.c): the step and the
   drawing after it see this player's own camera. */
void own_camera_before(void) {
    own_camera_options();
    if (!on() || !g_cam || !g_have_mine) return;
    memcpy(G2H(g_cam), g_mine_cam, CAM_BYTES);
    if (look_va()) memcpy(G2H(look_va()), g_mine_look, sizeof g_mine_look);
}
void own_camera_after(uint32_t cam) {
    if (!on()) return;
    if (cam != g_cam) g_cam = cam, g_have_mine = 0;   /* (a new match's camera) */
    g_stepped = 1;
    float eye[3], target[3];
    memcpy(eye, G2H(cam + 0x48), sizeof eye);
    memcpy(target, G2H(cam + 0x58), sizeof target);
    g_own[0] = target[0] - eye[0], g_own[1] = 0, g_own[2] = target[2] - eye[2];
    if (rt_pov_yaw == 180) g_own[0] = -g_own[0], g_own[2] = -g_own[2];   /* drawn from the other end (own_end.c) */
    g_own_set = 1;
    int ci = -1;   /* (checks: tools/net/ls_camera_check.py; the away side's first human's player) */
    if (rt_present_frame % 30 == 0 && ms_controlled(1, 0, &ci) | 1)
        rt_trace("ms: own camera f %u look %.0f %.0f ref %.3f %.3f away %d", rt_present_frame, (double)g_own[0],
                 (double)g_own[2], (double)g_ref[0], (double)g_ref[1], ci);
}

/* After drawing (every Present, before the match's next step): the match's camera. */
void own_camera_present(void) {
    if (!on() || !g_cam || !in_match()) return;
    uint32_t lv = look_va();
    if (g_stepped) {   /* (the step ran: its result is this player's camera; else it is the match's still) */
        memcpy(g_mine_cam, G2H(g_cam), CAM_BYTES);
        if (lv) memcpy(g_mine_look, G2H(lv), sizeof g_mine_look);
        g_have_mine = 1;
    }
    g_stepped = 0;
    ms_ball_t b;
    if (!ms_ball(&b) || (!g_scratch && !(g_scratch = gcalloc(48)))) return;
    float bx = b.pos[0] * 100.0f, bz = b.pos[2] * 100.0f;
    float v[12] = {
        bx - g_ref[0] * 4000.0f, 1000.0f, bz - g_ref[1] * 4000.0f, 1.0f, bx, 50.0f, bz, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f
    };
    memcpy(G2H(g_scratch), v, sizeof v);
    uint32_t args[3] = { g_scratch, g_scratch + 16, g_scratch + 32 }, sv = g_ecx;
    g_ecx = g_cam;
    guest_callv(FN_LOOKAT, 3, args);
    g_ecx = sv;
    float look[3] = { v[4] - v[0], 0.0f, v[6] - v[2] };
    if (lv) memcpy(G2H(lv), look, sizeof look);
}

/* Player 1's look, quantized (sent when it changes; net_frame.c). 0 when there is no camera. */
int own_camera_look(int16_t *x, int16_t *z) {
    if (!g_own_set || !g_cam) return 0;
    float l = sqrtf(g_own[0] * g_own[0] + g_own[2] * g_own[2]);
    if (l < 1e-3f) return 0;
    *x = (int16_t)lrintf(g_own[0] / l * 16000.0f);
    *z = (int16_t)lrintf(g_own[2] / l * 16000.0f);
    return 1;
}
void own_camera_set_ref(int16_t x, int16_t z) {   /* every game, at the frame player 1 sent it for */
    if (!x && !z) return;
    g_ref[0] = (float)x / 16000.0f;
    g_ref[1] = (float)z / 16000.0f;
}

/* A friend's stick, from its own camera's direction into player 1's (both sticks: run, sidestep). */
static void turn(int16_t *sx, int16_t *sy, float c, float s) {
    float x = *sx, y = *sy, nx = x * c - y * s, ny = x * s + y * c;
    *sx = (int16_t)(nx > 32767 ? 32767 : nx < -32768 ? -32768 : nx);
    *sy = (int16_t)(ny > 32767 ? 32767 : ny < -32768 ? -32768 : ny);
}
void own_camera_turn_pad(rt_pad_t *p) {
    if (!on() || !g_own_set || !g_cam) return;
    float a = atan2f(g_own[0], g_own[2]) - atan2f(g_ref[0], g_ref[1]);
    float c = cosf(a * OWN_CAMERA_SIGN), s = sinf(a * OWN_CAMERA_SIGN);
    turn(&p->lx, &p->ly, c, s);
    turn(&p->rx, &p->ry, c, s);
}
