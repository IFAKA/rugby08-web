/* match_replay.c -- the game's own record of the match (PlayBack::CReplayManager): every match frame
 * is appended to a stream of compressed records, which the game decodes to redraw the match in
 * instant replays (docs/MATCH_STATE.md "Replay system"). Online play sends these records from the
 * host to its friends (on.match_puppet).
 *   RT_MS_REPLAY_CHECK=N: every N frames of a match, compare the recorder's current frame with the live
 *   players and ball ("ms: replay check" lines).
 *   RT_MS_REC_OUT=FILE: the host's match as a file of records {u32 frame, f32 clock, u32 phase id,
 *   u16 score home, u16 score away, u16 score events, then the latest one: u16 scorer, u32 type, u32 team,
 *   f32 clock; the view (MS_CAMERA_BYTES, match_camera.c); frame B (0x118C bytes)} (96 + 0x118C bytes),
 *   one per Present at which the recorder wrote a frame. */
#include <stdio.h>
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

/* RT_MS_REC_OUT: one record per Present at which the recorder has written a new frame, i.e. its record
   stream grew (frame B holds uninitialised memory until the first write: time 2e-44, headings 1e33). */
static void record(uint32_t frame_no) {
    static FILE *out;
    static int tried;
    static uint32_t last_end;
    if (!tried) {
        tried = 1;
        const char *p = getenv("RT_MS_REC_OUT");
        if (p && !(out = fopen(p, "wb"))) rt_log("ms: cannot write %s", p);
    }
    uint32_t f = out && ms_active() ? ms_replay_current() : 0, start, end;
    if (!f || !ms_replay_stream(&start, &end) || end == start || end == last_end) return;
    last_end = end;
    ms_score_ev_t ev[64];
    int nev = ms_score_events(ev, 64);
    ms_score_ev_t last = nev ? ev[nev - 1] : (ms_score_ev_t){ 0, 0, 0, 0 };
    struct {
        uint32_t frame;
        float clock;
        uint32_t phase;
        uint16_t home, away, nscore, scorer;   /* the score events so far, and the latest one: */
        uint32_t type, team;
        float when;
    } h = { frame_no,
            ms_clock(),
            (uint32_t)ms_phase_id(),
            (uint16_t)ms_score(0),
            (uint16_t)ms_score(1),
            (uint16_t)nev,
            last.player_id,
            last.type,
            last.team_id,
            last.clock };
    uint8_t cam[MS_CAMERA_BYTES];
    ms_camera_last(cam);
    fwrite(&h, sizeof h, 1, out);
    fwrite(cam, 1, sizeof cam, out);
    fwrite(G2H(f), 1, FRAME_SIZE, out);
    fflush(out);
}

void ms_replay_frame(uint32_t frame) {
    static int every = -1;
    if (every < 0) {
        const char *e = getenv("RT_MS_REPLAY_CHECK");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && frame % (uint32_t)every == 0 && ms_active()) check(frame);
    record(frame);
}
