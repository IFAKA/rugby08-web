/* net_frame.c -- the lockstep step at every Present (det_frame hook): send this player's input,
 * admit newcomers, exchange state hashes, wait for every active player's input, apply them all,
 * decide catching up. Protocol and state: netplay.c, netplay_int.h. */
#include "netplay_int.h"
#include "own_camera.h"
#include "ls_roles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check_hashes(uint32_t frame) {
    uint32_t k = (frame / NET_HASH_EVERY) % HRING;
    if (N.hash_frame[k][N.slot] != frame) return;
    for (int i = 0; i < RT_MAX_PADS; i++) {
        if (i == N.slot || !N.active[i] || N.hash_frame[k][i] != frame || N.hash[k][i] == N.hash[k][N.slot])
            continue;   /* (a slot gone: its hashes since are another game's, e.g. after this player left) */
        rt_trace("netplay: DESYNC at frame %u (player %d hash %016llX, mine %016llX)", frame, i + 1,
                 (unsigned long long)N.hash[k][i], (unsigned long long)N.hash[k][N.slot]);
        static int keep = -1;   /* RT_NET_KEEP_DESYNC (experiments): go on after a desync */
        if (keep < 0) keep = getenv("RT_NET_KEEP_DESYNC") != NULL;
        if (i < N.slot && !keep) exit(3);   /* the lower slot is the reference: this game rebuilds itself */
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
    static int had;   /* the desk's player: its game controller (its keyboard is the game's keyboard) */
    rt_pad_t g;
    plat_desk_capture(N.slot == N.desk);
    if (N.slot != N.desk) {
        plat_local_pad(&N.local);   /* a friend: keyboard and game controller drive its pad */
        N.local.spare = 1;          /* (plugged in while it plays: rt_pad_attached) */
    } else if (plat_gamepad(&g)) {
        g.spare = 1;
        N.local = g;
    } else if (had) memset(&N.local, 0, sizeof N.local);
    had = N.slot == N.desk && N.local.spare;
    ls_roles_local(&N.local);   /* this player's own presses where another player is the host */
    static rt_pad_t last;   /* trace input changes (checks: tools/net/net_live_input.py) */
    if (memcmp(&last, &N.local, sizeof last) != 0) {
        rt_trace("netplay: local input frame %u buttons %04X stick %d,%d", frame, N.local.buttons, N.local.lx,
                 N.local.ly);
        last = N.local;
    }
    int d = net_tx_delay();   /* the page's estimate from its pings, less what the inputs show is spare */
    if (d > 0) N.delay = d - N.cut < 2 ? 2 : d - N.cut > 60 ? 60 : d - N.cut;
}

/* One 'I' message: this player's pad for frame f, and (the host) its keyboard and mouse since the last. */
static void send_input(int64_t f, int idle) {
    plat_desk_ev_t d[NDESK];
    int desk = N.slot == N.desk;
    int nd = desk && !idle ? plat_desk_take(d, NDESK - 2) : 0;
    static int16_t lx, lz;   /* the desk's player: its camera's look, when it changes (own_camera.c) */
    int16_t x, z;
    if (desk && !idle && own_camera_look(&x, &z) && (x != lx || z != lz))
        d[nd++] = (plat_desk_ev_t){ NET_DESK_LOOK, 0, x, z }, lx = x, lz = z;
    if (desk && !idle && net_leave_menu_event(&d[nd])) nd++;   /* (after a leave: to the main menu) */
    rt_pad_t pad = N.local;
    if (idle) memset(&pad, 0, sizeof pad), pad.spare = !desk;   /* plugged in, nothing pressed */
    else if (!desk) own_camera_turn_pad(&pad);   /* from its own camera's direction to the desk player's */
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
    if (own_camera_on()) {   /* each player's own camera: the match's state, not all memory (ls_simhash.c) */
        if ((frame + 1) % NET_HASH_EVERY) return;
        h = ls_simhash();
    } else if (!det_hash_step(frame, NET_HASH_CYCLE, NET_HASH_EVERY, &h)) return;
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
    if (waited >= 20 && !rt_ff && !N.offline) net_tx_status(0, 0), N.stalls++;
}

