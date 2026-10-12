/* shadow_report.c -- the end of a shadow check: the lifted run's registers and memory against the
 * interpreter's, reported to the trace, then the real return. */
#include "rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RT_INTERP
#include "shadow_int.h"

/* Memory the interpreter wrote, outside the dead stack below the entry esp: the dwords that differ
 * from the lifted run (traced, the first 24), and in *close the floats that differ only by rounding. */
static int mem_diffs(const shadow_t *sh, int *close) {
    int ndiff = 0, last = -1;
    for (size_t i = 0; i < sh->nfin; i++) {
        uint32_t a = sh->fin[i].a;
        if (a < sh->esp0 && a >= sh->esp0 - 0x40000) continue;
        if (a >= sh->esp0 && a < sh->esp0 + 4) continue;           /* the swapped return slot */
        if (*(uint8_t *)G2H(a) == sh->fin[i].v) continue;
        uint32_t d = a & ~3u;
        if ((int)d == last) continue;
        last = (int)d;
        uint8_t iv[4];
        memcpy(iv, G2H(d), 4);                    /* interpreter's dword: its bytes over ours */
        for (size_t j = i; j < sh->nfin && sh->fin[j].a < d + 4; j++) iv[sh->fin[j].a - d] = sh->fin[j].v;
        for (size_t j = i; j > 0 && sh->fin[j - 1].a >= d; j--) iv[sh->fin[j - 1].a - d] = sh->fin[j - 1].v;
        float lf, inf;
        memcpy(&lf, G2H(d), 4);
        memcpy(&inf, iv, 4);
        if (isfinite(lf) && isfinite(inf) && fabsf(lf) > 1e-30f && fabsf(lf - inf) <= 1e-5f * fabsf(inf)) {
            (*close)++;
            continue;
        }
        ndiff++;
        uint32_t iw;
        memcpy(&iw, iv, 4);
        if (ndiff <= 24)
            rt_trace("shadow sub_%08X %s #%u   [%08X] lifted %08X (%g) interp %08X (%g) written at %08X", sh->va,
                     sh->name, sh->n, d, rd(d), lf, iw, inf, sh->fin[i].pc);
    }
    return ndiff;
}

void shadow_ret(void) {
    shadow_t sh = t_sh;
    t_sh_busy = 0;
    static const char *rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    char regs[256] = "", *p = regs;
    for (int i = 0; i < 8; i++)
        if (r[i] != sh.r[i]) p += sprintf(p, " %s %08X/%08X", rn[i], r[i], sh.r[i]);
    /* modulo 8: x87 TOP is a ring. `ffree` tags a register empty without moving TOP (D3DX's matrix
       multiply ends each row with four of them), and the lifted top is an unwrapped count of it */
    if (((g_fp_top - sh.top0 - sh.depth) & 7) != 0) p += sprintf(p, " fpdepth %d/%d", g_fp_top - sh.top0, sh.depth);
    else if (sh.st0_ok) {   /* the interpreter ended with a value in ST0 (a result, or a _CI* return) */
        double a = g_st[0], b = sh.st0;
        if (!(a == b || fabs(a - b) <= 1e-9 * fabs(b) || (isnan(a) && isnan(b))))
            p += sprintf(p, " st0 %.9g/%.9g", a, b);
    }
    int close = 0, ndiff = mem_diffs(&sh, &close);
    static unsigned checked, bad;
    checked++;
    if (regs[0] || ndiff) bad++;
    if (sh.name[0] || regs[0] || ndiff || checked % 500 == 0)
        if (g_cur_func != sh.va)        /* the lifted ret that came back here: normally the function's own */
            p += sprintf(p, " (returned from sub_%08X, entry esp %08X)", g_cur_func, sh.esp0);
    rt_trace("shadow sub_%08X %s #%u (frame %u): %s%s; %d memory diffs, %d fp-close, %zu bytes written, %u imports [%u "
             "checked, %u mismatched]",
             sh.va, sh.name, sh.n, sh.frame, (regs[0] || ndiff) ? "MISMATCH" : "match", regs, ndiff, close, sh.nfin,
             sh.bridges, checked, bad);
    if (regs[0] || ndiff) {
        uint32_t ent[24];
        char list[24 * 9 + 1] = "", *q = list;
        int ne = rt_enter_since((uint32_t)sh.mark, ent, 24);
        for (int i = 0; i < ne; i++) q += sprintf(q, " %08X", ent[i]);
        rt_trace("shadow sub_%08X #%u   lifted run entered:%s", sh.va, sh.n, ne ? list : " (none)");
    }
    free(sh.fin);
    if (sh.ra == RECOMP_RETADDR) return;
    recomp_func_t fn = native_at(sh.ra);
    if (!fn) rt_fatal("shadow: no handler for the return address 0x%08X", sh.ra);
    fn();
}

#endif
