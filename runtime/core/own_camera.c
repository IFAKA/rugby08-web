/* own_camera.c -- each player's own camera in a lockstep party (the user, 2026-10-13: every player sees
 * the game as if playing alone). Every game runs the same match from everyone's inputs; the camera is the
 * one thing each game runs its own way:
 *   settings  the camera options (options 0xA33320 +0x230..+0x257: style, height, zoom, presets) are this
 *             player's. The menus are shared: a change made there with another player's controller is
 *             undone in this game, one made with this player's own controller is kept (ls_sender).
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
#include <math.h>
#include <string.h>

#define OPTIONS   0x00A33320u
#define O_CAM_LO  0x230u
#define O_CAM_HI  0x258u
#define BACKEND   0x009B65ECu
#define B_CAMERA  0x64u
#define C_LOOK    0x190u /* x, y, z: target - eye, y = 0 */
#define SKIP_OPTS 0      /* det_hash_skip slot */

static uint8_t g_mine[O_CAM_HI - O_CAM_LO], g_seen[O_CAM_HI - O_CAM_LO];
static int g_have;
static float g_ref[2] = { -1.0f, 0.0f };   /* the match's look (x, z): player 1's; across the pitch at first */
static float g_own[3];                      /* this game's camera's look, kept across the match's turn */
static int g_own_set;
static uint32_t g_va;                       /* where the camera keeps its look (this match's camera) */

/* OFF until the match's other reads of the camera are found: with different camera styles, positions
   still part within a second of kick-off (2026-10-13, the stick fixed; AI players too). */
#define OWN_CAMERA_ENABLED 0
static int on(void) { return OWN_CAMERA_ENABLED && ls_on(); }

/* Changes to the camera options since the last look: kept if this player's controller made them. */
void own_camera_options(void) {
    if (!on()) return;
    uint8_t *o = (uint8_t *)G2H(OPTIONS + O_CAM_LO);
    if (!g_have) {
        memcpy(g_mine, o, sizeof g_mine), memcpy(g_seen, o, sizeof g_seen), g_have = 1;
        det_hash_skip(SKIP_OPTS, OPTIONS + O_CAM_LO, OPTIONS + O_CAM_HI);
    }
    if (!memcmp(o, g_seen, sizeof g_seen)) return;
    if (ls_sender() == rt_net_slot()) memcpy(g_mine, o, sizeof g_mine);
    else memcpy(o, g_mine, sizeof g_mine);
    memcpy(g_seen, o, sizeof g_seen);
}

void own_camera_adopt(void) {
    if (!g_have) own_camera_options();
    memcpy(g_mine, G2H(OPTIONS + O_CAM_LO), sizeof g_mine);
    memcpy(g_seen, g_mine, sizeof g_seen);
}

static uint32_t look_va(void) {
    uint32_t be = rd32(BACKEND), y = be ? rd32(be + B_CAMERA) : 0;
    return y ? y + C_LOOK : 0;
}

/* Around each camera step (game/overrides/match/camera_update.c via match_camera.c). */
void own_camera_before(void) {
    own_camera_options();
    uint32_t v = look_va();
    if (!on() || !v) return;
    if (v != g_va) g_va = v, g_own_set = 0;   /* (a new match's camera) */
    if (g_own_set) memcpy(G2H(v), g_own, sizeof g_own);   /* the camera goes on from its own */
}
void own_camera_after(void) {
    uint32_t v = look_va();
    if (!on() || !v) return;
    memcpy(g_own, G2H(v), sizeof g_own);
    g_own_set = 1;
    float m[3] = { g_ref[0], 0.0f, g_ref[1] };
    memcpy(G2H(v), m, sizeof m);   /* the match reads sticks against player 1's */
}

/* Player 1's look, quantized (sent when it changes; net_frame.c). 0 when there is no camera. */
int own_camera_look(int16_t *x, int16_t *z) {
    if (!g_own_set || !look_va()) return 0;
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
    if (!on() || !g_own_set || !look_va()) return;
    float a = atan2f(g_own[0], g_own[2]) - atan2f(g_ref[0], g_ref[1]);
    float c = cosf(a * OWN_CAMERA_SIGN), s = sinf(a * OWN_CAMERA_SIGN);
    turn(&p->lx, &p->ly, c, s);
    turn(&p->rx, &p->ry, c, s);
}
