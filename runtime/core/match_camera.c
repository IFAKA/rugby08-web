/* match_camera.c -- the match camera. Each game frames the match with its own camera and settings
 * (pt.camera): the CameraManager picks its named cameras on match events (FUN_00407810), and a friend's
 * game, whose match logic does not run, hears the host's (forwarded as 'C': id and int; the personal
 * ones, 0x118 style and 0x119 zoom, and those that carry game objects stay home). Every Present
 * the camera manager's controller computes the view (FUN_006E8260, game/overrides/match/camera_update.c):
 * 16 floats {eye xyz 1, target xyz 1, up xyz 1, fov, aspect, near, far} (cm, degrees), after shake and
 * zoom, applied to the LookAtCamera and the projection and kept at controller+0x54
 * (docs/MATCH_STATE.md "Camera"); this game's last view goes with the host's frames (match_replay.c head;
 * measures only: no friend applies it). */
#include "rt.h"
#include "plat.h"
#include "match_state.h"
#include <string.h>

#define C_STATE 0x54u        /* controller: the 16 floats applied last */

static uint8_t g_last[MS_CAMERA_BYTES];   /* this game's camera at the last Present */
static uint32_t g_listener;               /* this game's CameraManager event listener */
static int g_replaying;                   /* a friend replaying the host's event */

/* Match events the cameras follow and a friend's game can replay: those whose handling only looks at the
   cameras and the player's own options. Not: 0x118/0x119 (personal), 0x3A (carries a game object), 0x41
   and 0x4A (lineout cameras: they read the throw, which a friend's game never sets up: a fault, measured),
   0x1A and 0x79 (they read players the friend's logic does not run). */
static int forwarded(uint32_t id) {
    static const uint16_t k[] = { 0x00, 0x03, 0x09, 0x0B, 0x12, 0x1B, 0x20, 0x21, 0x23, 0x31, 0x4C, 0x56, 0x6B,
                                  0x70, 0x75, 0x77, 0x78, 0x7E, 0x7F, 0xAD, 0xAE, 0xB3, 0xB5, 0xB7, 0xD6, 0x111 };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == id) return 1;
    return 0;
}

void ms_camera_event(uint32_t listener, uint32_t event) {
    g_listener = listener;
    uint32_t id = event ? rd32(event + 4) : 0xFFFFFFFFu;
    if (rt_net_ha == 1 && event && forwarded(id)) {
        uint32_t v[2] = { id, rd32(event + 0xC) };
        net_ha_send('C', 0, v, sizeof v);
    }
    if (rt_net_ha == 2 && !g_replaying && forwarded(id)) rt_trace("ms: cam own event %X", id);
}

static uint32_t g_manager;

/* The name of the camera this game shows ("Side", "Kickoff", "Zoom Broadcast" ...): CameraManager
   thunk 0x453E90 (its controller +0x1E4, 0x40BBB0). */
const char *ms_camera_name(void) {
    if (!g_manager) return "";
    uint32_t sv = g_ecx;
    g_ecx = g_manager;
    uint32_t s = guest_call(0x00453E90u, 0);
    g_ecx = sv;
    const char *n = s ? gstr(s) : NULL;
    return n ? n : "";
}

void ms_camera_manager(uint32_t manager) {   /* its event listener: the sub-object with vtable 0x8CCC88 */
    g_manager = manager;
    if (g_listener || !manager) return;
    for (uint32_t off = 0; off < 0x40; off += 4)
        if (rd32(manager + off) == 0x008CCC88u) {
            g_listener = manager + off;
            return;
        }
}

void ms_camera_remote(uint32_t id, uint32_t param) {
    static uint32_t ev, zero;
    if (!g_listener || !forwarded(id) || (!ev && !(ev = gcalloc(16))) || (!zero && !(zero = gcalloc(16)))) return;
    wr32(ev + 4, id);
    wr32(ev + 8, zero);
    wr32(ev + 12, param);
    uint32_t sv = g_ecx;
    g_ecx = g_listener;
    g_replaying = 1;
    guest_call(0x00407810u, 1, ev);
    g_replaying = 0;
    g_ecx = sv;
    rt_trace("ms: cam host event %X %d", id, (int)param);
}

void ms_camera_last(uint8_t *out) { memcpy(out, g_last, MS_CAMERA_BYTES); }

/* After the controller's own step: what it applied (the frame head carries it: match_replay.c). */
void ms_camera_after(uint32_t ctrl, uint32_t cam, uint32_t proj) {
    (void)cam, (void)proj;
    memcpy(g_last, G2H(ctrl + C_STATE), MS_CAMERA_BYTES);
}
