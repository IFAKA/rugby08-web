/* strings.c -- guest string helpers and the Windows-1252 <-> UTF-16 tables. */
#include "win32.h"
#include <string.h>

uint32_t gstr_put(uint32_t va, const char *s, uint32_t cap) {
    if (!va || !cap) return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(G2H(va), s, n);
    wr8(va + n, 0);
    return n;
}

uint32_t gstr_len(uint32_t va) { return va ? (uint32_t)strlen((const char *)G2H(va)) : 0; }

/* 0x80..0x9F of Windows-1252 (0 = undefined, mapped to itself) */
static const uint16_t k1252[32] = { 0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178 };

uint16_t cp1252_to_u16(uint8_t c) {
    if (c >= 0x80 && c < 0xA0 && k1252[c - 0x80]) return k1252[c - 0x80];
    return c;
}

int u16_to_cp1252(uint16_t w) {
    if (w < 0x80 || (w >= 0xA0 && w < 0x100)) return w;
    for (int i = 0; i < 32; i++)
        if (k1252[i] == w) return 0x80 + i;
    if (w >= 0x80 && w < 0xA0) return w;
    return -1;
}
