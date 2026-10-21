/* FUN_007BA0D6 (stdcall: out, m1, m2): D3DXMatrixMultiply's x87 path, out = m1 * m2 (row-major 4x4 floats).
 * The hottest game function in a match (every bone and object transform). Same arithmetic as the lifted
 * body, in the same order, in doubles (the x87 at 53-bit precision): products are exact, then
 * (p0 + p2) + (p1 + p3), then one rounding to float. The original walks columns when out == m2 and rows
 * otherwise, and copies m2 to its frame when all three are the same matrix; computing every element from
 * the inputs before writing gives the same results for those exact aliases. Partly overlapping matrices
 * (never seen) run the lifted body. The end state matches it too: registers, flags, the x87 stack (four
 * loads left behind: FFREE frees but does not pop) and the bytes its frame writes below esp. */
#pragma STDC FP_CONTRACT OFF
#include "game.h"

static int part_overlap(uint32_t a, uint32_t b) { return a != b && (a - b < 64u || b - a < 64u); }

GAME_OVERRIDE(sub_007BA0D6) {
    uint32_t out = GAME_ARG(0), m1 = GAME_ARG(1), m2 = GAME_ARG(2);
    if (part_overlap(out, m1) || part_overlap(out, m2)) {
        sub_007BA0D6_lifted();
        return;
    }
    RECOMP_ENTER(0x007BA0D6u); /* counted as an entry: the deterministic scheduler yields every 4096 */
    uint32_t e = g_esp, fp = e - 4; /* push ebp; mov ebp, esp; sub esp, 0x40; push ebx, esi, edi */
    wr32(fp, g_ebp);
    wr32(fp - 0x44, g_ebx), wr32(fp - 0x48, g_esi), wr32(fp - 0x4C, g_edi);
    int cols = m2 == out && m1 != out; /* the column walk: m2 is overwritten in place */
    uint32_t cf = cols ? 1u : (m2 == out ? 0u : (uint32_t)(m2 < out));
    if (m2 == out && m1 == out) { /* rep movsd m2 to the frame, which becomes m2 */
        for (uint32_t k = 0; k < 64; k += 4) wr32(fp - 0x40 + k, rd32(m2 + k));
        m2 = fp - 0x40;
        wr32(fp + 0x10, m2);
    }
    float a[16], b[16], r[16]; /* inputs read once, before any output is written */
    __builtin_memcpy(a, (const void *)ADDR(m1), sizeof a);
    __builtin_memcpy(b, (const void *)ADDR(m2), sizeof b);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double p0 = (double)a[4 * i] * b[j], p1 = (double)a[4 * i + 1] * b[4 + j];
            double p2 = (double)a[4 * i + 2] * b[8 + j], p3 = (double)a[4 * i + 3] * b[12 + j];
            double s = p2 + p0, t = p1 + p3;
            r[4 * i + j] = (float)(s + t);
        }
    /* the last four loads stay on the stack: m2's column 3 (column walk) or m1's row 3 (row walk) */
    for (int k = 0; k < 4; k++) g_st[k] = cols ? b[15 - 4 * k] : a[15 - k];
    for (int k = 4; k < 8; k++) g_st[k] = 0;
    g_fp_top += 16;
    __builtin_memcpy((void *)ADDR(out), r, sizeof r);
    g_ecx = cols ? m1 : m1 + 64, g_edx = m2;
    g_flag_k = FK_INC, g_flag_a = 0xFFFFFFFFu, g_flag_b = 1, g_flag_cf = cf;
    GAME_RETURN(out, 12);
}
