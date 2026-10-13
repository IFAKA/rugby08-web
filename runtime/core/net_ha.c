/* net_ha.c -- online play, host-authoritative (on.match_net; plan docs/plans/2026-10-12-online-host-
 * authoritative.md). Only the host plays the match: friends' pads are its controllers 2..4, and each
 * match frame it records goes to the friends, whose games show it (match_puppet.c) instead of playing
 * their own. Rooms and the relay are netplay's (server/, net_join.c: --net-mode ha).
 *
 * Wire (little-endian, after netplay's types; the server relays these and does not log them):
 *   {u8 type, u8 slot, u32 frame, u16 len, len bytes}
 *   'S' setup   host -> friends at the match launch: MS_SETUP_BYTES (match_setup.c); the server keeps it
 *   'F' frame   host -> friends, one per recorded frame: u8 kind, u32 sequence number, the 96-byte head
 *               (match_replay.c: score, view), then kind 0 (key) the raw 0x118C-byte frame, kind 1 the
 *               game's own record of the frame (its replay coder, ~380 bytes); the server keeps the last
 *               key for newcomers. Frames may come late, out of order or not at all (WebRTC's unreliable
 *               channel, js/rtc.js): a record that does not follow the last one ends the chain until a
 *               key, which the friend asks for ('K')
 *   'K' key     friend -> host: send a key next
 *   'P' pad     friend -> host every Present: rt_pad_t
 *   'E' end     host -> friends: its match is over (u32: its last frame's sequence number: later ones
 *               that arrive are the old match's)        'Z' pause   friend -> host: it paused
 *   'U' paused  host -> friends: u8 paused or not        (the party flow: net_ha_flow.c)
 * A key goes first, on request and every KEY_EVERY frames; a friend decodes the records after the last
 * key in order with its own game's decoder (the same tables in every game: RT_MS_STREAM_CHECK). */
#include "netplay_int.h"
#include "match_state.h"
#include "../debug/debug.h"
#include "ui.h"
#include "net_ha_int.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HDR       8
#define KEY_EVERY 600
#define PAD_STALE 60   /* frames without a friend's pad: its controller lets go */
#define KEY_ASK   30   /* Presents between a friend's key requests */

int rt_net_ha;   /* 1 host, 2 friend */
static uint8_t g_rx[1 << 16];
static int g_rx_n;
static int g_want_key = 1, g_since_key;
static rt_pad_t g_pad[RT_MAX_PADS];
static uint32_t g_pad_at[RT_MAX_PADS];
static uint32_t g_frame;

static uint32_t g_seq_out;                  /* host: the next frame's sequence number */
static uint32_t g_key_asked;                /* friend: Present of its last key request */

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

void net_ha_send(char type, uint32_t frame, const void *payload, size_t len) {
    send(type, frame, payload, len, NULL, 0);
}

/* host: its match is over (net_ha_flow.c); its last frame's sequence number goes with it */
void net_ha_send_end(uint32_t frame) {
    uint8_t last[4];
    put32(last, g_seq_out - 1);
    send('E', frame, last, sizeof last, NULL, 0);
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
    uint8_t kind_head[PRE + HEAD];
    int key = g_want_key || !rec_len || ++g_since_key >= KEY_EVERY;
    kind_head[0] = key ? 0 : 1;
    put32(kind_head + 1, g_seq_out++);
    memcpy(kind_head + PRE, head, HEAD);
    if (key) {
        g_want_key = g_since_key = 0;
        send('F', frame_no, kind_head, sizeof kind_head, G2H(frame), FRAME_SIZE);
    } else send('F', frame_no, kind_head, sizeof kind_head, G2H(rec), rec_len);
    rt_trace("ms: net tx %u %s ns %llu", frame_no, key ? "key" : "rec", (unsigned long long)now_ns());
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
        net_ha_rx_end(len == 4, len == 4 ? get32(m + HDR) : 0);
        net_ha_flow_end();
        break;
    case 'F':
        if (rt_net_ha == 2 && net_ha_rx_frame(get32(m + 2), m + HDR, len, g_frame) &&
            (!g_key_asked || g_frame - g_key_asked >= KEY_ASK))
            g_want_key = 1, g_key_asked = g_frame;
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
        if (g_rx[g_rx_n] == '{') ui_lobby_text((const char *)g_rx + g_rx_n, n);   /* lobby JSON: members */
        else g_rx_n += n;
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
    if (rt_net_ha == 2) plat_local_pad(&N.local);   /* devices; in replay runs the replay's pad events */
    pump();
    net_ha_flow_present(frame);
    if (rt_net_ha == 1) {   /* the host's own pad 0 is its game controller (pads.c); friends' pads in */
        static const rt_pad_t zero;
        rt_pads_party = ui_lobby_member_mask() & ~(1u << N.slot);
        for (int k = 0; k < RT_MAX_PADS; k++)
            if (k != N.slot) rt_pads[k] = g_pad_at[k] && frame - g_pad_at[k] < PAD_STALE ? g_pad[k] : zero;
        return;
    }
    rt_pads[N.slot] = N.local;
    net_ha_test_press(&N.local, net_ha_rx_phase(), frame);
    rt_pads[N.slot] = N.local;
    send('P', frame, &N.local, sizeof N.local, NULL, 0);
    if (g_want_key) {
        send('K', frame, NULL, 0, NULL, 0);
        g_want_key = 0;
    }
}

/* Back to playing alone: friends' pads let go, no frames kept. */
void rt_net_end_ha(void) {
    static const rt_pad_t zero;
    rt_pads_party = 0;
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (k != N.slot) rt_pads[k] = zero;
    memset(g_pad_at, 0, sizeof g_pad_at);
    net_ha_rx_reset();
    g_rx_n = 0;
    rt_net_ha = 0;
    rt_net = 0;
    rt_trace("netplay: host-authoritative session ended");
}

/* Start host mode (net_join.c --net-mode ha): the room's creator hosts. */
int rt_net_begin_ha(int slot, int creator) {
    N.slot = slot;
    rt_net = 1;   /* pads.c: replay pad events are this player's (N.local) */
    rt_net_ha = creator ? 1 : 2;
    if (rt_npads < (creator ? RT_MAX_PADS : 1)) rt_npads = creator ? RT_MAX_PADS : 1;   /* (from boot by default) */
    rt_trace("netplay: host-authoritative session as player %d (%s)", slot + 1, creator ? "host" : "friend");
    return 1;
}
