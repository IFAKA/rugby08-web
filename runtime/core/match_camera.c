/* match_camera.c -- the match camera, the host's on a friend's screen (on.match_puppet). Every Present
 * the camera manager's controller computes the view (FUN_006E8260, game/overrides/match/camera_update.c):
 * 16 floats {eye xyz 1, target xyz 1, up xyz 1, fov, aspect, near, far} (cm, degrees), after shake and
 * zoom, applied to the LookAtCamera and the projection and kept at controller+0x54
 * (docs/MATCH_STATE.md "Camera"). The host's 16 floats go with each frame (match_replay.c); a friend
 * applies them over its own through the same three calls. */
#include "rt.h"
#include "match_state.h"
#include <string.h>

#define C_STATE    0x54u        /* controller: the 16 floats applied last */
#define ASPECT_REF 0x008CD730u  /* float 1.3333: fov is scaled by aspect / this */

static uint8_t g_last[MS_CAMERA_BYTES];   /* this game's camera at the last Present */
static const uint8_t *g_host;             /* the host's, for a friend (NULL: its own) */

void ms_camera_last(uint8_t *out) { memcpy(out, g_last, MS_CAMERA_BYTES); }
void ms_camera_host(const uint8_t *cam) { g_host = cam; }

static uint32_t call(uint32_t va, uint32_t ecx, int n, const uint32_t *args) {
    uint32_t sv = g_ecx;
    g_ecx = ecx;
    uint32_t r = guest_callv(va, n, args);
    g_ecx = sv;
    return r;
}

static float f_at(const uint8_t *b, int off) {
    float f;
    memcpy(&f, b + off, 4);
    return f;
}

/* After the controller's own step (it has applied this game's view): the host's view instead, as
   FUN_006E8260's tail applies a view (0x006E8436..0x006E848D). */
void ms_camera_after(uint32_t ctrl, uint32_t cam, uint32_t proj) {
    static uint32_t s;
    if (!g_host) {
        memcpy(g_last, G2H(ctrl + C_STATE), MS_CAMERA_BYTES);
        return;
    }
    if (!s && !(s = gcalloc(MS_CAMERA_BYTES))) return;
    memcpy(G2H(s), g_host, MS_CAMERA_BYTES);
    uint32_t view[3] = { s, s + 0x10, s + 0x20 };
    call(0x00789BE0u, cam, 3, view);
    uint32_t ref = rd32(ASPECT_REF);
    float r, fov = f_at(g_host, 0x34) / (memcpy(&r, &ref, 4), r) * f_at(g_host, 0x30);
    uint32_t p[4];
    memcpy(&p[0], &fov, 4);
    memcpy(&p[1], g_host + 0x34, 12);   /* aspect, near, far */
    call(0x0075EC70u, proj, 4, p);
    uint32_t v = call(0x005F0EF0u, cam, 0, NULL);
    call(0x0075F800u, proj, 1, &v);
    memcpy(G2H(ctrl + C_STATE), g_host, MS_CAMERA_BYTES);   /* its next step starts from the host's */
    memcpy(g_last, g_host, MS_CAMERA_BYTES);
}
