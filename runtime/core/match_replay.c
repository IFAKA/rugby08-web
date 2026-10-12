/* match_replay.c -- the game's own record of the match (PlayBack::CReplayManager): every match frame
 * is appended to a stream of compressed records, which the game decodes to redraw the match in
 * instant replays (docs/MATCH_STATE.md "Replay system"). Online play sends these records from the
 * host to its friends (on.match_puppet).
 *   RT_MS_REPLAY_CHECK=N: every N frames of a match, compare the recorder's current frame with the live
 *   players and ball ("ms: replay check" lines). */
#include "rt.h"
#include "match_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint32_t rt_heap_top(void); /* heap.c */

#define BACKEND_PTR 0x009B65ECu /* BackendState* (the game's top object) */
#define B_COMMANDER 0x233Cu     /* BackendState: PlayBack::PlayBackCommander* */
#define C_MANAGER   0x4u        /* PlayBackCommander: CReplayManager* (FUN_00456200) */
#define M_STREAM    0x14u       /* CReplayManager: first record */
#define M_END       0x1Cu       /* end of the written records */
#define M_HUFFMAN   0x3Cu       /* the decoder's tree (ecx of FUN_00458620) */
#define FN_DECODE   0x00458620u /* thiscall (frame, record, add): frame += record's deltas */
#define FRAME_SIZE  0x118Cu
#define F_POS       0xF00u      /* 30 x {x, y, z, ?}: positions in 1/52.5 m */

static int heap_obj(uint32_t va, uint32_t size) { return va >= RT_HEAP_LO && va + size <= rt_heap_top(); }
static float rdf(uint32_t va) {
    uint32_t u = rd32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t ms_replay_manager(void) {
    uint32_t b = rd32(BACKEND_PTR), c = heap_obj(b, B_COMMANDER + 4) ? rd32(b + B_COMMANDER) : 0;
    uint32_t m = heap_obj(c, 8) ? rd32(c + C_MANAGER) : 0;
    return heap_obj(m, 0x1318) ? m : 0;
}

int ms_replay_stream(uint32_t *start, uint32_t *end) {
    uint32_t m = ms_replay_manager();
    if (!m) return 0;
    *start = rd32(m + M_STREAM);
    *end = rd32(m + M_END);
    return heap_obj(*start, 1) && *end >= *start && heap_obj(*end, 0);
}

/* The record at rec applied to frame (both guest addresses), by the game's own decoder. */
void ms_replay_decode(uint32_t frame, uint32_t rec) {
    uint32_t m = ms_replay_manager(), sv = g_ecx;
    if (!m) return;
    uint32_t args[3] = { frame, rec, 1 };
    g_ecx = m + M_HUFFMAN;
    guest_callv(FN_DECODE, 3, args);
    g_ecx = sv;
}

/* Frame B (+0x2C) is the frame the recorder wrote last, i.e. the current one: every player's position
   (1/52.5 m units) and the ball (metres) must equal the live ones. */
#define M_FRAME_B 0x2Cu
#define POS_UNITS 52.5f
uint32_t ms_replay_current(void) {
    uint32_t m = ms_replay_manager(), f = m ? rd32(m + M_FRAME_B) : 0;
    return heap_obj(f, FRAME_SIZE) ? f : 0;
}

static void check(uint32_t frame_no) {
    uint32_t f = ms_replay_current();
    if (!f) return;
    float worst = 0;
    int compared = 0;
    for (int t = 0; t < MS_TEAMS; t++)
        for (int i = 0; i < MS_PLAYERS; i++) {
            ms_player_t p;
            if (!ms_player(t, i, &p) || p.parked) continue;
            uint32_t e = f + F_POS + 16u * (uint32_t)(t * MS_PLAYERS + i);
            float dx = rdf(e) / POS_UNITS - p.pos[0], dz = rdf(e + 8) / POS_UNITS - p.pos[2];
            float d = 100 * sqrtf(dx * dx + dz * dz);
            if (d > worst) worst = d;
            compared++;
        }
    ms_ball_t b;
    float bd = -1;
    if (ms_ball(&b)) {
        float dx = rdf(f + 0x111C) - b.pos[0], dy = rdf(f + 0x1120) - b.pos[1], dz = rdf(f + 0x1124) - b.pos[2];
        bd = 100 * sqrtf(dx * dx + dy * dy + dz * dz);
    }
    rt_trace("ms: replay check f %u frame t %.3f players %d worst %.2f cm ball %.2f cm", frame_no,
             (double)rdf(f + 0x116C), compared, (double)worst, (double)bd);
}

void ms_replay_frame(uint32_t frame) {
    static int every = -1;
    if (every < 0) {
        const char *e = getenv("RT_MS_REPLAY_CHECK");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && frame % (uint32_t)every == 0 && ms_active()) check(frame);
}
