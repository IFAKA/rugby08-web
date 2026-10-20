/* miss.c -- dispatch misses: each guest VA without a lifted body is reported once (logs/icall_miss.txt),
 * with the code around it dumped for tools/lift/aems_lift.py when it lies outside the image. */
#include "rt.h"
#include "aems.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>

/* ---- dispatch misses ---------------------------------------------------------------------------- */
#define MAX_MISSES 65536
#define MISS_SLOTS (2 * MAX_MISSES) /* open-addressing set of VAs already reported; slot 0 = empty */
static uint32_t g_miss[MISS_SLOTS];
static int g_nmiss, g_miss_zero; /* g_miss_zero: VA 0 was reported (it cannot live in the set) */
static pthread_mutex_t g_miss_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_miss_f;

/* Code outside the image is loaded at run time (EA AEMS sound modules from Audio/aems.big). Its
 * surroundings are dumped to logs/dyncode/<va>.bin (u32 start, then 64 KiB of guest memory) so
 * tools/aems_capture.py can match the relocated module against the bank files. Native only. */
static void dyncode_capture(uint32_t va) {
    if (va < 0x10000u || (va >= RT_IMAGE_BASE && va < RT_IMAGE_END) || va >= RT_BRIDGE_HOST) return;
    uint32_t start = (va > 0x18000u ? va - 0x8000u : 0x10000u) & ~0xFFFu, n = 0x10000u;
    if (start + n > rt_guest_size) return;
    mkdir("logs/dyncode", 0755);
    char path[64];
    snprintf(path, sizeof path, "logs/dyncode/%08X.bin", va);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&start, 4, 1, f);
    fwrite(G2H(start), 1, n, f);
    fclose(f);
}

static uint32_t g_miss_calls;   /* every miss, repeats included (selftests) */
uint32_t recomp_miss_calls(void) { return __atomic_load_n(&g_miss_calls, __ATOMIC_RELAXED); }

void recomp_miss(int tail, uint32_t va, uint32_t from) {
    __atomic_add_fetch(&g_miss_calls, 1, __ATOMIC_RELAXED);
    pthread_mutex_lock(&g_miss_mu);
    if (va == 0) {
        if (g_miss_zero) {
            pthread_mutex_unlock(&g_miss_mu);
            return;
        }
        g_miss_zero = 1;
    } else {
        uint32_t h = (va * 2654435761u) >> 15 & (MISS_SLOTS - 1);
        while (g_miss[h] && g_miss[h] != va) h = (h + 1) & (MISS_SLOTS - 1);
        if (g_miss[h] == va) {
            pthread_mutex_unlock(&g_miss_mu);
            return;
        }
        if (g_nmiss < MAX_MISSES) {
            g_miss[h] = va;
            g_nmiss++;
        } /* full: report, but do not remember */
    }
    if (!g_miss_f) g_miss_f = fopen(getenv("RT_MISS_FILE") ? getenv("RT_MISS_FILE") : "logs/icall_miss.txt", "w");
    if (g_miss_f) {
        fprintf(g_miss_f, "0x%08X %s from 0x%08X\n", va, tail ? "ITAIL" : "ICALL", from);
        fflush(g_miss_f);
    }
    pthread_mutex_unlock(&g_miss_mu);
    rt_log("%s: unresolved VA 0x%08X from 0x%08X", tail ? "ITAIL" : "ICALL", va, from);
    dyncode_capture(va);
}
