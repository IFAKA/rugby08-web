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
 *   'I' slot u32 frame rt_pad_t   18 bytes  input        'H' slot u32 frame u64   14  state hash
 *   'A' slot u32 frame             6        slot joins   'D' slot u32 frame        6  slot leaves
 *   'W' slot                       2        (server -> authority) slot wants to join
 *   'R' slot                       2        (server -> new authority) you grant joins now
 * Lobby messages are JSON text (server/main.go). Transport: net_tx_* (platform). */
#include "rt.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_net;                           /* a netplay session is running */
int rt_ff;                            /* fast-forward (catching up): no drawing, no sound */
static int g_slot, g_auth, g_delay = 4, g_offline;
static rt_pad_t g_local;              /* this player's pad (devices, or replay "pad 0" events) */

#define RING 1024
#define AHEAD 600                     /* read no message for a frame this far ahead (ring room) */
typedef struct { uint32_t frame; uint8_t have; rt_pad_t pad; } slot_in_t;
static slot_in_t g_in[RING][RT_MAX_PADS];
#define NET_HASH_EVERY 120
#define HRING 16
static uint64_t g_hash[HRING][RT_MAX_PADS];
static uint32_t g_hash_frame[HRING][RT_MAX_PADS];

typedef struct { uint32_t frame; uint8_t on; } ev_t;   /* roster changes per slot, in frame order */
static ev_t g_ev[RT_MAX_PADS][32];
static int g_nev[RT_MAX_PADS];
static uint8_t g_active[RT_MAX_PADS]; /* roster at the frame being resolved */
static int64_t g_my_from = -1;        /* my slot plays from this frame (-1: not granted yet) */
static int64_t g_next_send;           /* first frame I have not sent an input for */
static uint32_t g_max_seen, g_frame;  /* highest input frame received; frame being resolved */
static uint8_t g_want[RT_MAX_PADS];   /* join requests to grant (authority) */
static uint8_t g_rx[1 << 16];         /* received binary bytes not handled yet */
static int g_rx_n;

rt_pad_t *rt_net_local_pad(void) { return &g_local; }
int rt_net_slot(void) { return g_slot; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void store_input(int slot, uint32_t frame, const rt_pad_t *pad) {
    slot_in_t *s = &g_in[frame % RING][slot];
    s->frame = frame; s->have = 1; s->pad = *pad;
}
static int have_input(int slot, uint32_t frame) { slot_in_t *s = &g_in[frame % RING][slot]; return s->have && s->frame == frame; }

static void add_event(int slot, uint32_t frame, int on) {
    if (g_nev[slot] == 32) { rt_trace("netplay: roster overflow for slot %d", slot); return; }
    g_ev[slot][g_nev[slot]++] = (ev_t){ frame, (uint8_t)on };
    rt_trace("netplay: player %d %s at frame %u", slot + 1, on ? "joins" : "leaves", frame);
}
static void roster_at(uint32_t frame) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        while (g_nev[k] && g_ev[k][0].frame <= frame) {
            g_active[k] = g_ev[k][0].on;
            memmove(&g_ev[k][0], &g_ev[k][1], (size_t)--g_nev[k] * sizeof(ev_t));
        }
}

static void send_grant(int slot, uint32_t frame) {
    uint8_t m[6] = { 'A', (uint8_t)slot };
    put32(m + 2, frame);
    net_tx_send(m, sizeof m, 0);
}

static void handle(const uint8_t *m) {
    int k = m[1] < RT_MAX_PADS ? m[1] : -1;
    if (k < 0) return;
    uint32_t f = m[0] == 'W' || m[0] == 'R' ? 0 : get32(m + 2);
    switch (m[0]) {
    case 'I': { rt_pad_t p; memcpy(&p, m + 6, sizeof p); store_input(k, f, &p); if (f > g_max_seen) g_max_seen = f; break; }
    case 'H': { uint64_t h = 0; for (int i = 7; i >= 0; i--) h = h << 8 | m[6 + i];
                g_hash[(f / NET_HASH_EVERY) % HRING][k] = h; g_hash_frame[(f / NET_HASH_EVERY) % HRING][k] = f; break; }
    case 'A': add_event(k, f, 1); if (k == g_slot) { g_my_from = f; g_next_send = f; } break;
    case 'D': add_event(k, f, 0);
              if (k == g_slot && g_my_from >= 0) { rt_trace("netplay: removed from the session"); exit(4); }
              break;
    case 'W': g_want[k] = 1; break;
    case 'R': g_auth = 1; rt_trace("netplay: this player now admits newcomers"); break;
    }
}

static int msg_len(uint8_t t) { return t == 'I' ? 18 : t == 'H' ? 14 : t == 'A' || t == 'D' ? 6 : t == 'W' || t == 'R' ? 2 : 0; }

