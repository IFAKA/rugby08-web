/* ls_party.c -- the party in lockstep (docs/plans/2026-10-13-lockstep-party.md): every game is a lockstep
 * session with its room open (netplay.c), so a party is that session. Its ONLINE screens (fe_party.c)
 * show the room's code and the players from the roster, which every game applies at the same frame;
 * JOIN PARTY and LEAVE PARTY act only for the player whose controller pressed them (this game's page
 * reloads into the friend's room, or into a fresh own game). Nothing here writes game memory from local
 * state: the screens' texts come from the code and the roster only, the same in every game. */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "pad_attach.h"
#include <string.h>

#define GAME_PTR  0x00A7ED28u
#define G_CTRLMGR 0x10u      /* Game: CPCControlMgr */
#define FN_SENDER 0x0081A5E0u /* thiscall (mgr, 4): the controller whose input the menus handle */
#define C_TYPE    0x2570u    /* CControl: 5 joystick, 8 keyboard, 9 mouse */
#define C_DEVICE  0x25E4u    /* CControl: its DirectInput device */
#define T_PAD     5u

int ls_on(void) { return rt_net && !rt_net_ha; }

uint32_t ls_members(void) {
    uint32_t m = 0;
    for (int k = 0; k < RT_MAX_PADS; k++) m |= (uint32_t)rt_net_active(k) << k;
    return m;
}

const char *ls_code(void) { return plat_session_code(); }

/* The room slot of the player whose controller the menus are handling: the keyboard and mouse are
   controller 1's player (the room's slot 0), pad k is slot k's player (rt_pads, net_frame.c). */
int ls_sender(void) {
    uint32_t g = rd32(GAME_PTR), m = g ? rd32(g + G_CTRLMGR) : 0, sv = g_ecx;
    if (!m) return 0;
    g_ecx = m;
    uint32_t c = guest_call(FN_SENDER, 1, 4u);
    g_ecx = sv;
    if (!c || rd32(c + C_TYPE) != T_PAD) return 0;
    int k = rt_dinput_pad_slot(rd32(c + C_DEVICE));
    return k > 0 ? k : 0;
}

void ls_join(int sender, const char *code) {
    rt_trace("ms: party player %d joins room %s", sender + 1, code);
    if (sender == rt_net_slot()) plat_session_join(code);
}

void ls_leave(int sender) {
    rt_trace("ms: party player %d leaves", sender + 1);
    if (sender == rt_net_slot()) plat_session_leave();
}
