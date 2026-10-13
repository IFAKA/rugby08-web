/* net_ha_int.h -- between net_ha.c (the session, its messages) and net_ha_rx.c (a friend's frames). */
#pragma once
#include <stdint.h>
#include "match_state.h"

#define HEAD       MS_HEAD_BYTES       /* the head every frame carries (match_replay.c) */
#define FRAME_SIZE 0x118Cu  /* a raw replay frame (a key) */
#define PRE        5        /* 'F' payload before the head: u8 kind (0 key, 1 record), u32 sequence number */

/* A received 'F' payload at Present now; 1: ask the host for a key (a frame was lost, or none yet). */
int net_ha_rx_frame(uint32_t frame_no, const uint8_t *p, uint16_t len, uint32_t now);
void net_ha_rx_end(int has_seq, uint32_t last_seq);   /* 'E' */
void net_ha_rx_reset(void);
int net_ha_rx_phase(void);   /* the host's phase id in the newest chained frame (-1: none) */
