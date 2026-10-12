/* debug.h -- the opt-in debugging tools (runtime/debug/): each is armed by an environment variable
 * or a tools/hooks.json name and costs nothing otherwise. */
#ifndef RT_DEBUG_H
#define RT_DEBUG_H
#include "rt.h"

void rt_probe_enter(uint32_t va);      /* probes.c: RT_PROBE / RT_PROBE_DUMP at a function entry */
void rt_dump_at(uint32_t frame);       /* probes.c: RT_DUMP memory ranges at a presented frame */
void rt_coverage_mark(uint32_t va);    /* coverage.c: RT_COVERAGE, a function entered */
void rt_selftest_crt(void);            /* selftest_crt.c: RT_SELFTEST_CRT, then exit */
#ifdef __cplusplus
extern "C"
#endif
    void ms_probe_frame(uint32_t frame);   /* match_probe.c: RT_MS_SCAN / RT_MS_DUMP / RT_MS_SNAP at a Present */

#endif
