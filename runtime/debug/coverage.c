/* coverage.c -- RT_COVERAGE="from-to:file[,...]": the lifted functions entered during a window of
 * presented frames. rt_coverage_frame runs at every Present (it also advances rt_present_frame). */
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *g_cov;              /* one bit per byte of [RT_IMAGE_BASE, RT_IMAGE_END) */
static volatile int g_cov_on;

void rt_coverage_mark(uint32_t va) {
    if (g_cov_on && va >= RT_IMAGE_BASE && va < RT_IMAGE_END) {
        uint32_t o = va - RT_IMAGE_BASE;
        g_cov[o >> 3] |= (uint8_t)(1u << (o & 7));
    }
}

void rt_coverage_frame(uint32_t frame) {
    static const char *spec;
    static int parsed;
    rt_present_frame = frame;
    rt_dump_at(frame);
    dbg_quit_frame(frame);
    if (!parsed) {
        parsed = 1;
        spec = getenv("RT_COVERAGE");
    }
    if (!spec) return;
    for (const char *p = spec; *p;) {
        unsigned from = 0, to = 0;
        char file[256] = "";
        int n = 0;
        if (sscanf(p, "%u-%u:%255[^,]%n", &from, &to, file, &n) < 3) return;
        if (frame == from) {
            size_t sz = (RT_IMAGE_END - RT_IMAGE_BASE + 7) / 8;
            if (!g_cov) g_cov = malloc(sz);
            memset(g_cov, 0, sz);
            g_cov_on = 1;
        }
        if (frame == to && g_cov) {
            g_cov_on = 0;
            FILE *f = fopen(file, "w");
            for (uint32_t o = 0; f && o < RT_IMAGE_END - RT_IMAGE_BASE; o++)
                if (g_cov[o >> 3] & (1u << (o & 7))) fprintf(f, "%08X\n", RT_IMAGE_BASE + o);
            if (f) fclose(f);
            rt_log("coverage %u-%u -> %s", from, to, file);
        }
        p += n;
        if (*p == ',') p++;
    }
}
