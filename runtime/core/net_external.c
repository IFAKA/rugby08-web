/* net_external.c -- lockstep: data a game reads from its own machine rather than from its inputs. The
 * one case: the game draws each stadium's shadow map into a render target and reads the pixels back
 * (d3d8_levels.cpp readback_level), and GPUs differ, and a game catching up draws nothing at all, so
 * every game must read the same pixels. One player (the lowest slot playing at that frame: the same
 * choice in every game) reads its GPU and sends the bytes ('X': slot, frame, the read's number in the
 * session, length, data); the others wait for them. The server logs them with the inputs, so a game
 * catching up later gets them too. Protocol: netplay.c. */
#include "netplay_int.h"
#include <stdlib.h>
#include <string.h>

#define KEEP 8
static struct {
    uint32_t seq, len;
    uint8_t *data;
} g_got[KEEP];
static uint32_t g_seq;   /* reads so far in this session (every game counts the same) */

void net_external_rx(const uint8_t *m) {
    uint32_t seq = get32(m + 6), len = get32(m + 10);
    int i = (int)(seq % KEEP);
    free(g_got[i].data);
    g_got[i].data = (uint8_t *)malloc(len ? len : 1);
    if (!g_got[i].data) return;
    memcpy(g_got[i].data, m + 14, len);
    g_got[i].seq = seq + 1;   /* (0: empty) */
    g_got[i].len = len;
}

static int source(void) {
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (N.active[k]) return k;
    return N.slot;
}

/* 1: this game reads its machine for read number g_seq and calls rt_net_external_put; 0: it calls
   rt_net_external_get, which waits for the bytes. Outside lockstep: 1 (the game reads its machine). */
int rt_net_external_source(void) { return !rt_net || rt_net_ha || N.offline || (!rt_ff && source() == N.slot); }

void rt_net_external_put(const void *data, uint32_t len) {
    if (!rt_net || rt_net_ha || N.offline) return;
    uint8_t *m = (uint8_t *)malloc(14 + (size_t)len);
    if (!m) return;
    m[0] = 'X';
    m[1] = (uint8_t)N.slot;
    put32(m + 2, N.frame);
    put32(m + 6, g_seq);
    put32(m + 10, len);
    memcpy(m + 14, data, len);
    net_tx_send(m, 14 + (int)len, 0);
    free(m);
    rt_trace("netplay: external read %u (%u bytes) sent at frame %u", g_seq, len, N.frame);
    g_seq++;
}

void rt_net_external_get(void *data, uint32_t len) {
    int i = (int)(g_seq % KEEP);
    for (uint32_t waited = 0; g_got[i].seq != g_seq + 1 && !N.offline; waited++) {
        net_pump(waited ? 5 : 0);
        if (waited % 2000 == 1999) rt_trace("netplay: waiting for external read %u", g_seq);
    }
    if (g_got[i].seq == g_seq + 1) {
        memcpy(data, g_got[i].data, len < g_got[i].len ? len : g_got[i].len);
        rt_trace("netplay: external read %u (%u bytes) from the stream at frame %u", g_seq, g_got[i].len, N.frame);
    }
    g_seq++;
}
