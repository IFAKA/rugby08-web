/* netplay_int.h -- state shared by netplay.c (messages, roster, session) and net_frame.c (the
 * per-frame lockstep hook). Wire format and protocol: netplay.c. */
#ifndef NETPLAY_INT_H
#define NETPLAY_INT_H
#include "plat.h"
#include "rt.h"
#include <stdint.h>

#define DESK_SLOT      0    /* the game's keyboard and mouse belong to controller 1's player */
#define RING           1024
#define AHEAD          600  /* read no message for a frame this far ahead (ring room) */
#define NDESK          24   /* keyboard and mouse events per input message (more wait a frame) */
#define NET_HASH_EVERY 120
#define NET_HASH_CYCLE 600  /* frames to hash the whole state once (det_hash_step) */
#define HRING          16

typedef struct {
    uint32_t frame;
    uint8_t have, nd;
    rt_pad_t pad;
    plat_desk_ev_t d[NDESK];
} slot_in_t;
typedef struct {
    uint32_t frame;
    uint8_t on;
} ev_t;   /* roster changes per slot, in frame order */

typedef struct {
    int slot, auth, delay, offline;
    rt_pad_t local;                   /* this player's pad (devices, or replay "pad 0" events) */
    slot_in_t in[RING][RT_MAX_PADS];
    uint64_t hash[HRING][RT_MAX_PADS];
    uint32_t hash_frame[HRING][RT_MAX_PADS];
    ev_t ev[RT_MAX_PADS][32];
    int nev[RT_MAX_PADS];
    uint8_t active[RT_MAX_PADS];      /* roster at the frame being resolved */
    int64_t my_from;                  /* my slot plays from this frame (-1: not granted yet) */
    int joined;                       /* caught up once (the room's creator: from the start): in step since */
    int64_t next_send;                /* first frame I have not sent an input for */
    uint32_t max_seen, frame;         /* highest input frame received; frame being resolved */
    int64_t front;                    /* the server's hint: the others' inputs reach this frame */
    uint32_t by_peer, by_relay, stalls; /* inputs that came first peer to peer / by the relay; frames waited */
    int64_t idle_lo, idle_hi;         /* catching up: idle inputs sent for these frames (beyond the ring) */
    rt_pad_t idle_pad;
    uint8_t want[RT_MAX_PADS];        /* join requests to grant (authority) */
    uint8_t rx[1 << 21];              /* received binary bytes not handled yet (an 'X' may be ~1 MB) */
    int rx_n;
} net_state_t;
extern net_state_t N;

static inline void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline uint32_t get32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* netplay.c */
void net_store_input(int slot, uint32_t frame, const rt_pad_t *pad, int nd, const plat_desk_ev_t *d);
int net_have_input(int slot, uint32_t frame);
void net_roster_at(uint32_t frame);
void net_send_grant(int slot, uint32_t frame);
void net_handle(const uint8_t *m);
void net_pump(int wait_ms);
/* net_external.c: data a game reads from its machine, shared through the stream ('X') */
void net_external_rx(const uint8_t *m);
/* net_frame.c */
void net_frame(uint32_t frame);   /* det_frame hook: runs first at every Present */

#endif
