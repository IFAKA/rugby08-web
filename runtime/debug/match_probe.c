/* match_probe.c -- diagnosis probes used to map the live match in guest memory (docs/MATCH_STATE.md):
 *   RT_MS_SCAN=F[,F...]: at those frames, every heap object whose first dword is one of the known
 *   class vtables (RTTI), and every image dword that points at one of them ("ms: obj", "ms: ref"). */
#include "debug.h"
#include <stdlib.h>
#include <string.h>

uint32_t rt_heap_top(void);   /* heap.c */

typedef struct {
    const char *name;
    uint32_t vt;
} ms_class_t;
static const ms_class_t k_classes[] = {
    { "CRugby", 0x008E8E80 },
    { "CRugbyTeam", 0x008E6734 },
    { "CRugbyPlayer", 0x008E5E00 },
    { "BallManager", 0x008E6B74 },
    { "CScoreData", 0x008E4238 },
    { "CReferee", 0x008CE21C },
    { "CPlayerControl", 0x008EE9D8 },
    { "ActivePlayer", 0x008F14C4 },
    { "ActiveTeam", 0x008F1364 },
    { "CTeamData", 0x008CE400 },
    { "CPlayerData", 0x008CE0B8 },
    { "CDirector", 0x008D66D4 },
    { "COverlayItemClock", 0x008E18B8 },
    { "COverlayItemScoreBox", 0x008E1608 },
};
#define NCLASS (int)(sizeof k_classes / sizeof k_classes[0])

static void scan(uint32_t frame) {
    enum { MAXO = 512 };
    static uint32_t objs[MAXO];
    static int cls[MAXO];
    int n = 0;
    uint32_t top = rt_heap_top();
    for (uint32_t a = RT_HEAP_LO; a + 4 <= top; a += 4) {
        uint32_t v = rd32(a);
        if (v < 0x008C0000u || v > 0x00900000u) continue;
        for (int c = 0; c < NCLASS; c++)
            if (v == k_classes[c].vt && n < MAXO) {
                objs[n] = a;
                cls[n] = c;
                n++;
            }
    }
    int count[NCLASS] = { 0 };
    for (int i = 0; i < n; i++) {
        count[cls[i]]++;
        rt_log("ms: obj frame %u %s %08X", frame, k_classes[cls[i]].name, objs[i]);
    }
    for (int c = 0; c < NCLASS; c++) rt_trace("ms: frame %u class %s count %d", frame, k_classes[c].name, count[c]);
    /* globals (image) pointing at an object */
    for (uint32_t a = RT_IMAGE_BASE; a + 4 <= RT_IMAGE_END; a += 4) {
        uint32_t v = rd32(a);
        if (v < RT_HEAP_LO || v >= top) continue;
        for (int i = 0; i < n; i++)
            if (v == objs[i]) rt_log("ms: ref frame %u %08X -> %s %08X", frame, a, k_classes[cls[i]].name, v);
    }
}

/* RT_MS_DUMP=A-B/S: frames A..B every S, raw bytes of CRugby (0x100), its two teams (0x1A4), their
 * players (0x240 each) and the ball manager (0x1DC) appended to logs/ms/dump.bin; records
 * {u32 frame, u32 va, u32 size, bytes} (tools/ms_dump.py reads them). */
#define MS_RUGBY_PTR 0x00A31988u
#include <stdio.h>
static void dump_obj(FILE *f, uint32_t frame, uint32_t va, uint32_t size) {
    if (va < RT_HEAP_LO || va + size > rt_heap_top()) return;
    uint32_t h[3] = { frame, va, size };
    fwrite(h, 4, 3, f);
    fwrite(G2H(va), 1, size, f);
}
static const char *ms_dir(void) {   /* RT_MS_DIR: where dump.bin and snap_F.bin go (default logs/ms) */
    const char *d = getenv("RT_MS_DIR");
    return d && *d ? d : "logs/ms";
}

