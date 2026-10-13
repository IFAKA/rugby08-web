/* net_ha_flow.c -- the party in host-authoritative play (on.match_flow): when the host starts a match,
 * its friends' games go into it from wherever they are in their menus; when the host's match ends
 * (full time, quit), they leave theirs and are back in their own menus.
 *   Game object [0x00A7ED28], its current GameState at +0x90: RugbyFrontEnd (vtable 0x008CD620) in
 *   the menus, BackendState (0x008CC6F0) in a match, loading screens between.
 *   Into a match: the launch every menu commits through, FUN_004F3D10 (after Game->FUN_004049C0(0), as
 *   PLAY NOW does), with the host's setup offered to it (match_setup.c applies it at its entry).
 *   Out of one: FUN_00576140, what the pause menu's quit does after its confirmation.
 *   Out of a cutscene (the intro; a friend's skipped logic starts no others): while the host's frames
 *   arrive (the host records only in play), BackendState FUN_0047D860, which queues the switch from
 *   the current mode (BackendState+0x48 +4: CutsceneMode 0x008CD20C) to MovementMode, as a skip does.
 *   Pause: only the keyboard's Esc pauses a match (no pad button does); any controller then drives the
 *   pause menu, and Cross on RESUME (first) resumes. The front end's current screen ([0x013834E8]+4,
 *   its id word at +4) is 0x0079 in play and 0x0071 for GAME PAUSED. Esc on an open pause menu does
 *   nothing.
 * Messages (net_ha.c): 'S' starts the friends' launch, 'E' (host, when its game leaves BackendState)
 * ends their match; 'Z' (a friend whose game opened its pause menu) makes the host press Esc, so its
 * match pauses and the pauser's pad (a host controller) drives its menu; 'U' (host: paused or not)
 * opens the friends' menus (Esc) and closes them (FUN_00575F90, the menu's resume) on its edges. */
#include "rt.h"
#include "plat.h"
#include "match_state.h"
#include <string.h>

#define GAME_PTR    0x00A7ED28u
#define G_STATE     0x90u
#define VT_BACKEND  0x008CC6F0u
#define VT_FRONTEND 0x008CD620u
#define RETRY       60   /* Presents before launching again if the game was busy */
#define BACKEND_PTR 0x009B65ECu
#define VT_CUTSCENE 0x008CD20Cu
#define FE_PTR      0x013834E8u   /* the front end: +4 its current screen, whose +4 is the screen id */
#define SCR_PAUSED  0x0071u
#define SCR_TITLE   0x0002u   /* "Click to Continue": the menus' packages are not loaded yet */
#define G_QUEUED    0x94u     /* Game+0x8C is its state machine: +8 a queued state (the launch's busy test) */
#define SC_ESCAPE   41        /* SDL scancode */

static uint8_t g_setup[MS_SETUP_BYTES];
static int g_launch, g_end;
static uint32_t g_tried;

static uint32_t state_vt(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + G_STATE) : 0;
    return st ? rd32(st) : 0;
}

void net_ha_flow_setup(const uint8_t *setup) {   /* friend: 'S' */
    memcpy(g_setup, setup, MS_SETUP_BYTES);
    g_launch = 1;
    g_tried = 0;
}
void net_ha_flow_end(void) { g_end = 1; }   /* friend: 'E' */

static int g_pause_req, g_host_paused;
static uint32_t g_esc_up;
void net_ha_flow_pause_request(void) { g_pause_req = 1; }   /* host: 'Z' */
void net_ha_flow_host_paused(int paused) { g_host_paused = paused; }   /* friend: 'U' */

static int paused_here(void) {
    uint32_t fe = rd32(FE_PTR), screen = fe ? rd32(fe + 4) : 0;
    return screen && state_vt() == VT_BACKEND && rd16(screen + 4) == SCR_PAUSED;
}
static void escape(uint32_t frame, int down) {
    plat_desk_ev_t e = { PLAT_DESK_KEY, (uint8_t)down, SC_ESCAPE, 0 };
    plat_desk_apply(&e);
    g_esc_up = down ? frame + 3 : 0;
}

