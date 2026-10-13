/* netplay.c -- online play: deterministic lockstep over the relay server (server/), drop-in.
 * Every instance runs the whole game in deterministic mode (det.c) from boot and only player inputs
 * travel. The room's creator plays from frame 0; anyone may join later: the server replays the
 * room's message log to the newcomer, whose game re-runs every frame since boot (fast, without
 * drawing or sound) until it reaches the present, then plays along as one more controller.
 *
 * Lockstep: at each Present (frame f) a player sends its pad for every frame up to f + delay (its
 * own delay, which may change: a sender simply covers each frame once), then waits until every
 * player active at frame f has an input for f, and applies them all (rt_pads).
 *
 * Roster: which slots play at which frame is part of the message stream, so every instance applies a
 * change at the same frame. A join is granted by the roster authority (the creator; the server hands
 * the role on if it leaves) as "A slot J" with J beyond every input it has sent, so nobody can have
 * passed frame J without first seeing the grant. A leave is decided by the server as "D slot L", L
 * just past the leaver's last input, sent after all of them.
 *
 * Desync check: every NET_HASH_EVERY frames the players exchange det_hash_step(). Whoever disagrees
 * with a lower slot is the one out of sync: it stops (exit 3) and the page rejoins, i.e. it rebuilds
 * its game from the log. Lost connection: the game carries on offline (it is a complete game).
 *
 * Wire format (little-endian; the server relays and logs binary messages, possibly several per frame):
 *   'I' slot u32 frame rt_pad_t n ev[n]   19 + 6n  input  'H' slot u32 frame u64   14  state hash
 *       (ev: the keyboard and mouse events of the host, controller 1: plat_desk_ev_t, applied by all)
 *   'A' slot u32 frame             6        slot joins   'D' slot u32 frame        6  slot leaves
 *   'W' slot                       2        (server -> authority) slot wants to join
 *   'R' slot                       2        (server -> new authority) you grant joins now
 *   'X' slot u32 frame u32 seq u32 len data   14 + len  external data (net_external.c)
 * Lobby messages are JSON text (server/main.go). Transport: net_tx_* (platform). */
#include "netplay_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_net;   /* a netplay session is running */
int rt_ff;    /* fast-forward (catching up): no drawing, no sound */
int rt_net_progress;   /* catching up: percent done (the game shows it: ui_badge.c) */
net_state_t N = { .delay = 4, .my_from = -1 };

rt_pad_t *rt_net_local_pad(void) { return &N.local; }
int rt_net_slot(void) { return N.slot; }
int rt_net_active(int k) { return k >= 0 && k < RT_MAX_PADS && N.active[k]; }

void net_store_input(int slot, uint32_t frame, const rt_pad_t *pad, int nd, const plat_desk_ev_t *d) {
    slot_in_t *s = &N.in[frame % RING][slot];
    s->frame = frame;
    s->have = 1;
    s->pad = *pad;
    s->nd = (uint8_t)(nd < NDESK ? nd : NDESK);
    memcpy(s->d, d, s->nd * sizeof *d);
}
int net_have_input(int slot, uint32_t frame) {
    slot_in_t *s = &N.in[frame % RING][slot];
    return s->have && s->frame == frame;
}

static void add_event(int slot, uint32_t frame, int on) {
    if (N.nev[slot] == 32) {
        rt_trace("netplay: roster overflow for slot %d", slot);
        return;
    }
    N.ev[slot][N.nev[slot]++] = (ev_t){ frame, (uint8_t)on };
    rt_trace("netplay: player %d %s at frame %u", slot + 1, on ? "joins" : "leaves", frame);
}
void net_roster_at(uint32_t frame) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        while (N.nev[k] && N.ev[k][0].frame <= frame) {
            N.active[k] = N.ev[k][0].on;
            memmove(&N.ev[k][0], &N.ev[k][1], (size_t)--N.nev[k] * sizeof(ev_t));
        }
}

void net_send_grant(int slot, uint32_t frame) {
    uint8_t m[6] = { 'A', (uint8_t)slot };
    put32(m + 2, frame);
    net_tx_send(m, sizeof m, 0);
}

void net_handle(const uint8_t *m) {
    int k = m[1] < RT_MAX_PADS ? m[1] : -1;
    if (k < 0) return;
    uint32_t f = m[0] == 'W' || m[0] == 'R' ? 0 : get32(m + 2);
    switch (m[0]) {
    case 'I': {
        rt_pad_t p;
        plat_desk_ev_t d[NDESK];
        int nd = m[18] < NDESK ? m[18] : NDESK;
        memcpy(&p, m + 6, sizeof p);
        for (int i = 0; i < nd; i++) {
            const uint8_t *e = m + 19 + 6 * i;
            d[i] = (plat_desk_ev_t){ e[0], e[1], (int16_t)(e[2] | e[3] << 8), (int16_t)(e[4] | e[5] << 8) };
        }
        net_store_input(k, f, &p, nd, d);
        if (f > N.max_seen) N.max_seen = f;
        break;
    }
    case 'H': {
        uint64_t h = 0;
        for (int i = 7; i >= 0; i--) h = h << 8 | m[6 + i];
        N.hash[(f / NET_HASH_EVERY) % HRING][k] = h;
        N.hash_frame[(f / NET_HASH_EVERY) % HRING][k] = f;
        break;
    }
    case 'A':
        add_event(k, f, 1);
        if (k == N.slot) {   /* (the server may have told this game already: net_hint) */
            N.my_from = f;
            if (N.next_send < (int64_t)f) N.next_send = f;
        }
        break;
    case 'D':
        add_event(k, f, 0);
        if (k == N.slot && N.my_from >= 0) {
            rt_trace("netplay: removed from the session");
            exit(4);
        }
        break;
    case 'X': net_external_rx(m); break;
    case 'W': N.want[k] = 1; break;
    case 'R':
        N.auth = 1;
        rt_trace("netplay: this player now admits newcomers");
        break;
    }
}

