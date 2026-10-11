/* netplay.c -- online play: deterministic lockstep over the relay server (server/).
 * Every instance runs the whole game in deterministic mode (det.c) and only player inputs travel.
 * At each Present (frame f) a player sends its pad for frame f + delay, then waits until every
 * player's input for frame f has arrived and applies them all (rt_pads), so all instances see the
 * same inputs at the same frame. Every NET_HASH_EVERY frames the instances exchange det_hash();
 * a mismatch is a desync and stops the session.
 *
 * Wire format (server relays binary messages to the other players of the room unchanged):
 *   input: 'I' slot u32 frame rt_pad_t       (little-endian, 18 bytes)
 *   hash:  'H' slot u32 frame u64 hash        (14 bytes)
 * Lobby messages are JSON text (server/main.go). Transport: net_tx_* (platform). */
#include "rt.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rt_net;                           /* a netplay session is running */
static int g_slot, g_players, g_delay;
static rt_pad_t g_local;              /* this player's pad (devices, or replay "pad 0" events) */

#define RING 512
typedef struct { uint32_t frame; uint8_t have; rt_pad_t pad; } slot_in_t;
static slot_in_t g_in[RING][RT_MAX_PADS];
#define NET_HASH_EVERY 120
static uint64_t g_hash[RING / NET_HASH_EVERY + 2][RT_MAX_PADS];
static uint32_t g_hash_frame[RING / NET_HASH_EVERY + 2][RT_MAX_PADS];

rt_pad_t *rt_net_local_pad(void) { return &g_local; }
int rt_net_slot(void) { return g_slot; }

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void store_input(int slot, uint32_t frame, const rt_pad_t *pad) {
    slot_in_t *s = &g_in[frame % RING][slot];
    s->frame = frame; s->have = 1; s->pad = *pad;
}

static void on_message(const uint8_t *m, int n) {
    if (n >= 18 && m[0] == 'I' && m[1] < RT_MAX_PADS) {
        rt_pad_t p; memcpy(&p, m + 6, sizeof p);
        store_input(m[1], get32(m + 2), &p);
    } else if (n >= 14 && m[0] == 'H' && m[1] < RT_MAX_PADS) {
        uint32_t f = get32(m + 2), k = (f / NET_HASH_EVERY) % (RING / NET_HASH_EVERY + 2);
        uint64_t h = 0; for (int i = 7; i >= 0; i--) h = h << 8 | m[6 + i];
        g_hash[k][m[1]] = h; g_hash_frame[k][m[1]] = f;
    } else if (n > 0 && m[0] == '{' && strstr((const char *)m, "\"t\":\"left\"")) {
        rt_trace("netplay: a player left the session");
        exit(4);
    }
}

static void pump(int wait_ms) {
    uint8_t buf[2048];
    int n;
    while ((n = net_tx_recv(buf, sizeof buf - 1, wait_ms)) > 0) { buf[n] = 0; on_message(buf, n); wait_ms = 0; }
    if (n < 0) { rt_trace("netplay: connection lost"); exit(4); }
}

static void check_hashes(uint32_t frame) {
    uint32_t k = (frame / NET_HASH_EVERY) % (RING / NET_HASH_EVERY + 2);
    for (int i = 0; i < g_players; i++) {
        if (i == g_slot || g_hash_frame[k][i] != frame) continue;
        if (g_hash[k][i] != g_hash[k][g_slot]) {
            rt_trace("netplay: DESYNC at frame %u (player %d hash %016llX, mine %016llX)", frame, i,
                     (unsigned long long)g_hash[k][i], (unsigned long long)g_hash[k][g_slot]);
            exit(3);
        }
    }
}

