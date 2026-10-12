/* shadow_int.h -- the shadow check's shared state (shadow.c runs the reference, shadow_report.c compares). */
#ifndef SHADOW_INT_H
#define SHADOW_INT_H
#include "../core/interp_int.h"

#define SHADOW_VA 0xFFD00000u
typedef struct {
    uint32_t a, pc;
    uint8_t old;
} undo_t;
typedef struct {
    uint32_t a, pc;
    uint8_t v;
} fin_t;
typedef struct {
    const char *name;
    uint32_t va, ra, esp0, n, frame, bridges;
    uint32_t r[8];
    double st0;
    int depth, top0, mark, st0_ok;
    fin_t *fin;
    size_t nfin;
} shadow_t;

extern RECOMP_TLS shadow_t t_sh;   /* the check in flight on this thread */
extern RECOMP_TLS int t_sh_busy;
void shadow_ret(void);             /* the lifted body returns here (SHADOW_VA): compare and report */

#endif
