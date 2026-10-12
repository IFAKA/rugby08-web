/* k32_time.c -- tick counts, the performance counter, wall-clock time (virtual time in
 * deterministic mode, runtime/core/det.c). */
#include "win32.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ---- time ------------------------------------------------------------------------------------------ */
uint64_t rt_now_ns(void) {   /* deterministic mode: virtual time (det.c) */
    if (det_on()) return det_clock_read();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t g_boot_ns;
uint32_t rt_ms(void) {
    if (!g_boot_ns) g_boot_ns = rt_now_ns();
    return (uint32_t)((rt_now_ns() - g_boot_ns) / 1000000ull);
}
HLE(GetTickCount) { (void)a; return rt_ms(); }
HLE(QueryPerformanceFrequency) { wr32(a[0], 1000000u); wr32(a[0] + 4, 0); return TRUE_; }
HLE(QueryPerformanceCounter) {
    uint64_t us = rt_now_ns() / 1000ull;
    wr32(a[0], (uint32_t)us); wr32(a[0] + 4, (uint32_t)(us >> 32));
    return TRUE_;
}

/* FILETIME: 100 ns since 1601-01-01 */
static uint64_t unix_to_filetime(uint64_t sec, uint64_t ns) { return (sec + 11644473600ull) * 10000000ull + ns / 100; }
HLE(GetSystemTimeAsFileTime) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (det_on()) { uint64_t v = det_now_ns(); tv.tv_sec = (time_t)(1199145600ull + v / 1000000000ull); tv.tv_usec = (int)(v / 1000 % 1000000); }   /* 2008-01-01 + virtual time */
    uint64_t ft = unix_to_filetime((uint64_t)tv.tv_sec, (uint64_t)tv.tv_usec * 1000);
    wr32(a[0], (uint32_t)ft); wr32(a[0] + 4, (uint32_t)(ft >> 32));
    return 0;
}
HLE(FileTimeToLocalFileTime) { wr32(a[1], rd32(a[0])); wr32(a[1] + 4, rd32(a[0] + 4)); return TRUE_; }   /* UTC */
HLE(FileTimeToSystemTime) {   /* SYSTEMTIME: 8 WORDs */
    uint64_t ft = rd32(a[0]) | ((uint64_t)rd32(a[0] + 4) << 32);
    time_t t = (time_t)(ft / 10000000ull - 11644473600ull);
    struct tm tm;
    gmtime_r(&t, &tm);
    wr16(a[1] + 0, (uint16_t)(tm.tm_year + 1900)); wr16(a[1] + 2, (uint16_t)(tm.tm_mon + 1));
    wr16(a[1] + 4, (uint16_t)tm.tm_wday); wr16(a[1] + 6, (uint16_t)tm.tm_mday);
    wr16(a[1] + 8, (uint16_t)tm.tm_hour); wr16(a[1] + 10, (uint16_t)tm.tm_min);
    wr16(a[1] + 12, (uint16_t)tm.tm_sec); wr16(a[1] + 14, (uint16_t)((ft / 10000) % 1000));
    return TRUE_;
}
HLE(GetTimeZoneInformation) { memset(G2H(a[0]), 0, 172); return 0; /* TIME_ZONE_ID_UNKNOWN: UTC */ }