/* det_frame hook: runs first at every Present. */
static void net_frame(uint32_t frame) {
    static long fault = -2;   /* RT_NET_FAULT=F (tests): flip one guest byte at frame F, a desync to detect */
    if (fault == -2) { const char *e = getenv("RT_NET_FAULT"); fault = e ? atol(e) : -1; }
    if ((long)frame == fault) wr8(RT_HEAP_LO + 0x100, (uint8_t)(rd8(RT_HEAP_LO + 0x100) ^ 0x5A));
    plat_local_pad(&g_local);   /* live devices (no-op in replay runs: replay events already set g_local) */
    uint8_t m[18];
    m[0] = 'I'; m[1] = (uint8_t)g_slot; put32(m + 2, frame + (uint32_t)g_delay); memcpy(m + 6, &g_local, sizeof g_local);
    store_input(g_slot, frame + (uint32_t)g_delay, &g_local);
    net_tx_send(m, sizeof m, 0);
    uint64_t h;
    if (det_hash_step(frame, NET_HASH_EVERY, &h)) {   /* frames frame-119..frame, one slice each */
        uint8_t hm[14];
        hm[0] = 'H'; hm[1] = (uint8_t)g_slot; put32(hm + 2, frame);
        for (int i = 0; i < 8; i++) hm[6 + i] = (uint8_t)(h >> (8 * i));
        uint32_t k = (frame / NET_HASH_EVERY) % (RING / NET_HASH_EVERY + 2);
        g_hash[k][g_slot] = h; g_hash_frame[k][g_slot] = frame;
        net_tx_send(hm, sizeof hm, 0);
        rt_trace("netplay: frame %u hash %016llX", frame, (unsigned long long)h);
    }
    uint32_t waited = 0;
    for (;;) {
        int all = 1;
        for (int i = 0; i < g_players; i++) {
            slot_in_t *s = &g_in[frame % RING][i];
            if (!s->have || s->frame != frame) { all = 0; break; }
        }
        if (all) break;
        pump(waited ? 5 : 0);
        if (++waited % 2000 == 0) rt_trace("netplay: waiting for players at frame %u", frame);
        if (waited == 20) net_tx_status(1);   /* ~0.1 s late: the page shows "waiting for..." */
    }
    if (waited >= 20) net_tx_status(0);
    pump(0);
    for (int i = 0; i < g_players; i++) rt_pads[i] = g_in[frame % RING][i].pad;
    if (frame >= 2 * NET_HASH_EVERY && frame % NET_HASH_EVERY == NET_HASH_EVERY - 1) check_hashes(frame - NET_HASH_EVERY);
}

/* --net URL --net-room CODE [--net-players N] [--net-name NAME] [--net-delay D]: join the room
 * (the first to join hosts it; it starts when N players are in). Returns 1 if a session started. */
int rt_net_start(int argc, char **argv) {
    const char *url = NULL, *room = NULL, *name = "player";
    int players = 2, delay = 4;
    for (int i = 1; i + 1 < argc; i++) {
        int s, p, d;   /* --net-session SLOT,PLAYERS,DELAY: the web lobby already connected and started */
        if (!strcmp(argv[i], "--net-session") && sscanf(argv[i + 1], "%d,%d,%d", &s, &p, &d) == 3) return rt_net_begin(s, p, d);
        if (!strcmp(argv[i], "--net")) url = argv[++i];
        else if (!strcmp(argv[i], "--net-room")) room = argv[++i];
        else if (!strcmp(argv[i], "--net-players")) players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-name")) name = argv[++i];
        else if (!strcmp(argv[i], "--net-delay")) delay = atoi(argv[++i]);
    }
    if (!url || !room) return 0;
    if (players < 1 || players > RT_MAX_PADS) players = 2;
    if (net_tx_open(url)) rt_fatal("netplay: cannot connect to %s", url);
    char msg[256];
    snprintf(msg, sizeof msg, "{\"t\":\"join\",\"room\":\"%s\",\"name\":\"%s\",\"players\":%d,\"delay\":%d,\"auto\":true}", room, name, players, delay);
    net_tx_send(msg, (int)strlen(msg), 1);
    char buf[2048];
    int n, slot = -1;
    while ((n = net_tx_recv(buf, sizeof buf - 1, 1000)) >= 0) {   /* lobby: until "start" */
        if (!n) continue;
        buf[n] = 0;
        const char *p;
        if ((p = strstr(buf, "\"slot\":"))) slot = atoi(p + 7);
        if (strstr(buf, "\"t\":\"error\"")) rt_fatal("netplay: %s", buf);
        if (strstr(buf, "\"t\":\"start\"")) {
            if ((p = strstr(buf, "\"players\":"))) players = atoi(p + 10);
            if ((p = strstr(buf, "\"delay\":"))) delay = atoi(p + 8);
            break;
        }
    }
    if (n < 0 || slot < 0) rt_fatal("netplay: lobby failed");
    return rt_net_begin(slot, players, delay);
}

/* Start the lockstep session (also called by the web lobby, platform/web). */
int rt_net_begin(int slot, int players, int delay) {
    g_slot = slot; g_players = players; g_delay = delay < 1 ? 1 : delay > 60 ? 60 : delay;
    rt_npads = players; rt_pads_first = 1;
    rt_det = 1; rt_net = 1;
    rt_pad_t zero = { 0 };
    for (uint32_t f = 0; f <= (uint32_t)g_delay; f++) for (int i = 0; i < players; i++) store_input(i, f, &zero);
    det_on_frame(net_frame);
    rt_trace("netplay: session started, player %d of %d, input delay %d frames", slot + 1, players, g_delay);
    return 1;
}
