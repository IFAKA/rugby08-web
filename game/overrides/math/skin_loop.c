/* FUN_00773FD0's x87 vertex loop (0x774300..0x7743A2): CPU skinning, the hottest code in a match after
 * the matrix helpers. Per vertex (stride 0x20 in: position, bone index at +0xC, normal at +0x10), the
 * position goes through the bone matrix (0xA62A80 + index * 64) with FUN_0079B230 and the normal through
 * its 3x3 part; the output vertex is `stride` (edi) bytes long. The rest of the function (its buffer
 * locks and the SSE/3DNow paths) stays lifted.
 *
 * Same results as the lifted loop bit for bit (checked by the deterministic state hashes): the normal is
 * (n.x * m[0][k] + n.y * m[1][k]) + n.z * m[2][k] in doubles, one rounding to float. Also the same end
 * state: registers, flags, the frame's locals and the bytes each call leaves below esp. The x87 stack
 * needs nothing: the lifted loop leaves st(0..1) as they were and st(2..7) zero, as FUN_0079B230 does. */
#pragma STDC FP_CONTRACT OFF
#include "game.h"

void sub_0079B230(void);

GAME_BLOCK(00774300, 007743A2) {
    const uint32_t s = g_esp, stride = g_edi; /* esp holds still across the loop */
    uint32_t src = g_esi, dst = g_ecx, out, cf, left;
    do {
        uint32_t mat = (rd32(src + 0xC) << 6) + 0xA62A80u;
        wr32(s - 4, dst), wr32(s - 8, mat), wr32(s - 12, src); /* push dst, mat, src */
        wr32(s + 0x38, mat);
        g_eax = mat, g_ebx = dst, g_ecx = dst;
        g_esp = s - 16; /* the call: its dummy return address */
        wr32(g_esp, RECOMP_RETADDR);
        uint32_t caller = g_cur_func;
        sub_0079B230(); /* the position: dst = (p, 1) * mat */
        g_cur_func = caller;
        uint32_t n = src + 0x10;
        out = dst + 0x10;
        wr32(s + 0x3C, out), wr32(s + 0x34, n);
        double x = MEMF(n), y = MEMF(n + 4), z = MEMF(n + 8), r[3];
        for (uint32_t k = 0; k < 3; k++) {
            double a = x * MEMF(mat + 4 * k), b = y * MEMF(mat + 16 + 4 * k);
            double t = a + b;
            r[k] = t + z * MEMF(mat + 32 + 4 * k);
        }
        for (uint32_t k = 0; k < 3; k++) MEMF(out + 4 * k) = (float)r[k];
        dst = rd32(s + 0x14) + stride;
        cf = src + 0x20u < src;
        src += 0x20u;
        left = rd32(s + 0x20);
        wr32(s + 0x14, dst), wr32(s + 0x20, left - 1);
    } while (left != 1);
    g_esp = s;
    g_eax = 0, g_ebx = out, g_ecx = dst, g_edx = out, g_esi = src;
    g_flag_k = FK_DEC, g_flag_a = 1, g_flag_b = 1, g_flag_cf = cf;
}