static void dump_extra(FILE *f, uint32_t frame) {
    /* RT_MS_DUMP_EXTRA=VA:SIZE[,VA:SIZE...]: more ranges in the same records */
    for (const char *e = getenv("RT_MS_DUMP_EXTRA"); e && *e; e = strchr(e, ',') ? strchr(e, ',') + 1 : "") {
        uint32_t va = (uint32_t)strtoul(e, NULL, 16),
                 n = strchr(e, ':') ? (uint32_t)strtoul(strchr(e, ':') + 1, NULL, 16) : 4;
        uint32_t h[3] = { frame, va, n };
        fwrite(h, 4, 3, f);
        fwrite(G2H(va), 1, n, f);
    }
}

static void dump(uint32_t frame) {
    static FILE *f;
    uint32_t rugby = rd32(MS_RUGBY_PTR);
    if (!f) {
        char path[512];
        snprintf(path, sizeof path, "%s/dump.bin", ms_dir());
        if (!(f = fopen(path, "wb"))) return;
    }
    dump_extra(f, frame);   /* any frame: menus too */
    if (!rugby) {
        fflush(f);
        return;
    }
    dump_obj(f, frame, rugby, 0x100);
    dump_obj(f, frame, rd32(rugby + 0x98), 0x1DC);
    for (int t = 0; t < 2; t++) {
        uint32_t team = rd32(rugby + 0x68 + 4 * (uint32_t)t);
        if (!team) continue;
        dump_obj(f, frame, team, 0x1A4);
        for (uint32_t i = 0; i < rd8(team + 0x128) && i < 15; i++) {
            uint32_t pl = rd32(team + 0xD4 + 4 * i);
            dump_obj(f, frame, pl, 0x240);
            /* RT_MS_DUMP_SUB=OFF:SIZE[,...]: also the objects the player points at (player+OFF) */
            for (const char *e = getenv("RT_MS_DUMP_SUB"); e && *e; e = strchr(e, ',') ? strchr(e, ',') + 1 : "") {
                uint32_t off = (uint32_t)strtoul(e, NULL, 16),
                         n = strchr(e, ':') ? (uint32_t)strtoul(strchr(e, ':') + 1, NULL, 16) : 0x40;
                if (off + 4 <= 0x240) dump_obj(f, frame, rd32(pl + off), n);
            }
        }
    }
    fflush(f);
}

/* RT_MS_SNAP=F[,F...]: image and heap at those frames -> logs/ms/snap_F.bin
 * ({u32 va, u32 size, bytes} for [image), then [heap)). */
static void snap(uint32_t frame) {
    char path[512];
    snprintf(path, sizeof path, "%s/snap_%u.bin", ms_dir(), frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t r[2][2] = { { RT_IMAGE_BASE, RT_IMAGE_END }, { RT_HEAP_LO, rt_heap_top() } };
    int nr = getenv("RT_MS_SNAP_IMAGE") ? 1 : 2;   /* RT_MS_SNAP_IMAGE=1: the image only (17 MB, not ~1 GB) */
    for (int i = 0; i < nr; i++) {
        uint32_t h[2] = { r[i][0], r[i][1] - r[i][0] };
        fwrite(h, 4, 2, f);
        fwrite(G2H(r[i][0]), 1, h[1], f);
    }
    fclose(f);
    rt_trace("ms: snapshot %s", path);
}

void ms_probe_frame(uint32_t frame) {
    static const char *snap_at = (const char *)-1;
    if (snap_at == (const char *)-1) snap_at = getenv("RT_MS_SNAP");
    for (const char *p = snap_at; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "")
        if ((uint32_t)atol(p) == frame) snap(frame);
    static long d_a = -1, d_b, d_s = 1;
    if (d_a == -1) {
        const char *d = getenv("RT_MS_DUMP");
        d_a = -2;
        if (d) {
            d_a = atol(d);
            d_b = strchr(d, '-') ? atol(strchr(d, '-') + 1) : d_a;
            d_s = strchr(d, '/') ? atol(strchr(d, '/') + 1) : 1;
        }
    }
    if (d_a >= 0 && (long)frame >= d_a && (long)frame <= d_b && ((long)frame - d_a) % d_s == 0) dump(frame);
    static const char *scan_at = (const char *)-1;
    if (scan_at == (const char *)-1) scan_at = getenv("RT_MS_SCAN");
    for (const char *p = scan_at; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "")
        if ((uint32_t)atol(p) == frame) scan(frame);
}
