/* net_frame.c -- the lockstep step at every Present (det_frame hook): send this player's input,
 * admit newcomers, exchange state hashes, wait for every active player's input, apply them all,
 * decide catching up. Protocol and state: netplay.c, netplay_int.h. */
#include "netplay_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check_hashes(uint32_t frame) {
    uint32_t k = (frame / NET_HASH_EVERY) % HRING;
    if (N.hash_frame[k][N.slot] != frame) return;
    for (int i = 0; i < RT_MAX_PADS; i++) {
        if (i == N.slot || N.hash_frame[k][i] != frame || N.hash[k][i] == N.hash[k][N.slot]) continue;
        rt_trace("netplay: DESYNC at frame %u (player %d hash %016llX, mine %016llX)", frame, i + 1,
                 (unsigned long long)N.hash[k][i], (unsigned long long)N.hash[k][N.slot]);
        if (i < N.slot) exit(3);   /* the lower slot is the reference: this game rebuilds itself */
    }
}

/* RT_NET_FAULT=F (tests): flip one guest byte at frame F, a desync to detect */
static void fault_inject(uint32_t frame) {
    static long fault = -2;
    if (fault == -2) {
        const char *e = getenv("RT_NET_FAULT");
        fault = e ? atol(e) : -1;
    }
    if ((long)frame == fault) wr8(RT_HEAP_LO + 0x100, (uint8_t)(rd8(RT_HEAP_LO + 0x100) ^ 0x5A));
}

/* This player's pad from its devices (no-op in replay runs: replay events already set N.local), and
 * its input delay as the page or the platform chose it. */
static void read_local(uint32_t frame) {
    static int had;   /* controller 1's player: its game controller (its keyboard is the game's keyboard) */
    rt_pad_t g;
    if (N.slot != DESK_SLOT) {
        plat_local_pad(&N.local);   /* a friend: keyboard and game controller drive its pad */
        N.local.spare = 1;          /* (plugged in while it plays: rt_pad_attached) */
    } else if (plat_gamepad(&g)) {
        g.spare = 1;
        N.local = g;
    } else if (had) memset(&N.local, 0, sizeof N.local);
    had = N.slot == DESK_SLOT && N.local.spare;
    static rt_pad_t last;   /* trace input changes (checks: tools/net/net_live_input.py) */
    if (memcmp(&last, &N.local, sizeof last) != 0) {
        rt_trace("netplay: local input frame %u buttons %04X stick %d,%d", frame, N.local.buttons, N.local.lx,
                 N.local.ly);
        last = N.local;
    }
    int d = net_tx_delay();
    if (d > 0) N.delay = d > 60 ? 60 : d;
}

/* One 'I' message: this player's pad for frame f, and (the host) its keyboard and mouse since the last. */
static void send_input(int64_t f, int idle) {
    plat_desk_ev_t d[NDESK];
    int nd = N.slot == DESK_SLOT && !idle ? plat_desk_take(d, NDESK) : 0;
    rt_pad_t pad = N.local;
    if (idle) memset(&pad, 0, sizeof pad), pad.spare = N.slot != DESK_SLOT;   /* plugged in, nothing pressed */
    uint8_t m[19 + 6 * NDESK] = { 'I', (uint8_t)N.slot };
    put32(m + 2, (uint32_t)f);
    memcpy(m + 6, &pad, sizeof pad);
    m[18] = (uint8_t)nd;
    for (int i = 0; i < nd; i++) {
        uint8_t *e = m + 19 + 6 * i;
        e[0] = d[i].type;
        e[1] = d[i].a;
        e[2] = (uint8_t)d[i].x;
        e[3] = (uint8_t)((uint16_t)d[i].x >> 8);
        e[4] = (uint8_t)d[i].y;
        e[5] = (uint8_t)((uint16_t)d[i].y >> 8);
    }
    if (!idle || f < (int64_t)N.frame + RING - 16) net_store_input(N.slot, (uint32_t)f, &pad, nd, d);
    if (idle) {   /* (far ahead of this game's frame: stored when it gets there, wait_inputs) */
        if (!N.idle_hi) N.idle_lo = f;
        N.idle_hi = f;
        N.idle_pad = pad;
    }
    net_tx_send(m, 19 + 6 * nd, 0);
    N.next_send = f + 1;
}

/* The roster authority admits newcomers at a frame past every input this player has sent. */
static void admit(uint32_t frame) {
    for (int k = 0; k < RT_MAX_PADS; k++) {
        if (!N.want[k]) continue;
        N.want[k] = 0;
        int64_t j = N.next_send;
        if ((int64_t)N.max_seen + 1 > j) j = (int64_t)N.max_seen + 1;
        if ((int64_t)frame + N.delay + 1 > j) j = (int64_t)frame + N.delay + 1;
        /* a little ahead: the newcomer, told at once (net_hint), sends idle inputs while it catches up */
        if ((int64_t)frame + 300 > j) j = (int64_t)frame + 300;
        net_send_grant(k, (uint32_t)j);
        net_handle((const uint8_t[6]){ 'A', (uint8_t)k, (uint8_t)j, (uint8_t)(j >> 8), (uint8_t)(j >> 16),
                                       (uint8_t)(j >> 24) });
    }
}

/* Frames frame-119..frame, one slice each: this player's hash, sent once its slot plays. */
static void exchange_hash(uint32_t frame) {
    uint64_t h;
    if (!det_hash_step(frame, NET_HASH_CYCLE, NET_HASH_EVERY, &h)) return;
    uint32_t k = (frame / NET_HASH_EVERY) % HRING;
    N.hash[k][N.slot] = h;
    N.hash_frame[k][N.slot] = frame;
    if (N.my_from >= 0 && frame >= N.my_from && !N.offline) {
        uint8_t hm[14] = { 'H', (uint8_t)N.slot };
        put32(hm + 2, frame);
        for (int i = 0; i < 8; i++) hm[6 + i] = (uint8_t)(h >> (8 * i));
        net_tx_send(hm, sizeof hm, 0);
    }
    rt_trace("netplay: frame %u hash %016llX", frame, (unsigned long long)h);
}

