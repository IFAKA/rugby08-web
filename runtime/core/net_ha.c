/* net_ha.c -- online play, host-authoritative (on.match_net; plan docs/plans/2026-10-12-online-host-
 * authoritative.md). Only the host plays the match: friends' pads are its controllers 2..4, and each
 * match frame it records goes to the friends, whose games show it (match_puppet.c) instead of playing
 * their own. Rooms and the relay are netplay's (server/, net_join.c: --net-mode ha).
 *
 * Wire (little-endian, after netplay's types; the server relays these and does not log them):
 *   {u8 type, u8 slot, u32 frame, u16 len, len bytes}
 *   'S' setup   host -> friends at the match launch: MS_SETUP_BYTES (match_setup.c); the server keeps it
 *   'F' frame   host -> friends, one per recorded frame: u8 kind, the 96-byte head (match_replay.c: score,
 *               view), then kind 0 (key) the raw 0x118C-byte frame, kind 1 the game's own record of the
 *               frame (its replay coder, ~380 bytes); the server keeps the last key for newcomers
 *   'K' key     friend -> host: send a key next
 *   'P' pad     friend -> host every Present: rt_pad_t
 *   'E' end     host -> friends: its match is over      'Z' pause   friend -> host: it paused
 *   'U' paused  host -> friends: u8 paused or not        (the party flow: net_ha_flow.c)
 * A key goes first, on request and every KEY_EVERY frames; a friend decodes the records after the last
 * key in order with its own game's decoder (the same tables in every game: RT_MS_STREAM_CHECK). */
#include "netplay_int.h"
#include "match_state.h"
#include "../debug/debug.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HDR        8
#define HEAD       96
#define FRAME_SIZE 0x118Cu
#define KEY_EVERY  600
#define PAD_STALE  60   /* frames without a friend's pad: its controller lets go */

int rt_net_ha;   /* 1 host, 2 friend */
static uint8_t g_rx[1 << 16];
static int g_rx_n;
static int g_want_key = 1, g_since_key;
static rt_pad_t g_pad[RT_MAX_PADS];
static uint32_t g_pad_at[RT_MAX_PADS];
static uint32_t g_frame;

/* a friend's frames since the last key: whole 'F' payloads, decoded in order */
static uint8_t *g_pk;
static size_t g_pk_n, g_pk_cap;
static uint32_t g_dec;                      /* guest frame the records are decoded into */
static size_t g_dec_at;                     /* bytes of g_pk already decoded */
static uint8_t g_newest[HEAD + FRAME_SIZE]; /* head and frame of the newest decoded */
static int g_have, g_fresh;
static int g_host_phase = -1;               /* the phase id in the newest head received */
static uint32_t g_rx_at;                    /* Present at which the last frame arrived */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void send(char type, uint32_t frame, const void *a, size_t na, const void *b, size_t nb) {
    static uint8_t m[HDR + 0xFFFF];   /* the largest message: the setup (MS_SETUP_BYTES) */
    size_t len = na + nb;
    if (len > 0xFFFF) {
        rt_log("ms: net message '%c' of %zu bytes does not fit", type, len);
        return;
    }
    m[0] = (uint8_t)type;
    m[1] = (uint8_t)N.slot;
    put32(m + 2, frame);
    m[6] = (uint8_t)len;
    m[7] = (uint8_t)(len >> 8);
    if (na) memcpy(m + HDR, a, na);
    if (nb) memcpy(m + HDR + na, b, nb);
    net_tx_send(m, (int)(HDR + len), 0);
}

int net_ha_rx_recent(uint32_t frame, uint32_t within) { return g_rx_at && frame - g_rx_at <= within; }

void net_ha_send(char type, uint32_t frame, const void *payload, size_t len) {
    send(type, frame, payload, len, NULL, 0);
}

/* host: match_setup.c at the launch; the new match's first frame is a key */
void net_ha_setup(const uint8_t *setup) {
    if (rt_net_ha != 1) return;
    send('S', g_frame, setup, MS_SETUP_BYTES, NULL, 0);
    g_want_key = 1;
}

