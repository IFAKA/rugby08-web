/* own_end.c -- each player's camera as if playing alone (docs/plans/2026-10-14-online-solo-feel.md,
 * rule 1). Measured (RT_MS_CAMSIDE): the game's end cameras stay behind the human team when only one team
 * has humans (solo, or friends on one team: 45 + 15 samples, possession either way) and follow possession
 * when both teams have humans (24/25, 24/26): the couch camera, one view for one TV. In a party each copy's
 * camera (its event listener FUN_00407810 and its step FUN_006E8260) runs with only this player's team's
 * humans: the other team's controls (CRugbyTeam +0xC0..+0xCC) are hidden and put back right after, in a
 * det_local section, so the shared match never sees it and every copy's scheduling stays the same.
 * Trace: "ms: own end team N" when this player's team changes. */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "match_state.h"
#include "pad_attach.h"

#define T_CONTROLS 0xC0u   /* CRugbyTeam: CPlayerControl*[4], one per human on that side */

static uint32_t g_saved[2][4];
static int g_depth, g_hid[2];

/* This player's team: 0 home, 1 away, -1 none (its controller in the middle: it watches). */
static int my_team(void) {
    int me = rt_net_slot();
    char s = me == rt_net_desk() && rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(me);
    return s == 'H' ? 0 : s == 'A' ? 1 : -1;
}

void own_end_begin(void) {
    if (!ls_on() || rt_ff) return;
    det_local_begin();
    if (g_depth++) return;
    static int said = -2;
    int me = my_team();
    if (me != said) rt_trace("ms: own end team %d", me), said = me;
    for (int t = 0; t < 2; t++) {
        uint32_t team = ms_team_va(t);
        g_hid[t] = team && t != me;
        if (!g_hid[t]) continue;
        for (uint32_t k = 0; k < 4; k++) {
            g_saved[t][k] = rd32(team + T_CONTROLS + 4 * k);
            wr32(team + T_CONTROLS + 4 * k, 0);
        }
    }
}

void own_end_end(void) {
    if (!ls_on() || rt_ff || !g_depth) return;
    if (!--g_depth)
        for (int t = 0; t < 2; t++) {
            uint32_t team = ms_team_va(t);
            for (uint32_t k = 0; team && g_hid[t] && k < 4; k++) wr32(team + T_CONTROLS + 4 * k, g_saved[t][k]);
            g_hid[t] = 0;
        }
    det_local_end();
}