static void pause_step(uint32_t frame) {
    static int was_here, was_host;
    if (g_esc_up && frame >= g_esc_up) escape(frame, 0);
    int here = paused_here();
    if (rt_net_ha == 1) {
        if (here != was_here) {
            uint8_t p = (uint8_t)here;
            rt_trace("ms: flow frame %u: %s", frame, here ? "paused" : "resumed");
            net_ha_send('U', frame, &p, 1);
        }
        if (g_pause_req && !here && state_vt() == VT_BACKEND) {
            rt_trace("ms: flow frame %u: a friend pauses", frame);
            escape(frame, 1);
        }
        g_pause_req = 0;
    } else {
        if (here && !was_here && !g_host_paused) {
            rt_trace("ms: flow frame %u: pausing the host", frame);
            net_ha_send('Z', frame, NULL, 0);
        }
        if (g_host_paused && !was_host && !here) escape(frame, 1);   /* the host paused: our menu too */
        if (!g_host_paused && was_host && here) {                     /* it resumed: close ours */
            rt_trace("ms: flow frame %u: the host resumed", frame);
            guest_callv(0x00575F90u, 0, NULL);
        }
        was_host = g_host_paused;
    }
    was_here = here;
}

/* In a match, in a cutscene while the host plays: play too. */
static void follow_play(uint32_t frame) {
    static uint32_t asked;
    uint32_t b = rd32(BACKEND_PTR), mode = b ? rd32(b + 0x48 + 4) : 0;
    if (!mode || rd32(mode) != VT_CUTSCENE || !net_ha_rx_recent(frame, 30) || (asked && frame - asked < RETRY)) return;
    rt_trace("ms: flow frame %u: the host plays, leaving our cutscene", frame);
    uint32_t sv = g_ecx;
    g_ecx = b;
    guest_callv(0x0047D860u, 0, NULL);
    g_ecx = sv;
    asked = frame;
}

static void friend_step(uint32_t frame, uint32_t vt) {
    if (g_end) {   /* first out of the ended match, then into the next one */
        if (vt == VT_BACKEND && !g_tried) {
            rt_trace("ms: flow frame %u: the host's match is over, leaving ours", frame);
            guest_callv(0x00576140u, 0, NULL);
            g_tried = frame;
        }
        if (vt == VT_FRONTEND) g_end = g_tried = 0;
        return;
    }
    if (vt == VT_BACKEND) follow_play(frame);
    if (!g_launch || vt != VT_FRONTEND || (g_tried && frame - g_tried < RETRY)) {
        if (g_launch && vt == VT_BACKEND) g_launch = g_tried = 0;   /* in: done */
        return;
    }
    /* The game launches only from its menus: from the title screen the loading state's package
       unload later reads names that were never set (wasm out-of-bounds in stricmp, measured). And a
       launch under way (a queued state: the front end plays its way out first) is not repeated. */
    uint32_t game = rd32(GAME_PTR), fe = rd32(FE_PTR), screen = fe ? rd32(fe + 4) : 0;
    if (!game || !screen || rd16(screen + 4) == SCR_TITLE || rd32(game + G_QUEUED)) return;
    rt_trace("ms: flow frame %u: joining the host's match", frame);
    ms_setup_offer(g_setup);
    uint32_t zero = 0, sv = g_ecx;
    g_ecx = game;
    guest_callv(0x004049C0u, 1, &zero);
    g_ecx = sv;
    guest_callv(0x004F3D10u, 0, NULL);
    g_tried = frame;
}

/* At every Present (net_ha_present). */
void net_ha_flow_present(uint32_t frame) {
    static uint32_t last;
    uint32_t vt = state_vt();
    if (rt_net_ha == 1 && last == VT_BACKEND && vt != VT_BACKEND) {
        rt_trace("ms: flow frame %u: match over", frame);
        net_ha_send('E', frame, NULL, 0);
    }
    if (rt_net_ha == 2) friend_step(frame, vt);
    pause_step(frame);
    if (vt != last) rt_trace("ms: flow frame %u: game state %08X", frame, vt);
    last = vt;
}