/* host: match_replay.c for each recorded frame: head (96 bytes), the frame (guest), the game's record of
   it (guest, rec_len bytes; 0: none) */
void net_ha_frame_out(uint32_t frame_no, const uint8_t *head, uint32_t frame, uint32_t rec, uint32_t rec_len) {
    if (rt_net_ha != 1) return;
    uint8_t kind_head[1 + HEAD];
    int key = g_want_key || !rec_len || ++g_since_key >= KEY_EVERY;
    kind_head[0] = key ? 0 : 1;
    memcpy(kind_head + 1, head, HEAD);
    if (key) {
        g_want_key = g_since_key = 0;
        send('F', frame_no, kind_head, sizeof kind_head, G2H(frame), FRAME_SIZE);
    } else send('F', frame_no, kind_head, sizeof kind_head, G2H(rec), rec_len);
    rt_trace("ms: net tx %u %s ns %llu", frame_no, key ? "key" : "rec", (unsigned long long)now_ns());
}

static void store_frame(uint32_t frame_no, const uint8_t *p, uint16_t len) {
    if (len < 1 + HEAD) return;
    if (p[0] == 0) g_pk_n = g_dec_at = 0;   /* a key: what came before is not needed */
    else if (!g_pk_n) return;               /* a record without its key: wait for one */
    if (g_pk_n + 2u + len > g_pk_cap) {
        size_t cap = (g_pk_n + 2u + len) * 2;
        uint8_t *grown = realloc(g_pk, cap);
        if (!grown) return;
        g_pk = grown;
        g_pk_cap = cap;
    }
    g_pk[g_pk_n] = (uint8_t)len;
    g_pk[g_pk_n + 1] = (uint8_t)(len >> 8);
    memcpy(g_pk + g_pk_n + 2, p, len);
    g_pk_n += 2u + len;
    memcpy(&g_host_phase, p + 1 + 8, 4);
    g_rx_at = g_frame;
    rt_trace("ms: net rx %u %s ns %llu", frame_no, p[0] ? "rec" : "key", (unsigned long long)now_ns());
}

static void handle(const uint8_t *m) {
    int k = m[1] < RT_MAX_PADS ? m[1] : -1;
    uint16_t len = (uint16_t)(m[6] | m[7] << 8);
    if (k < 0 || k == N.slot) return;
    switch (m[0]) {
    case 'S':
        if (rt_net_ha == 2 && len == MS_SETUP_BYTES) net_ha_flow_setup(m + HDR);
        break;
    case 'E':   /* the host's match is over: its frames are history */
        if (rt_net_ha != 2) break;
        g_pk_n = g_dec_at = 0;
        g_have = 0;
        net_ha_flow_end();
        break;
    case 'F':
        if (rt_net_ha == 2) store_frame(get32(m + 2), m + HDR, len);
        break;
    case 'K': g_want_key = 1; break;
    case 'Z':
        if (rt_net_ha == 1) net_ha_flow_pause_request();
        break;
    case 'U':
        if (rt_net_ha == 2 && len == 1) net_ha_flow_host_paused(m[HDR]);
        break;
    case 'P':
        if (rt_net_ha == 1 && len == sizeof(rt_pad_t)) {
            if (memcmp(&g_pad[k], m + HDR, sizeof(rt_pad_t)) != 0)
                rt_trace("ms: net pad %d frame %u buttons %04X stick %d,%d", k, g_frame,
                         (unsigned)((const rt_pad_t *)(m + HDR))->buttons, ((const rt_pad_t *)(m + HDR))->lx,
                         ((const rt_pad_t *)(m + HDR))->ly);
            memcpy(&g_pad[k], m + HDR, sizeof(rt_pad_t));
            g_pad_at[k] = g_frame;
        }
        break;
    }
}

/* Length of the message at m: netplay's types (lockstep traffic is not expected here: skipped), ours. */
static int msg_len(const uint8_t *m, int avail) {
    switch (m[0]) {
    case 'S':
    case 'F':
    case 'K':
    case 'P':
    case 'E':
    case 'Z':
    case 'U': return avail < HDR ? HDR : HDR + (m[6] | m[7] << 8);
    case 'I': return avail < 19 ? 19 : 19 + 6 * m[18];
    case 'H': return 14;
    case 'A':
    case 'D': return 6;
    case 'W':
    case 'R': return 2;
    }
    return 0;
}

