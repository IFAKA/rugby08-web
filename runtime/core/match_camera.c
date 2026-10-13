/* match_camera.c -- the match camera. Each player sees the match as if playing alone (the user,
 * 2026-10-13): in open play every game frames the match with its own CameraManager and its own settings
 * (style, zoom); a friend's game shows the host's view only where its own camera has nothing to go on,
 * its match logic not running: the set pieces' and events' cameras (ms_camera_directed: Kickoff, Lineout .., Scrum, GoalKick, Rail and
 * Unique cameras); the host's frame head says which ("directed"). Every Present the camera manager's
 * controller computes the view (FUN_006E8260, game/overrides/match/camera_update.c): 16 floats {eye xyz
 * 1, target xyz 1, up xyz 1, fov, aspect, near, far} (cm, degrees), after shake and zoom, applied to the
 * LookAtCamera and the projection and kept at controller+0x54 (docs/MATCH_STATE.md "Camera"); a friend
 * applies the host's through the same three calls, and its own camera goes on from there. The current
 * camera's name is kept for the rule and for measures (RT_MS_CAM). */
#include "rt.h"
#include "../debug/debug.h"
#include "plat.h"
#include "match_state.h"
#include "own_camera.h"
#include <string.h>

#define C_STATE    0x54u        /* controller: the 16 floats applied last */
#define ASPECT_REF 0x008CD730u  /* float 1.3333: fov is scaled by aspect / this */

static uint8_t g_last[MS_CAMERA_BYTES];   /* this game's camera at the last Present */
static uint32_t g_listener;               /* this game's CameraManager event listener */
static const uint8_t *g_host;             /* the host's view, for a friend (NULL: its own) */

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

/* Match events the cameras follow and a friend's game can replay (its own camera picks its style's
   camera on them): those whose handling only looks at the cameras and the player's own options. Not:
   0x118/0x119 (personal), 0x3A (carries a game object), 0x41 and 0x4A (lineout cameras: they read the
   throw, which a friend's game never sets up: a fault, measured), 0x1A and 0x79 (they read players the
   friend's logic does not run). */
static int forwarded(uint32_t id) {
    static const uint16_t k[] = { 0x00, 0x03, 0x09, 0x0B, 0x12, 0x1B, 0x20, 0x21, 0x23, 0x31, 0x4C, 0x56, 0x6B,
                                  0x70, 0x75, 0x77, 0x78, 0x7E, 0x7F, 0xAD, 0xAE, 0xB3, 0xB5, 0xB7, 0xD6, 0x111 };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == id) return 1;
    return 0;
}

void ms_camera_event(uint32_t listener, uint32_t event) {
    own_camera_options();   /* (this player's camera settings, before the camera hears of a change) */
    g_listener = listener;
    uint32_t id = event ? rd32(event + 4) : 0xFFFFFFFFu;
    if (rt_net_ha == 1 && event && forwarded(id)) {
        uint32_t v[2] = { id, rd32(event + 0xC) };
        net_ha_send('C', 0, v, sizeof v);
    }
}

void ms_camera_remote(uint32_t id, uint32_t param) {   /* a friend: the host's event, to its own cameras */
    static uint32_t ev, zero;
    if (!g_listener || !forwarded(id) || (!ev && !(ev = gcalloc(16))) || (!zero && !(zero = gcalloc(16)))) return;
    wr32(ev + 4, id);
    wr32(ev + 8, zero);
    wr32(ev + 12, param);
    uint32_t sv = g_ecx;
    g_ecx = g_listener;
    guest_call(0x00407810u, 1, ev);
    g_ecx = sv;
    rt_trace("ms: cam host event %X %d", id, (int)param);
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

int ms_camera_directed(void) {
    static const char *const k[] = { "Kickoff", "Lineout", "Scrum", "GoalKick", "Rail Camera", "Unique Camera" };
    const char *n = ms_camera_name();
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (strstr(n, k[i])) return 1;
    return 0;
}

void ms_camera_manager(uint32_t manager) {   /* its event listener: the sub-object with vtable 0x8CCC88 */
    own_camera_before();
    dbg_render_writes_begin(rt_present_frame);
    g_manager = manager;
    if (g_listener || !manager) return;
    for (uint32_t off = 0; off < 0x40; off += 4)
        if (rd32(manager + off) == 0x008CCC88u) {
            g_listener = manager + off;
            return;
        }
}

/* The view as the scene is drawn with it (the LookAtCamera: eye +0x48, target +0x58, up +0x68; the
   projection as FUN_0075EC70 keeps it: fov scaled to the aspect +0x10C, aspect, near, far): the match
   camera's own, or a cutscene's (they draw through the same two objects), in the controller's 16 floats. */
static uint32_t g_cam, g_proj;
void ms_camera_last(uint8_t *out) {
    memcpy(out, g_last, MS_CAMERA_BYTES);
    if (!g_cam || !g_proj) return;
    memcpy(out, G2H(g_cam + 0x48), 0x30);
    float fov = f_at(G2H(g_proj), 0x10C), aspect = f_at(G2H(g_proj), 0x110), r;
    uint32_t ref = rd32(ASPECT_REF);
    memcpy(&r, &ref, 4);
    if (aspect > 0) fov = fov * r / aspect;
    memcpy(out + 0x30, &fov, 4);
    memcpy(out + 0x34, G2H(g_proj + 0x110), 12);
}

/* After the controller's own step (it has applied this game's view): on a friend the host's view
   instead, as FUN_006E8260's tail applies a view (0x006E8436..0x006E848D). */
void ms_camera_after(uint32_t ctrl, uint32_t cam, uint32_t proj) {
    static uint32_t s;
    own_camera_after(cam);
    if (!g_host) {
        g_cam = cam, g_proj = proj;
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