/* Handle buffered messages in order; 0 = stopped at one for a frame too far ahead (kept for later). */
static int consume(void) {
    int p = 0, ok = 1;
    while (p < g_rx_n) {
        const uint8_t *m = g_rx + p;
        int len = msg_len(m[0]);
        if (!len) { rt_trace("netplay: bad message type %02X", m[0]); p = g_rx_n; break; }
        if (p + len > g_rx_n) break;
        if ((m[0] == 'I' || m[0] == 'H') && get32(m + 2) >= g_frame + AHEAD) { ok = 0; break; }
        handle(m);
        p += len;
    }
    memmove(g_rx, g_rx + p, (size_t)(g_rx_n - p));
    g_rx_n -= p;
    return ok;
}

static void go_offline(void) {
    if (g_offline) return;
    g_offline = 1; rt_ff = 0;
    rt_trace("netplay: connection lost, playing on offline");
    net_tx_status(3, 0);
}

static void pump(int wait_ms) {
    while (!g_offline && consume() && g_rx_n < (int)sizeof g_rx - 4096) {
        int n = net_tx_recv(g_rx + g_rx_n, (int)sizeof g_rx - g_rx_n, wait_ms);
        if (n < 0) { go_offline(); return; }
        if (n == 0) return;
        if (g_rx[g_rx_n] != '{') g_rx_n += n;   /* text (lobby JSON) is the page's business */
        wait_ms = 0;
    }
}

static void check_hashes(uint32_t frame) {
    uint32_t k = (frame / NET_HASH_EVERY) % HRING;
    if (g_hash_frame[k][g_slot] != frame) return;
    for (int i = 0; i < RT_MAX_PADS; i++) {
        if (i == g_slot || g_hash_frame[k][i] != frame || g_hash[k][i] == g_hash[k][g_slot]) continue;
        rt_trace("netplay: DESYNC at frame %u (player %d hash %016llX, mine %016llX)", frame, i + 1,
                 (unsigned long long)g_hash[k][i], (unsigned long long)g_hash[k][g_slot]);
        if (i < g_slot) exit(3);   /* the lower slot is the reference: this game rebuilds itself */
    }
}

/* det_frame hook: runs first at every Present. */
static void net_frame(uint32_t frame) {
    static long fault = -2;   /* RT_NET_FAULT=F (tests): flip one guest byte at frame F, a desync to detect */
    if (fault == -2) { const char *e = getenv("RT_NET_FAULT"); fault = e ? atol(e) : -1; }
    if ((long)frame == fault) wr8(RT_HEAP_LO + 0x100, (uint8_t)(rd8(RT_HEAP_LO + 0x100) ^ 0x5A));
    g_frame = frame;
    plat_local_pad(&g_local);   /* live devices (no-op in replay runs: replay events already set g_local) */
    {   static rt_pad_t last;   /* trace input changes (checks: tools/net_live_input.py) */
        if (memcmp(&last, &g_local, sizeof last)) { rt_trace("netplay: local input frame %u buttons %04X stick %d,%d", frame, g_local.buttons, g_local.lx, g_local.ly); last = g_local; } }
    { int d = net_tx_delay(); if (d > 0) g_delay = d > 60 ? 60 : d; }
    pump(0);
    if (g_my_from >= 0 && !g_offline) {   /* my input for every frame up to frame + delay, each once */
        for (int64_t f = g_next_send; f <= (int64_t)frame + g_delay; f++) {
            uint8_t m[18] = { 'I', (uint8_t)g_slot };
            put32(m + 2, (uint32_t)f); memcpy(m + 6, &g_local, sizeof g_local);
            store_input(g_slot, (uint32_t)f, &g_local);
            net_tx_send(m, sizeof m, 0);
            g_next_send = f + 1;
        }
    }
    if (g_auth && !g_offline)   /* admit newcomers at a frame past every input this player has sent */
        for (int k = 0; k < RT_MAX_PADS; k++) {
            if (!g_want[k]) continue;
            g_want[k] = 0;
            int64_t j = g_next_send;
            if ((int64_t)g_max_seen + 1 > j) j = (int64_t)g_max_seen + 1;
            if ((int64_t)frame + g_delay + 1 > j) j = (int64_t)frame + g_delay + 1;
            send_grant(k, (uint32_t)j);
            handle((const uint8_t[6]){ 'A', (uint8_t)k, (uint8_t)j, (uint8_t)(j >> 8), (uint8_t)(j >> 16), (uint8_t)(j >> 24) });
        }
    uint64_t h;
    if (det_hash_step(frame, NET_HASH_EVERY, &h)) {   /* frames frame-119..frame, one slice each */
        uint32_t k = (frame / NET_HASH_EVERY) % HRING;
        g_hash[k][g_slot] = h; g_hash_frame[k][g_slot] = frame;
        if (g_my_from >= 0 && frame >= g_my_from && !g_offline) {
            uint8_t hm[14] = { 'H', (uint8_t)g_slot };
            put32(hm + 2, frame);
            for (int i = 0; i < 8; i++) hm[6 + i] = (uint8_t)(h >> (8 * i));
            net_tx_send(hm, sizeof hm, 0);
        }
        rt_trace("netplay: frame %u hash %016llX", frame, (unsigned long long)h);
    }
    uint32_t waited = 0;
    for (;;) {
        roster_at(frame);
        int who = -1;
        for (int k = 0; k < RT_MAX_PADS && who < 0; k++) if (g_active[k] && !have_input(k, frame)) who = k;
        if (who < 0 || g_offline) break;
        pump(waited ? 5 : 0);
        if (++waited == 20 && !rt_ff) net_tx_status(1, who);   /* ~0.1 s late: the page shows who we wait for */
        if (waited % 2000 == 0) rt_trace("netplay: waiting for player %d at frame %u", who + 1, frame);
    }
    if (waited >= 20 && !rt_ff && !g_offline) net_tx_status(0, 0);
    static const rt_pad_t zero;
    for (int k = 0; k < RT_MAX_PADS; k++)
        rt_pads[k] = !g_offline && g_active[k] && have_input(k, frame) ? g_in[frame % RING][k].pad : zero;
    if (g_offline) rt_pads[g_slot] = g_local;
    {   static int tr = -1;   /* RT_NET_TRACE=1: the inputs applied at every frame (debugging a desync) */
        if (tr < 0) tr = getenv("RT_NET_TRACE") != NULL;
        if (tr) { uint32_t c = 0; const uint8_t *b = (const uint8_t *)rt_pads;
                  for (size_t i = 0; i < sizeof rt_pads; i++) c = c * 31 + b[i];
                  rt_trace("netplay: applied frame %u roster %d%d%d%d pads %08X", frame, g_active[0], g_active[1], g_active[2], g_active[3], c); } }
    if (frame >= 2 * NET_HASH_EVERY && frame % NET_HASH_EVERY == NET_HASH_EVERY - 1) check_hashes(frame - NET_HASH_EVERY);
    /* catching up: until this player's slot plays, or while far behind the others */
    int ff = !g_offline && (g_my_from < 0 || (int64_t)frame < g_my_from || g_max_seen > frame + 2u * g_delay + 60);
    if (ff && frame % 30 == 0) {
        uint32_t target = g_my_from > (int64_t)frame ? (uint32_t)g_my_from : g_max_seen;
        net_tx_status(2, target > frame ? (int)((uint64_t)frame * 100 / target) : 99);
    }
    if (rt_ff && !ff) { rt_trace("netplay: caught up at frame %u", frame); net_tx_status(0, 0); }
    rt_ff = ff;
}

