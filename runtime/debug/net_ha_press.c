/* net_ha_press.c -- a friend's test input that waits for the host's match (tools/net/match_net_check.sh):
 * frame-timed replay presses cannot target a match state on the host (the host's match changes course
 * once a friend plays, and the two games' frames drift apart). RT_NET_HA_PRESS=N: once the host frames
 * have shown open play (phase Free, id 7) for 30 Presents in a row, hold the left stick right for N
 * Presents, once ("ms: net test press" lines).
 * RT_NET_HA_PAUSE=N: after that (or at the same open play if no press is asked for), press Esc (this
 * game pauses, so the host does: net_ha_flow.c), and N Presents later this game's own Cross (S: RESUME
 * is highlighted; everyone resumes).
 * RT_NET_HA_MASH=N: tap Cross every N Presents while the host's frames arrive, as a player answering the
 * set-piece prompts of its side (the host's match waits for them), except around the test pause. */
#include "debug.h"
#include <stdlib.h>

static uint32_t g_pause_at;

static void mash(rt_pad_t *local, uint32_t frame, int host_phase) {
    static int every = -2;
    if (every == -2) every = getenv("RT_NET_HA_MASH") ? atoi(getenv("RT_NET_HA_MASH")) : 0;
    if (every <= 0 || host_phase < 0 || (g_pause_at && frame < g_pause_at + 400)) return;
    if (frame % (uint32_t)every < 5) local->buttons |= RT_PAD_CROSS;
    else local->buttons &= (uint16_t)~RT_PAD_CROSS;
}

static void test_pause(rt_pad_t *local, uint32_t frame) {
    static int n = -2;
    static uint32_t at;
    if (n == -2) n = getenv("RT_NET_HA_PAUSE") ? atoi(getenv("RT_NET_HA_PAUSE")) : 0;
    if (n <= 0) return;
    if (!at) {
        at = g_pause_at = frame;
        rt_trace("ms: net test pause at frame %u", frame);
        plat_desk_apply(&(plat_desk_ev_t){ PLAT_DESK_KEY, 1, 41, 0 });
    }
    if (frame == at + 3) plat_desk_apply(&(plat_desk_ev_t){ PLAT_DESK_KEY, 0, 41, 0 });
    if (frame == at + (uint32_t)n) {   /* this game's own RESUME (S: Cross), which resumes everyone ('Y') */
        rt_trace("ms: net test resume at frame %u", frame);
        plat_desk_apply(&(plat_desk_ev_t){ PLAT_DESK_KEY, 1, 22, 0 });
    }
    if (frame == at + (uint32_t)n + 3) plat_desk_apply(&(plat_desk_ev_t){ PLAT_DESK_KEY, 0, 22, 0 });
}

void net_ha_test_press(rt_pad_t *local, int host_phase, uint32_t frame) {
    static int n = -2, free_for, held = -1;
    mash(local, frame, host_phase);
    if (n == -2) n = getenv("RT_NET_HA_PRESS") ? atoi(getenv("RT_NET_HA_PRESS")) : 0;
    if (held >= n || (n <= 0 && held >= 0)) { /* the press is over (or none was asked for) */
        if (held >= 0) test_pause(local, frame);
        return;
    }
    if (held < 0) {
        free_for = host_phase == 7 ? free_for + 1 : 0;
        if (free_for < 30) return;
        held = 0;
        if (n <= 0) return;
        rt_trace("ms: net test press start");
    }
    local->lx = 32767;
    if (++held == n) {
        local->lx = 0;
        rt_trace("ms: net test press end");
    }
}
