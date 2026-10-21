/* setpiece_probe.c -- RT_MS_SETPIECE=1: when a set piece's options open (quick options 0x58, penalty
 * options 0x5F), the phase, the ball's holder team and the phase object's first words: to find which
 * team the game gives the options to (ls_roles.c "the other team"). Measured already: the controllers'
 * active flags, the teams' controls' set-piece objects and FN_SENDER (the last controller pressed) do not
 * tell. */
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
    if (!on || id == last) return;
    last = id;
    if (id != 0x58 && id != 0x5F) return;
    ms_ball_t b = { 0 };
    int ball = ms_ball(&b);
    rt_trace("setpiece: screen %02X at frame %u phase %s holder team %d", id, frame, ms_phase(),
             ball ? b.holder_team : -9);
    for (int t = 0; t < 2; t++)   /* players in QuickOptionState@Player (vtable 0x8EDC78): the option's taker */
        for (int i = 0; i < 15; i++) {
            ms_player_t p;
            if (ms_player(t, i, &p) && p.state_vt == 0x008EDC78u)
                rt_trace("setpiece:  taker team %d player %d (number %d)", t, i, p.number);
        }
    uint32_t ph = ms_phase_va();   /* the phase object: which word is the awarded team? */
    char pw[400];
    int pn = 0;
    for (uint32_t o = 0; ph && o < 0x80 && pn < (int)sizeof pw - 12; o += 4)
        pn += snprintf(pw + pn, sizeof pw - (size_t)pn, "%08X ", rd32(ph + o));
    rt_trace("setpiece:  phase %d object %08X: %s", ms_phase_id(), ph, pn ? pw : "-");
    rt_trace("setpiece:  teams %08X %08X", ms_team_va(0), ms_team_va(1));
    for (int t = 0; t < 2; t++)   /* each CRugbyTeam's first 0x400 bytes: a field only the deciding team has? */
        for (uint32_t row = 0, team = ms_team_va(t); team && row < 0x400; row += 0x100) {
            char hx[0x100 * 2 + 1];
            for (uint32_t o = 0; o < 0x100; o++) snprintf(hx + 2 * o, 3, "%02X", rd8(team + row + o));
            rt_trace("setpiece:  team %d +%03X %s", t, row, hx);
        }
}
