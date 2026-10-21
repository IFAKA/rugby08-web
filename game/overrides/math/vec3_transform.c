/* FUN_0079B230 (cdecl: v, m, out): out = (v.x, v.y, v.z, 1) * m, a float3 point through a 4x4 float matrix
 * (the game's D3DXVec3TransformCoord without the divide). Its hottest function in a match: the players'
 * CPU skinning (FUN_00773FD0) calls it for every vertex. Written in C as the lifted x87 code computes it:
 * each product exact in double, the sums in the same order in double, each component rounded to float
 * once. Same results bit for bit, without the emulated x87 stack (whose six pushes leave st(2..7) zero:
 * later code reads them, so this does too). */
#pragma STDC FP_CONTRACT OFF
#include "game.h"

GAME_OVERRIDE(sub_0079B230) {
    RECOMP_ENTER(0x0079B230u);   /* counted as an entry: the deterministic scheduler yields every 4096 */
    uint32_t v = GAME_ARG(0), m = GAME_ARG(1), out = GAME_ARG(2);
    double x = MEMF(v), y = MEMF(v + 4), z = MEMF(v + 8);
    double r[3];   /* every input read before any output is written: out may overlap v or m */
    for (uint32_t i = 0; i < 3; i++) {
        double s = x * MEMF(m + 4 * i);
        double t = y * MEMF(m + 16 + 4 * i);
        double u = z * MEMF(m + 32 + 4 * i);
        s += t;
        s += u;
        r[i] = s + MEMF(m + 48 + 4 * i);
    }
    for (uint32_t i = 0; i < 3; i++) MEMF(out + 4 * i) = (float)r[i];
    for (int i = 2; i < 8; i++) g_st[i] = 0;
    g_eax = v, g_ecx = m, g_edx = out;
    GAME_RETURN(v, 0);
}