/* Length of the message at m (0: unknown type); avail bytes are there (an 'I' needs 19 to know). */
static int msg_len(const uint8_t *m, int avail) {
    switch (m[0]) {
    case 'I': return avail < 19 ? 19 : 19 + 6 * m[18];
    case 'H': return 14;
    case 'A':
    case 'D': return 6;
    case 'W':
    case 'R': return 2;
    case 'X': return avail < 14 ? 14 : 14 + (int)get32(m + 10);
    }
    return 0;
}

/* Handle buffered messages in order; 0 = stopped at one for a frame too far ahead (kept for later). */
static int consume(void) {
    int p = 0, ok = 1;
    while (p < N.rx_n) {
        const uint8_t *m = N.rx + p;
        int len = msg_len(m, N.rx_n - p);
        if (!len) {
            rt_trace("netplay: bad message type %02X", m[0]);
            p = N.rx_n;
            break;
        }
        if (p + len > N.rx_n) break;
        if ((m[0] == 'I' || m[0] == 'H') && get32(m + 2) >= N.frame + AHEAD) {
            ok = 0;
            break;
        }
        net_handle(m);
        p += len;
    }
    memmove(N.rx, N.rx + p, (size_t)(N.rx_n - p));
    N.rx_n -= p;
    return ok;
}

static void go_offline(void) {
    if (N.offline) return;
    N.offline = 1;
    rt_ff = 0;
    rt_trace("netplay: connection lost, playing on offline");
    net_tx_status(3, 0);
}

/* The server's hints to a game catching up (JSON, ahead of the stream it has not replayed yet): the frame
   its slot plays from ("granted") and how far the others have sent inputs ("front"), so it can send idle
   inputs that far and nobody waits for it. The page passes them on (net_tx_hints); natively they come
   as text in the stream. */
static void hint(int64_t grant, int64_t front) {
    if (grant >= 0 && N.my_from < 0) {
        N.my_from = grant;
        if (N.next_send < grant) N.next_send = grant;
        rt_trace("netplay: plays from frame %lld (told ahead)", (long long)grant);
    }
    if (front > N.front) N.front = front;
}
static void hint_json(const char *s, int n) {
    char b[160];
    snprintf(b, sizeof b, "%.*s", n < 159 ? n : 159, s);
    const char *p;
    if (strstr(b, "\"granted\"") && (p = strstr(b, "\"from\":"))) hint(atoll(p + 7), -1);
    if (strstr(b, "\"front\"") && (p = strstr(b, "\"frame\":"))) hint(-1, atoll(p + 8));
}

void net_pump(int wait_ms) {
    int64_t g, f;
    if (net_tx_hints(&g, &f)) hint(g, f);
    while (!N.offline) {
        consume();   /* (it may stop at a message for a frame too far ahead: reading goes on, for the hints) */
        if (N.rx_n >= (int)sizeof N.rx - (1 << 20) - 64) return;   /* (room for an 'X') */
        int n = net_tx_recv(N.rx + N.rx_n, (int)sizeof N.rx - N.rx_n, wait_ms);
        if (n < 0) {
            go_offline();
            return;
        }
        if (n == 0) return;
        if (N.rx[N.rx_n] == '{') hint_json((const char *)N.rx + N.rx_n, n);   /* text: lobby JSON */
        else N.rx_n += n;
        wait_ms = 0;
    }
}

/* Start the lockstep session (also called by the web page, platform/web). */
int rt_net_begin(int slot, int creator) {
    N.slot = slot;
    N.auth = creator;
    if (rt_net_cfg_delay > 0) N.delay = rt_net_cfg_delay;
    rt_npads = RT_MAX_PADS;
    rt_pads_first = 1;   /* every controller exists from boot: joiners plug in */
    rt_det = 1;
    rt_net = 1;
    add_event(0, 0, 1);   /* the room's creator plays from the first frame */
    if (creator) {
        N.my_from = 0;
        N.next_send = 0;
    }
    rt_ff = !creator;
    plat_desk_capture(slot == DESK_SLOT);   /* the host plays with the game's own keyboard and mouse */
    det_on_frame(net_frame);
    rt_trace("netplay: session started as player %d%s", slot + 1, creator ? " (created the room)" : ", catching up");
    return 1;
}