/* --net URL --net-room CODE [--net-name NAME] [--net-delay D]: join the room (the first to join creates
 * it and plays from frame 0; later ones catch up). Returns 1 if a session started. */
static int g_cfg_delay;
int rt_net_start(int argc, char **argv) {
    const char *url = NULL, *room = NULL, *name = "player";
    for (int i = 1; i + 1 < argc; i++) {
        int s, c;   /* --net-session SLOT,CREATOR: the web page already joined the room */
        if (!strcmp(argv[i], "--net-session") && sscanf(argv[i + 1], "%d,%d", &s, &c) == 2) return rt_net_begin(s, c);
        if (!strcmp(argv[i], "--net")) url = argv[++i];
        else if (!strcmp(argv[i], "--net-room")) room = argv[++i];
        else if (!strcmp(argv[i], "--net-name")) name = argv[++i];
        else if (!strcmp(argv[i], "--net-delay")) g_cfg_delay = atoi(argv[++i]);
    }
    if (!url || !room) return 0;
    if (net_tx_open(url)) rt_fatal("netplay: cannot connect to %s", url);
    char msg[256];
    snprintf(msg, sizeof msg, "{\"t\":\"join\",\"room\":\"%s\",\"name\":\"%s\",\"auto\":true}", room, name);
    net_tx_send(msg, (int)strlen(msg), 1);
    char buf[2048];
    int n;
    while ((n = net_tx_recv(buf, sizeof buf - 1, 1000)) >= 0) {   /* until "joined" */
        if (!n || buf[0] != '{') continue;
        buf[n] = 0;
        const char *p;
        if (strstr(buf, "\"t\":\"error\"")) rt_fatal("netplay: %s", buf);
        if (strstr(buf, "\"t\":\"joined\"") && (p = strstr(buf, "\"slot\":")))
            return rt_net_begin(atoi(p + 7), strstr(buf, "\"creator\":true") != NULL);
    }
    rt_fatal("netplay: lobby failed");
    return 0;
}

/* Start the lockstep session (also called by the web page, platform/web). */
int rt_net_begin(int slot, int creator) {
    g_slot = slot; g_auth = creator;
    if (g_cfg_delay > 0) g_delay = g_cfg_delay;
    rt_npads = RT_MAX_PADS; rt_pads_first = 1;   /* every controller exists from boot: joiners plug in */
    rt_det = 1; rt_net = 1;
    add_event(0, 0, 1);   /* the room's creator plays from the first frame */
    if (creator) { g_my_from = 0; g_next_send = 0; }
    rt_ff = !creator;
    det_on_frame(net_frame);
    rt_trace("netplay: session started as player %d%s", slot + 1, creator ? " (created the room)" : ", catching up");
    return 1;
}
