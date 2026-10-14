/* setpiece_probe.c -- RT_MS_SETPIECE=1: when a set piece's options open (quick options 0x58, penalty
 * options 0x5F), who the game gives them to: per team its human controls (CRugbyTeam +0xC0..+0xCC
 * CPlayerControl*, docs/MATCH_STATE.md) with their set-piece object (+0x14) and that object's first words,
 * the phase and the ball holder. To find what decides whose menu it is (ls_roles.c "the other team"). */
#include "rt.h"
#include "debug.h"
#include "fe_party.h"
#include "match_state.h"
#include "../core/pad_attach.h"
#include <stdio.h>
#include <stdlib.h>

void dbg_setpiece_frame(uint32_t frame) {
    static int on = -1, last;
    if (on < 0) on = getenv("RT_MS_SETPIECE") != NULL;
    int id = fe_screen_id();
    static int last_sender = -9;   /* while a set piece's menu is up: whose controller it listens to */
    if (on && (id == 0x58 || id == 0x5F)) {
        int k = ls_sender();
        if (k != last_sender) rt_trace("setpiece: screen %02X frame %u the menu listens to slot %d", id, frame, k);
        last_sender = k;
    } else last_sender = -9;
    if (!on || id == last) return;
    last = id;
    if (id != 0x58 && id != 0x5F) return;
    ms_ball_t b = { 0 };
    int ball = ms_ball(&b);
    rt_trace("setpiece: screen %02X at frame %u phase %s holder team %d", id, frame, ms_phase(),
             ball ? b.holder_team : -9);
    uint32_t g = rd32(0x00A7ED28u), mgr = g ? rd32(g + 0x10) : 0, list = mgr ? rd32(mgr + 0x1C) : 0;
    for (int e = 0; list && e < 9; e++) {   /* the controllers (pad_attach.c): type, side, active, attached */
        uint32_t c = rd32(list + 4u * (uint32_t)e);
        if (!c) continue;
        rt_trace("setpiece:  controller %d type %u pad %d side %c active %u attached %u", e, rd32(c + 0x2570),
                 rd32(c + 0x2570) == 5 ? rt_dinput_pad_slot(rd32(c + 0x25E4)) : -1,
                 rd8(c + 0x2558) ? rd8(c + 0x2558) : '-', rd32(c + 0x257C), rd32(c + 0x2580));
    }
    for (int t = 0; t < 2; t++) {
        uint32_t team = ms_team_va(t);
        for (int k = 0; team && k < 4; k++) {
            uint32_t c = rd32(team + 0xC0 + 4u * (uint32_t)k), sp = c ? rd32(c + 0x14) : 0;
            if (!c) continue;
            char w[160];
            int n = 0;
            for (uint32_t o = 0; sp && o < 0x40 && n < (int)sizeof w - 10; o += 4)
                n += snprintf(w + n, sizeof w - (size_t)n, "%08X ", rd32(sp + o));
            rt_trace("setpiece:  team %d control %d %08X team field %u set piece %08X: %s", t, k, c, rd32(c + 0xC), sp,
                     n ? w : "-");
        }
    }
}
