/* net_ha_press.c -- a friend's test input that waits for the host's match (tools/net/match_net_check.sh):
 * frame-timed replay presses cannot target a match state on the host (the host's match changes course
 * once a friend plays, and the two games' frames drift apart). RT_NET_HA_PRESS=N: once the host frames
 * have shown open play (phase Free, id 7) for 30 Presents in a row, hold the left stick right for N
 * Presents, once ("ms: net test press" lines). */
#include "debug.h"
#include <stdlib.h>

void net_ha_test_press(rt_pad_t *local, int host_phase) {
    static int n = -2, free_for, held = -1;
    if (n == -2) n = getenv("RT_NET_HA_PRESS") ? atoi(getenv("RT_NET_HA_PRESS")) : 0;
    if (n <= 0 || held >= n) return;
    if (held < 0) {
        free_for = host_phase == 7 ? free_for + 1 : 0;
        if (free_for < 30) return;
        held = 0;
        rt_trace("ms: net test press start");
    }
    local->lx = 32767;
    if (++held == n) {
        local->lx = 0;
        rt_trace("ms: net test press end");
    }
}
