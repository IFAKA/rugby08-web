/* match_stream_check.c -- can a friend decode the host's own replay records (on.match_net wire format)?
 * RT_MS_STREAM_CHECK=STREAM:REC (files from a host's RT_MS_STREAM_OUT and RT_MS_REC_OUT): once this game
 * has a match, start from the host's first raw frame, apply each host record with this game's decoder
 * (FUN_00458620, ms_replay_decode) and compare with the host's raw frame of the same record:
 * "ms: stream check" line (worst position, heading and ball error, frames with identical pose blocks). */
#include "debug.h"
#include "match_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SIZE 0x118Cu
#define REC_SIZE   (96u + FRAME_SIZE)

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n ? *n : 1);
    if (b && fread(b, 1, *n, f) != *n) b = (free(b), NULL);
    fclose(f);
    return b;
}

static float fat(const uint8_t *p, uint32_t off) {
    float v;
    memcpy(&v, p + off, 4);
    return v;
}

typedef struct {
    float pos, heading, ball;
    int same_blocks, frames, pos_over_1cm, heading_over_001;
    float pos_at, heading_at, heading_got, heading_want;
} err_t;

static void compare(const uint8_t *got, const uint8_t *want, err_t *e) {
    float fp = 0, fh = 0;
    for (int k = 0; k < 30; k++) {
        const uint8_t *a = got + 0xF00 + 16 * k, *b = want + 0xF00 + 16 * k;
        float dx = fat(a, 0) - fat(b, 0), dz = fat(a, 8) - fat(b, 8), d = 100 * sqrtf(dx * dx + dz * dz) / 52.5f;
        float h = fabsf(fat(a, 12) - fat(b, 12));
        fp = fmaxf(fp, d);
        if (h > fh) {
            fh = h;
            if (h > e->heading) {
                e->heading_got = fat(a, 12);
                e->heading_want = fat(b, 12);
            }
        }
    }
    if (fp > e->pos) e->pos_at = fat(want, 0x116C);
    if (fh > e->heading) e->heading_at = fat(want, 0x116C);
    e->pos = fmaxf(e->pos, fp);
    e->heading = fmaxf(e->heading, fh);
    e->pos_over_1cm += fp > 1;
    e->heading_over_001 += fh > 0.01f;
    float bx = fat(got, 0x111C) - fat(want, 0x111C), bz = fat(got, 0x1124) - fat(want, 0x1124);
    e->ball = fmaxf(e->ball, 100 * sqrtf(bx * bx + bz * bz));
    e->same_blocks += !memcmp(got, want, 0xF00);
    e->frames++;
}

static void run(const char *spec) {
    char sp[512];
    snprintf(sp, sizeof sp, "%s", spec);
    char *colon = strchr(sp, ':');
    if (!colon) return;
    *colon = 0;
    size_t ns, nr;
    uint8_t *st = slurp(sp, &ns), *rec = slurp(colon + 1, &nr);
    uint32_t d = gcalloc(FRAME_SIZE), r = gcalloc(0x10000);
    if (!st || !rec || !d || !r) {
        rt_log("ms: stream check: cannot read %s", spec);
        return;
    }
    size_t nrec = nr / REC_SIZE, off = 0, keys = 0;
    err_t e;
    memset(&e, 0, sizeof e);
    for (size_t i = 0; i < nrec && off + 6 <= ns; i++) {
        uint16_t len;
        memcpy(&len, st + off + 4, 2);
        const uint8_t *want = rec + i * REC_SIZE + 96;
        if (i == 0 || !len) {   /* a key: the raw frame */
            memcpy(G2H(d), want, FRAME_SIZE);
            keys++;
        } else {
            memcpy(G2H(r), st + off + 6, len);
            ms_replay_decode(d, r);
            compare(G2H(d), want, &e);
        }
        off += 6u + len;
    }
    rt_trace("ms: stream check %d frames decoded (%zu keys): worst position %.2f cm (t %.2f; %d frames over 1 cm), "
             "heading %.4f (t %.2f: %.4f for %.4f; %d frames over 0.01), ball %.2f cm, pose blocks identical in %d",
             e.frames, keys, (double)e.pos, (double)e.pos_at, e.pos_over_1cm, (double)e.heading, (double)e.heading_at,
             (double)e.heading_got, (double)e.heading_want, e.heading_over_001, (double)e.ball, e.same_blocks);
    free(st);
    free(rec);
}

void ms_stream_check_frame(void) {
    static int done;
    const char *spec = getenv("RT_MS_STREAM_CHECK");
    if (done || !spec || !ms_active() || !ms_replay_manager()) return;
    done = 1;
    run(spec);
}
