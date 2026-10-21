/* match_quit.c -- RT_MS_QUIT_AT=T: in each match, at the first Present with match time >= T, quit to
 * the front end the way the pause menu's confirmed quit does (FUN_00576140). Checks of the party flow
 * (tools/net/match_flow_check.sh) end matches at a known moment instead of playing 80 minutes. Also
 * traces the Game's state changes ("ms: game state VT at frame F").
 * RT_MS_RELAUNCH=N: N frames after a quit has brought the game back to its menus, start the same match
 * again through the launch every menu commits through (FUN_004F3D10, after Game->FUN_004049C0(0) as PLAY
 * NOW does; the setup is offered back to it), like a rematch: a second match without replaying menus
 * (they remember choices, so the same presses do not repeat the first route); its intro is skipped the
 * way a press does (BackendState FUN_0047D860: CutsceneMode -> MovementMode). */
#include "debug.h"
#include "match_state.h"
#include <stdlib.h>

#define GAME_PTR    0x00A7ED28u   /* Game: +0x90 its current state (net_ha_flow.c) */
#define BACKEND_PTR 0x009B65ECu
#define RUGBY_PTR   0x00A31988u
#define VT_FRONTEND 0x008CD620u
#define VT_BACKEND  0x008CC6F0u
#define VT_CUTSCENE 0x008CD20Cu

static uint8_t g_setup[MS_SETUP_BYTES];
static uint32_t g_quit_frame, g_menu_frame;
static int g_relaunched;

static uint32_t thiscall0(uint32_t va, uint32_t ecx, int n, uint32_t arg) {
    uint32_t sv = g_ecx;
    g_ecx = ecx;
    uint32_t r = guest_callv(va, n, &arg);
    g_ecx = sv;
    return r;
}

static void relaunch(uint32_t frame, uint32_t game, uint32_t vt) {
    static long after = -2;
    if (after == -2) after = getenv("RT_MS_RELAUNCH") ? atol(getenv("RT_MS_RELAUNCH")) : -1;
    if (after < 0) return;
    if (g_quit_frame && vt == VT_FRONTEND && !g_menu_frame) g_menu_frame = frame;   /* back in the menus */
    if (g_menu_frame && frame == g_menu_frame + (uint32_t)after) {
        rt_trace("ms: relaunch at frame %u", frame);
        ms_setup_offer(g_setup);
        thiscall0(0x004049C0u, game, 1, 0);
        guest_callv(0x004F3D10u, 0, NULL);
        g_quit_frame = g_menu_frame = 0;
        g_relaunched = 1;
    }
    uint32_t b = rd32(BACKEND_PTR), mode = b && vt == VT_BACKEND ? rd32(b + 0x48 + 4) : 0;
    if (g_relaunched && mode && rd32(mode) == VT_CUTSCENE && frame % 60 == 0) thiscall0(0x0047D860u, b, 0, 0);
}

void ms_quit_frame(uint32_t frame) {
    static float at = -2;
    static uint32_t rugby_done, last_vt;   /* the match (CRugby*) already quit; the Game's state */
    if (at == -2) at = getenv("RT_MS_QUIT_AT") ? (float)atof(getenv("RT_MS_QUIT_AT")) : -1;
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0, vt = st ? rd32(st) : 0;
    if (vt != last_vt) rt_trace("ms: game state %08X at frame %u", vt, frame);
    static long pages = -2;   /* RT_DET_PAGES_MATCH=N: the page hashes N frames into the first match */
    if (pages == -2) pages = getenv("RT_DET_PAGES_MATCH") ? atol(getenv("RT_DET_PAGES_MATCH")) : -1;
    if (pages >= 0 && vt != last_vt && vt == VT_BACKEND) {
        det_pages_at(frame + (uint32_t)pages);
        pages = -1;
    }
    last_vt = vt;
    static uint32_t last_screen;   /* the front end's current screen ([0x013834E8]+4) */
    uint32_t fe = rd32(0x013834E8u), screen = fe ? rd32(fe + 4) : 0;
    if (screen != last_screen)
        rt_trace("ms: fe screen %08X (vt %08X, id %04X) at frame %u (match time %.2f)", screen,
                 screen ? rd32(screen) : 0, screen ? rd16(screen + 4) : 0, frame, (double)ms_time());
    last_screen = screen;   /* (the state and screen traces: always, every play log has them) */
    if (at < 0) return;
    relaunch(frame, g, vt);
    uint32_t r = rd32(RUGBY_PTR);
    if (!ms_active() || r == rugby_done || ms_time() < at) return;
    rugby_done = r;
    rt_trace("ms: quit at frame %u, match time %.2f", frame, (double)ms_time());
    ms_setup_last(g_setup);
    guest_callv(0x00576140u, 0, NULL);
    g_quit_frame = frame;
}