static void apply_inputs(uint32_t frame) {
    static const rt_pad_t zero;
    for (int k = 0; k < RT_MAX_PADS; k++)
        rt_pads[k] = !N.offline && N.active[k] && net_have_input(k, frame) ? N.in[frame % RING][k].pad : zero;
    if (N.offline) rt_pads[N.slot] = N.local;
    net_desk_at(frame);
    /* the desk's keyboard and mouse, at the frame they were sent for (offline: this player's, as they come) */
    if (!N.offline && N.active[N.desk] && net_have_input(N.desk, frame)) {
        const slot_in_t *s = &N.in[frame % RING][N.desk];
        int desk = ls_roles_filter(s->d, s->nd);   /* who may act now (the host's menus, the pauser's) */
        for (int i = 0; i < s->nd; i++)
            if (s->d[i].type == NET_DESK_LOOK) own_camera_set_ref(s->d[i].x, s->d[i].y);
            else if (s->d[i].type == NET_DESK_MENU) net_leave_menu_apply();
            else if ((desk && ls_roles_desk_event(&s->d[i])) ||
                     ((s->d[i].type == PLAT_DESK_KEY || s->d[i].type == PLAT_DESK_BUTTON) && !s->d[i].a))
                plat_desk_apply(&s->d[i]);   /* (releases always: nothing stays held) */
    } else if (!N.offline) {
        ls_roles_filter(NULL, 0);
    } else if (N.offline) {
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

/* Catching up when joining: until this player's slot plays and it is near the others. Once in step it
   stays in step (the others wait for its inputs), so it never fast-forwards again. */
static void catch_up(uint32_t frame) {
    int ff = !N.offline && !N.joined &&
             (N.my_from < 0 || (int64_t)frame < N.my_from || N.max_seen > frame + 2u * N.delay + 60);
    if (ff && frame % 30 == 0) {
        uint32_t target = N.my_from > (int64_t)frame ? (uint32_t)N.my_from : N.max_seen;
        rt_net_progress = target > frame ? (int)((uint64_t)frame * 100 / target) : 99;
        net_tx_status(2, rt_net_progress);
    }
    if (rt_ff && !ff) {
        rt_trace("netplay: caught up at frame %u", frame);
        net_tx_status(0, 0);
        N.joined = 1;
    }
    rt_ff = ff;
}

/* The input delay, as lockstep games tune it: the others' inputs arriving 3+ frames before they are needed
   for a whole window (10 s) means a frame of delay to spare (the trips are alike both ways, and every game
   tunes alike); any wait gives one back at once. From the page's estimate down to that. */
static void tune_delay(uint32_t frame) {
    int was = N.cut;
    if (N.stalls && N.cut > 0) N.cut--;
    else if (!N.stalls && N.slack_min != 1 << 30 && N.slack_min >= 3 && N.delay > 2) N.cut++;
    if (N.cut != was)
        rt_trace("netplay: input delay %s at frame %u (the others' inputs %d frames early at least, %u waits)",
                 N.cut > was ? "down a frame" : "up a frame", frame, N.slack_min, N.stalls);
    N.slack_min = 1 << 30;
}

/* det_frame hook: runs first at every Present. */
void net_frame(uint32_t frame) {
    fault_inject(frame);
    N.frame = frame;
    read_local(frame);
    net_pump(0);
    if (N.my_from >= 0 && !N.offline) {   /* my input for every frame up to frame + delay, each once */
        int64_t own = (int64_t)frame + N.delay, upto = own;
        /* joining: idle inputs as far as the others have gone, so nobody waits for this game. Only then:
           a game in step sends its real inputs (idle ones ahead would drop its presses and let the others
           run away from it, which would make it catch up again, and so on) */
        if (rt_ff && !N.joined && (int64_t)N.max_seen + N.delay > upto) upto = (int64_t)N.max_seen + N.delay;
        if (rt_ff && !N.joined && N.front + N.delay + 30 > upto) upto = N.front + N.delay + 30;
        for (int64_t f = N.next_send; f <= upto; f++) send_input(f, f > own);
    }
    if (N.auth && !N.offline) admit(frame);
    exchange_hash(frame);
    net_leave_step(frame);
    wait_inputs(frame);
    apply_inputs(frame);
    trace_applied(frame);
    if (frame >= 2 * NET_HASH_EVERY && frame % NET_HASH_EVERY == NET_HASH_EVERY - 1)
        check_hashes(frame - NET_HASH_EVERY);
    catch_up(frame);
    if (frame % 600 == 0 && !rt_ff && !N.offline) {   /* (checks: tools/net/ls_lag_check.py) */
        rt_trace("netplay: pace at frame %u: delay %d inputs first by peer %u by relay %u stalls %u", frame, N.delay,
                 N.by_peer, N.by_relay, N.stalls);
        tune_delay(frame);
        N.by_peer = N.by_relay = N.stalls = 0;
    }
}