static void pump(void) {
    for (;;) {
        int n = net_tx_recv(g_rx + g_rx_n, (int)sizeof g_rx - g_rx_n, 0);
        if (n <= 0) break;
        if (g_rx[g_rx_n] != '{') g_rx_n += n;   /* text: lobby JSON */
        int p = 0;
        while (p < g_rx_n) {
            int len = msg_len(g_rx + p, g_rx_n - p);
            if (!len) {
                rt_trace("ms: net bad message type %02X", g_rx[p]);
                p = g_rx_n;
                break;
            }
            if (p + len > g_rx_n) break;
            handle(g_rx + p);
            p += len;
        }
        memmove(g_rx, g_rx + p, (size_t)(g_rx_n - p));
        g_rx_n -= p;
    }
}

/* At every Present, before the frame's input reaches the game (d3d8 Present). */
void net_ha_present(uint32_t frame) {
    if (!rt_net_ha) return;
    g_frame = frame;
    plat_local_pad(&N.local);   /* devices; in replay runs the replay's pad events are already there */
    pump();
    net_ha_flow_present(frame);
    rt_pads[N.slot] = N.local;
    if (rt_net_ha == 1) {
        static const rt_pad_t zero;
        for (int k = 0; k < RT_MAX_PADS; k++)
            if (k != N.slot) rt_pads[k] = g_pad_at[k] && frame - g_pad_at[k] < PAD_STALE ? g_pad[k] : zero;
        return;
    }
    net_ha_test_press(&N.local, g_host_phase, frame);
    rt_pads[N.slot] = N.local;
    send('P', frame, &N.local, sizeof N.local, NULL, 0);
    if (g_want_key) {
        send('K', frame, NULL, 0, NULL, 0);
        g_want_key = 0;
    }
}

/* friend (match_puppet.c): the newest host frame, all received records decoded in order. Returns its
   96-byte head followed by the frame (host memory), or NULL before the first; *fresh: newer than the
   last call. Needs this game's replay manager (its decoder tables). */
const uint8_t *net_ha_newest(int *fresh) {
    *fresh = 0;
    if (rt_net_ha != 2 || !ms_replay_manager()) return g_have ? g_newest : NULL;
    static uint32_t rec;
    if ((!g_dec && !(g_dec = gcalloc(FRAME_SIZE))) || (!rec && !(rec = gcalloc(0x10000)))) return NULL;
    while (g_dec_at < g_pk_n) {
        uint16_t len = (uint16_t)(g_pk[g_dec_at] | g_pk[g_dec_at + 1] << 8);
        const uint8_t *p = g_pk + g_dec_at + 2;
        if (p[0] == 0) memcpy(G2H(g_dec), p + 1 + HEAD, FRAME_SIZE);
        else {
            memcpy(G2H(rec), p + 1 + HEAD, len - 1u - HEAD);
            ms_replay_decode(g_dec, rec);
        }
        memcpy(g_newest, p + 1, HEAD);
        g_dec_at += 2u + len;
        g_have = g_fresh = 1;
    }
    if (g_have) memcpy(g_newest + HEAD, G2H(g_dec), FRAME_SIZE);
    *fresh = g_fresh;
    g_fresh = 0;
    return g_have ? g_newest : NULL;
}

/* Start host mode (net_join.c --net-mode ha): the room's creator hosts. */
int rt_net_begin_ha(int slot, int creator) {
    N.slot = slot;
    rt_net = 1;   /* pads.c: replay pad events are this player's (N.local) */
    rt_net_ha = creator ? 1 : 2;
    if (rt_npads < RT_MAX_PADS) rt_npads = creator ? RT_MAX_PADS : 1;
    rt_trace("netplay: host-authoritative session as player %d (%s)", slot + 1, creator ? "host" : "friend");
    return 1;
}
