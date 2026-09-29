/* cpu_ext.c -- helpers for instructions tools/lift_ext.py adds on top of lift32. */
#include "rt.h"
#include <math.h>

void recomp_fault(uint32_t va, const char *what) {
    rt_fatal("CPU fault %s at 0x%08X (in sub_%08X)", what, va, g_cur_func);
}

/* #DE: the original never divides by zero on a real path, so this marks a bug upstream. Log each
 * site once and continue with a 0 result (pcrecomp's policy) rather than kill the game. */
void recomp_soft_fault(uint32_t va, const char *what) {
    enum { N = 256 };
    static uint32_t seen[N];
    static int n;
    for (int i = 0; i < n; i++) if (seen[i] == va) return;
    if (n < N) seen[n++] = va;
    rt_log("CPU soft fault %s at 0x%08X (in sub_%08X), continuing", what, va, g_cur_func);
}

/* Flags word bits */
#define F_CF 0x001u
#define F_PF 0x004u
#define F_AF 0x010u
#define F_ZF 0x040u
#define F_SF 0x080u

static uint32_t szp(uint32_t fl, uint8_t r) {
    fl &= ~(F_SF | F_ZF | F_PF);
    if (r == 0) fl |= F_ZF;
    if (r & 0x80) fl |= F_SF;
    if (!(__builtin_popcount(r) & 1)) fl |= F_PF;
    return fl;
}

void recomp_bcd_daa(uint32_t *eax, uint32_t *fl) {
    uint8_t al = (uint8_t)*eax, old = al;
    uint32_t f = *fl, cf = f & F_CF;
    f &= ~F_CF;
    if ((al & 0x0F) > 9 || (f & F_AF)) { al += 6; f |= F_AF; if (cf || old > 0xF9) f |= F_CF; }
    else f &= ~F_AF;
    if (old > 0x99 || cf) { al += 0x60; f |= F_CF; }
    *eax = (*eax & 0xFFFFFF00u) | al;
    *fl = szp(f, al);
}

void recomp_bcd_das(uint32_t *eax, uint32_t *fl) {
    uint8_t al = (uint8_t)*eax, old = al;
    uint32_t f = *fl, cf = f & F_CF;
    f &= ~F_CF;
    if ((al & 0x0F) > 9 || (f & F_AF)) { if (old < 6 || cf) f |= F_CF; al -= 6; f |= F_AF; }
    else f &= ~F_AF;
    if (old > 0x99 || cf) { al -= 0x60; f |= F_CF; }
    *eax = (*eax & 0xFFFFFF00u) | al;
    *fl = szp(f, al);
}

void recomp_bcd_aaa(uint32_t *eax, uint32_t *fl) {
    uint32_t a = *eax, f = *fl;
    if ((a & 0x0F) > 9 || (f & F_AF)) {
        uint16_t ax = (uint16_t)(a + 0x106);
        a = (a & 0xFFFF0000u) | ax;
        f |= F_AF | F_CF;
    } else f &= ~(F_AF | F_CF);
    *eax = (a & 0xFFFFFF0Fu) & ~0xF0u;
    *fl = f;
}

void recomp_bcd_aas(uint32_t *eax, uint32_t *fl) {
    uint32_t a = *eax, f = *fl;
    if ((a & 0x0F) > 9 || (f & F_AF)) {
        uint16_t ax = (uint16_t)(a - 6);
        uint8_t ah = (uint8_t)((ax >> 8) - 1);
        a = (a & 0xFFFF0000u) | ((uint32_t)ah << 8) | (ax & 0xFF);
        f |= F_AF | F_CF;
    } else f &= ~(F_AF | F_CF);
    *eax = a & 0xFFFFFF0Fu;
    *fl = f;
}

void recomp_bcd_aam(uint32_t *eax, uint32_t *fl, uint32_t imm) {
    uint8_t al = (uint8_t)*eax;
    uint8_t ah = (uint8_t)(al / imm), r = (uint8_t)(al % imm);
    *eax = (*eax & 0xFFFF0000u) | ((uint32_t)ah << 8) | r;
    *fl = szp(*fl, r);
}

void recomp_bcd_aad(uint32_t *eax, uint32_t *fl, uint32_t imm) {
    uint8_t al = (uint8_t)*eax, ah = (uint8_t)(*eax >> 8);
    uint8_t r = (uint8_t)(al + ah * imm);
    *eax = (*eax & 0xFFFF0000u) | r;
    *fl = szp(*fl, r);
}

/* 80-bit packed BCD: 9 bytes of 2 digits (low first), byte 9 bit 7 = sign */
double recomp_fbld(uint32_t va) {
    double v = 0;
    for (int i = 8; i >= 0; i--) {
        uint8_t b = rd8(va + (uint32_t)i);
        v = v * 100.0 + (b >> 4) * 10 + (b & 0x0F);
    }
    return (rd8(va + 9) & 0x80) ? -v : v;
}

void recomp_fbstp(uint32_t va, double v) {
    int neg = v < 0;
    double a = fabs(v);
    if (!(a < 1e18)) {            /* invalid: store the packed-BCD indefinite */
        for (int i = 0; i < 7; i++) wr8(va + (uint32_t)i, 0);
        wr8(va + 7, 0xC0); wr8(va + 8, 0xFF); wr8(va + 9, 0xFF);
        return;
    }
    uint64_t n = (uint64_t)a;
    for (int i = 0; i < 9; i++) {
        uint8_t lo = (uint8_t)(n % 10); n /= 10;
        uint8_t hi = (uint8_t)(n % 10); n /= 10;
        wr8(va + (uint32_t)i, (uint8_t)((hi << 4) | lo));
    }
    wr8(va + 9, neg ? 0x80 : 0);
}
