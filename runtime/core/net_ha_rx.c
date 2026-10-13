/* net_ha_rx.c -- a friend's frames from the host (net_ha.c 'F'), put back in order and decoded.
 * Frames travel over an unordered channel (WebRTC, platform/web/js/rtc.js; reliable, but a frame can
 * still go missing when the path changes between the relay and the channel): a key
 * (the raw frame) starts a chain, each record (the game's replay coder: a delta) needs the one before.
 * Frames that come early wait in a window until the missing one arrives (a 4.7 KB key, sent in pieces,
 * is often passed by the small records after it); one missing while LOSS_AFTER later ones wait is lost:
 * the chain ends and a key is asked for (the caller, at most every KEY_ASK Presents). */
#include "netplay_int.h"
#include "match_state.h"
#include "net_ha_int.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WINDOW     48   /* frames held (~0.8 s at 60 fps) */
#define LOSS_AFTER 30   /* later frames waiting (~0.5 s): the missing one is not coming */

typedef struct {
    uint32_t seq, frame_no;
    uint16_t len;
    uint8_t *p;
} held_t;
static held_t g_hold[WINDOW];
static int g_nhold;

static uint8_t *g_pk;   /* the chain since its key: {u16 len, payload} */
static size_t g_pk_n, g_pk_cap;
static uint32_t g_dec;                      /* guest frame the records are decoded into */
static size_t g_dec_at;                     /* bytes of g_pk already decoded */
static uint8_t g_newest[HEAD + FRAME_SIZE]; /* head and frame of the newest decoded */
static int g_have, g_fresh, g_chain;
static int g_host_phase = -1;               /* the phase id in the newest head chained */
static uint32_t g_rx_at;                    /* Present at which the last frame was chained */
static uint32_t g_next, g_end_seq;          /* the record the chain needs; the ended match's last frame */
static int g_ended;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void drop(int i) {
    free(g_hold[i].p);
    g_hold[i] = g_hold[--g_nhold];
}

static void append(const held_t *h, uint32_t now) {
    if (g_pk_n + 2u + h->len > g_pk_cap) {
        size_t cap = (g_pk_n + 2u + h->len) * 2;
        uint8_t *grown = realloc(g_pk, cap);
        if (!grown) return;
        g_pk = grown;
        g_pk_cap = cap;
    }
    g_pk[g_pk_n] = (uint8_t)h->len;
    g_pk[g_pk_n + 1] = (uint8_t)(h->len >> 8);
    memcpy(g_pk + g_pk_n + 2, h->p, h->len);
    g_pk_n += 2u + h->len;
    g_next = h->seq + 1;
    g_chain = 1;
    memcpy(&g_host_phase, h->p + PRE + 8, 4);
    g_rx_at = now;
    rt_trace("ms: net rx %u %s ns %llu", h->frame_no, h->p[0] ? "rec" : "key", (unsigned long long)now_ns());
}

static int newest_key(void) {
    int k = -1;
    for (int i = 0; i < g_nhold; i++)
        if (g_hold[i].p[0] == 0 && (k < 0 || (int32_t)(g_hold[i].seq - g_hold[k].seq) > 0)) k = i;
    return k;
}

/* Chain what can be chained; 1: a frame was lost (ask for a key). */
static int drain(uint32_t now) {
    for (;;) {
        int k = newest_key();
        if (k >= 0 && (!g_chain || (int32_t)(g_hold[k].seq - g_next) >= 0)) {   /* a key: start from it */
            held_t h = g_hold[k];
            g_pk_n = g_dec_at = 0;
            append(&h, now);
            for (int i = g_nhold - 1; i >= 0; i--)
                if ((int32_t)(g_hold[i].seq - g_next) < 0) drop(i);
            continue;
        }
        int i = 0;
        while (i < g_nhold && !(g_chain && g_hold[i].seq == g_next)) i++;
        if (i < g_nhold) {
            append(&g_hold[i], now);
            drop(i);
            continue;
        }
        if (g_nhold < (g_chain ? LOSS_AFTER : WINDOW)) return 0;
        if (g_chain) rt_trace("ms: net rx frame %u lost: asking for a key", g_next);
        g_chain = 0;   /* nothing waiting can be decoded: keep only keys */
        for (int j = g_nhold - 1; j >= 0; j--)
            if (g_hold[j].p[0] != 0) drop(j);
        return 1;
    }
}

int net_ha_rx_frame(uint32_t frame_no, const uint8_t *p, uint16_t len, uint32_t now) {
    if (len < PRE + HEAD) return 0;
    uint32_t seq = get32(p + 1);
    if (g_ended && (int32_t)(seq - g_end_seq) <= 0) return 0;   /* the ended match's, late */
    if (g_chain && (int32_t)(seq - g_next) < 0) return 0;      /* already past it */
    for (int i = 0; i < g_nhold; i++)
        if (g_hold[i].seq == seq) return 0;
    if (g_nhold == WINDOW) drop(0);
    uint8_t *copy = malloc(len);
    if (!copy) return 0;
    memcpy(copy, p, len);
    g_hold[g_nhold++] = (held_t){ seq, frame_no, len, copy };
    return drain(now) || (!g_chain && !g_have);
}

void net_ha_rx_end(int has_seq, uint32_t last_seq) {   /* 'E': the host's frames are history */
    if (has_seq) g_end_seq = last_seq, g_ended = 1;
    net_ha_rx_reset();
}

void net_ha_rx_reset(void) {
    while (g_nhold) drop(g_nhold - 1);
    g_pk_n = g_dec_at = 0;
    g_have = g_chain = 0;
    g_rx_at = 0;
}

int net_ha_rx_recent(uint32_t frame, uint32_t within) { return g_rx_at && frame - g_rx_at <= within; }
int net_ha_rx_phase(void) { return g_host_phase; }

/* friend (match_puppet.c): the newest host frame, all chained records decoded in order. Returns its
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
        if (p[0] == 0) memcpy(G2H(g_dec), p + PRE + HEAD, FRAME_SIZE);
        else {
            memcpy(G2H(rec), p + PRE + HEAD, len - (uint32_t)PRE - HEAD);
            ms_replay_decode(g_dec, rec);
        }
        memcpy(g_newest, p + PRE, HEAD);
        g_dec_at += 2u + len;
        g_have = g_fresh = 1;
    }
    if (g_have) memcpy(g_newest + HEAD, G2H(g_dec), FRAME_SIZE);
    *fresh = g_fresh;
    g_fresh = 0;
    return g_have ? g_newest : NULL;
}