static int missing(uint32_t frame) {   /* the first active player without an input for frame, or -1 */
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (N.active[k] && !net_have_input(k, frame)) return k;
    return -1;
}

static uint64_t wall_ms(void) {   /* (only to decide when to report a stall, never game state) */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* A player whose input stops coming would stop everyone: after STALL_MS the server is told, and it
   drops that player (its 'D' reaches every game at the same frame; the AI takes over its player). */
#define STALL_MS 3000
static void wait_inputs(uint32_t frame) {
    uint32_t waited = 0;
    uint64_t since = 0;
    if (N.idle_hi && frame >= N.idle_lo && frame <= N.idle_hi && !net_have_input(N.slot, frame))
        net_store_input(N.slot, frame, &N.idle_pad, 0, NULL);   /* the idle input sent while catching up */
    for (;;) {
        net_roster_at(frame);
        int who = missing(frame);
        if (who < 0 || N.offline) break;
        if (!rt_ff && waited == 20) since = wall_ms();
        if (since && wall_ms() - since > STALL_MS) {
            char m[48];
            int n = snprintf(m, sizeof m, "{\"t\":\"stall\",\"slot\":%d}", who);
            net_tx_send(m, n, 1);
            rt_trace("netplay: player %d stalled at frame %u: reported", who + 1, frame);
            since = wall_ms();
        }
        net_pump(waited ? 5 : 0);
        if (++waited == 20 && !rt_ff) net_tx_status(1, who);   /* ~0.1 s late: the page shows who we wait for */
        if (waited % 2000 == 0) rt_trace("netplay: waiting for player %d at frame %u", who + 1, frame);
    }
    if (waited >= 20 && !rt_ff && !N.offline) net_tx_status(0, 0);
}

static void apply_inputs(uint32_t frame) {
    static const rt_pad_t zero;
    for (int k = 0; k < RT_MAX_PADS; k++)
        rt_pads[k] = !N.offline && N.active[k] && net_have_input(k, frame) ? N.in[frame % RING][k].pad : zero;
    if (N.offline) rt_pads[N.slot] = N.local;
    /* the host's keyboard and mouse, at the frame they were sent for (offline: as they come) */
    if (!N.offline && N.active[DESK_SLOT] && net_have_input(DESK_SLOT, frame)) {
        const slot_in_t *s = &N.in[frame % RING][DESK_SLOT];
        for (int i = 0; i < s->nd; i++) plat_desk_apply(&s->d[i]);
    } else if (N.offline && N.slot == DESK_SLOT) {
        plat_desk_ev_t d[NDESK];
        for (int i = 0, n = plat_desk_take(d, NDESK); i < n; i++) plat_desk_apply(&d[i]);
    }
}

/* RT_NET_TRACE=1: the inputs applied at every frame (debugging a desync) */
static void trace_applied(uint32_t frame) {
    static int tr = -1;
    if (tr < 0) tr = getenv("RT_NET_TRACE") != NULL;
    if (!tr) return;
    uint32_t c = 0;
    const uint8_t *b = (const uint8_t *)rt_pads;
    size_t n = sizeof rt_pads;
    for (size_t i = 0; i < n; i++) c = c * 31 + b[i];
    rt_trace("netplay: applied frame %u roster %d%d%d%d pads %08X", frame, N.active[0], N.active[1], N.active[2],
             N.active[3], c);
}

/* Catching up: until this player's slot plays, or while far behind the others. */
static void catch_up(uint32_t frame) {
    int ff = !N.offline && (N.my_from < 0 || (int64_t)frame < N.my_from || N.max_seen > frame + 2u * N.delay + 60);
    if (ff && frame % 30 == 0) {
        uint32_t target = N.my_from > (int64_t)frame ? (uint32_t)N.my_from : N.max_seen;
        rt_net_progress = target > frame ? (int)((uint64_t)frame * 100 / target) : 99;
        net_tx_status(2, rt_net_progress);
    }
    if (rt_ff && !ff) {
        rt_trace("netplay: caught up at frame %u", frame);
        net_tx_status(0, 0);
    }
    rt_ff = ff;
}

/* det_frame hook: runs first at every Present. */
void net_frame(uint32_t frame) {
    fault_inject(frame);
    N.frame = frame;
    read_local(frame);
    net_pump(0);
    if (N.my_from >= 0 && !N.offline) {   /* my input for every frame up to frame + delay, each once */
        int64_t own = (int64_t)frame + N.delay, upto = own;
        /* catching up: idle inputs as far as the others have gone, so nobody waits for this game */
        if (rt_ff && (int64_t)N.max_seen + N.delay > upto) upto = (int64_t)N.max_seen + N.delay;
        if (rt_ff && N.front + N.delay + 30 > upto) upto = N.front + N.delay + 30;
        for (int64_t f = N.next_send; f <= upto; f++) send_input(f, f > own);
    }
    if (N.auth && !N.offline) admit(frame);
    exchange_hash(frame);
    wait_inputs(frame);
    apply_inputs(frame);
    trace_applied(frame);
    if (frame >= 2 * NET_HASH_EVERY && frame % NET_HASH_EVERY == NET_HASH_EVERY - 1)
        check_hashes(frame - NET_HASH_EVERY);
    catch_up(frame);
}
