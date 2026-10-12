/* match_setup.c -- the match a game is about to play, as bytes another game can load (on.match_setup).
 * The whole definition of a match lives in the setup block (docs/MATCH_STATE.md): both rosters, kits,
 * conditions, wind, half length, stadium, difficulty. Each menu route ends in the same launch step,
 * FUN_004F3D10 (Stadium Select's commit FUN_004FCDD0 and the other modes' commits call it after
 * writing the block): its entry is hooked (tools/hooks.json "ms:launch"). There the host's setup is
 * recorded and a friend's own is replaced by the host's, before anything is loaded from it.
 *   RT_MS_SETUP_OUT=FILE  write this game's setup at launch
 *   RT_MS_SETUP_IN=FILE   play FILE's setup instead of this game's own (test stand-in for the network)
 *   RT_MS_AFTER_LAUNCH=K:PNG  K frames after the launch: a screenshot and an "ms: after launch" line */
#include "rt.h"
#include "match_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bytes of the setup block that define the match: everything before the score list, and difficulty.
   (The clock at +0x18F0 is 0 at launch; the score list head at +0x1908 belongs to the running match.) */
static const struct {
    uint32_t off, len;
} k_span[] = { { 0x0000u, 0x1908u }, { 0x190Cu, 0x4u } };
#define NSPAN (sizeof k_span / sizeof *k_span)

static uint8_t g_offer[MS_SETUP_BYTES];
static int g_have_offer;
static long g_launch_frame = -1;

int ms_setup_capture(uint8_t *out) {
    uint32_t base = ms_setup_va(), n = 0;
    for (size_t k = 0; k < NSPAN; k++) {
        memcpy(out + n, G2H(base + k_span[k].off), k_span[k].len);
        n += k_span[k].len;
    }
    return (int)n;
}

void ms_setup_offer(const uint8_t *in) {
    memcpy(g_offer, in, MS_SETUP_BYTES);
    g_have_offer = 1;
}

static void apply(const uint8_t *in) {
    uint32_t base = ms_setup_va(), n = 0;
    for (size_t k = 0; k < NSPAN; k++) {
        memcpy(G2H(base + k_span[k].off), in + n, k_span[k].len);
        n += k_span[k].len;
    }
}

uint64_t ms_setup_hash(void) {
    uint8_t b[MS_SETUP_BYTES];
    ms_setup_capture(b);
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < MS_SETUP_BYTES; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

static void file_offer(void) {   /* RT_MS_SETUP_IN */
    static int done;
    const char *path = getenv("RT_MS_SETUP_IN");
    if (done++ || !path) return;
    uint8_t b[MS_SETUP_BYTES];
    FILE *f = fopen(path, "rb");
    if (!f || fread(b, 1, sizeof b, f) != sizeof b) rt_fatal("ms: cannot read a setup from %s", path);
    fclose(f);
    ms_setup_offer(b);
}

/* tools/hooks.json "ms:launch": the game is about to load the match its setup block describes. */
void ms_launch(void) {
    file_offer();
    uint64_t own = ms_setup_hash();
    if (g_have_offer) {
        apply(g_offer);
        g_have_offer = 0;
    }
    uint64_t h = ms_setup_hash();
    g_launch_frame = (long)rt_present_frame;
    rt_trace("ms: launch setup %016llX (own %016llX) teams %u v %u stadium %u frame %ld", (unsigned long long)h,
             (unsigned long long)own, ms_team_id(0), ms_team_id(1), rd16(ms_setup_va() + 0x1902), g_launch_frame);
    const char *out = getenv("RT_MS_SETUP_OUT");
    if (out) {
        uint8_t b[MS_SETUP_BYTES];
        ms_setup_capture(b);
        FILE *f = fopen(out, "wb");
        if (!f || fwrite(b, 1, sizeof b, f) != sizeof b) rt_log("ms: cannot write the setup to %s", out);
        if (f) fclose(f);
    }
}

/* RT_MS_AFTER_LAUNCH=K:PNG (checks): the same moment of the match in two games whose menus took
   different times, K frames after each one's launch. Called from ms_frame at every Present. */
void ms_setup_frame(uint32_t frame) {
    static long k = -2;
    static char png[512];
    if (k == -2) {
        const char *e = getenv("RT_MS_AFTER_LAUNCH");
        k = e ? atol(e) : -1;
        if (e && strchr(e, ':')) snprintf(png, sizeof png, "%s", strchr(e, ':') + 1);
    }
    if (k < 0 || g_launch_frame < 0 || (long)frame != g_launch_frame + k) return;
    if (png[0]) snprintf(rt_shot_path, sizeof rt_shot_path, "%s", png);
    rt_trace("ms: after launch %ld setup %016llX teams %u v %u stadium %u phase %s", k,
             (unsigned long long)ms_setup_hash(), ms_team_id(0), ms_team_id(1), rd16(ms_setup_va() + 0x1902),
             ms_phase());
}
