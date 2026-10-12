/* k32_locale.c -- code pages (1252 ANSI, 437 OEM), UTF-16 conversion, the en-US locale, string
 * typing, case mapping and comparison. */
#include "win32.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ---- code pages ---------------------------------------------------------------------------------- */
HLE(GetACP) {
    (void)a;
    return 1252;
}
HLE(GetOEMCP) {
    (void)a;
    return 437;
}
HLE(IsValidCodePage) { return (a[0] == 1252 || a[0] == 437 || a[0] == 65001) ? TRUE_ : FALSE_; }
HLE(GetCPInfo) {   /* CPINFO: MaxCharSize(4) DefaultChar[2] LeadByte[12] */
    memset(G2H(a[1]), 0, 18);
    wr32(a[1], 1);
    wr8(a[1] + 4, '?');
    return TRUE_;
}

static uint32_t mb_len(uint32_t s, int32_t n) { return n >= 0 ? (uint32_t)n : gstr_len(s) + 1; }

HLE(MultiByteToWideChar) {   /* (cp, flags, src, srclen, dst, dstlen) */
    uint32_t src = a[2], dst = a[4], cap = a[5];
    uint32_t n = mb_len(src, (int32_t)a[3]);
    if (!cap) return n;
    if (cap < n) {
        set_err(ERROR_INSUFFICIENT_BUFFER_);
        n = cap;
    }
    for (uint32_t i = 0; i < n; i++) wr16(dst + 2 * i, cp1252_to_u16(rd8(src + i)));
    return n;
}

HLE(WideCharToMultiByte) {   /* (cp, flags, src, srclen, dst, dstlen, defchar, useddef) */
    uint32_t src = a[2], dst = a[4], cap = a[5];
    int32_t sl = (int32_t)a[3];
    uint32_t n = 0;
    if (sl < 0) {
        while (rd16(src + 2 * n)) n++;
        n++;
    } else n = (uint32_t)sl;
    if (a[7]) wr32(a[7], 0);
    if (!cap) return n;
    if (cap < n) {
        set_err(ERROR_INSUFFICIENT_BUFFER_);
        n = cap;
    }
    for (uint32_t i = 0; i < n; i++) {
        int c = u16_to_cp1252(rd16(src + 2 * i));
        if (c < 0) {
            c = a[6] ? rd8(a[6]) : '?';
            if (a[7]) wr32(a[7], 1);
        }
        wr8(dst + i, (uint8_t)c);
    }
    return n;
}

/* ---- locale ---------------------------------------------------------------------------------------- */
HLE(GetUserDefaultLCID) {
    (void)a;
    return 0x0409;
}
HLE(IsValidLocale) {
    (void)a;
    return TRUE_;
}
HLE(EnumSystemLocalesA) {
    if (a[0]) guest_call(a[0], 1, gstrdup("00000409"));
    return TRUE_;
}

static const char *locale_str(uint32_t type) {
    switch (type & 0xFFFF) {
    case 0x01: return "0409";           /* LOCALE_ILANGUAGE */
    case 0x03: return "ENU";            /* LOCALE_SABBREVLANGNAME */
    case 0x07: return "USA";            /* LOCALE_SABBREVCTRYNAME */
    case 0x0B: return "437";            /* LOCALE_IDEFAULTCODEPAGE */
    case 0x0E: return ".";              /* LOCALE_SDECIMAL */
    case 0x0F: return ",";              /* LOCALE_STHOUSAND */
    case 0x1004: return "1252";         /* LOCALE_IDEFAULTANSICODEPAGE */
    case 0x1001: return "English";      /* LOCALE_SENGLANGUAGE */
    case 0x1002: return "United States";
    case 0x14: return "$";
    case 0x1E: return ":";
    case 0x1D: return "/";
    default: return "";
    }
}
HLE(GetLocaleInfoA) {
    const char *s = locale_str(a[1]);
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (!a[3]) return n;
    if (a[3] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    gstr_put(a[2], s, a[3]);
    return n;
}
HLE(GetLocaleInfoW) {
    const char *s = locale_str(a[1]);
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (!a[3]) return n;
    if (a[3] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (uint32_t i = 0; i < n; i++) wr16(a[2] + 2 * i, (uint8_t)s[i]);
    return n;
}

/* C1 character types (GetStringType CT_CTYPE1) for code page 1252 / Latin-1 */
static uint16_t ctype1(uint16_t c) {
    uint16_t t = 0;
    if (c < 128) {
        if (isupper(c)) t |= 0x001;
        if (islower(c)) t |= 0x002;
        if (isdigit(c)) t |= 0x004;
        if (isspace(c)) t |= 0x008;
        if (ispunct(c)) t |= 0x010;
        if (iscntrl(c)) t |= 0x020;
        if (c == ' ') t |= 0x040;
        if (isxdigit(c)) t |= 0x080;
        if (isalpha(c)) t |= 0x100;
    } else if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7) {
        t = 0x100 | (c < 0xDF ? 0x001 : 0x002);
    } else if (c >= 0xA0) t = 0x010;
    return t;
}
HLE(GetStringTypeW) {   /* (type, src, len, out) */
    int32_t n = (int32_t)a[2];
    if (n < 0) {
        n = 0;
        while (rd16(a[1] + 2 * (uint32_t)n)) n++;
        n++;
    }
    for (int32_t i = 0; i < n; i++) wr16(a[3] + 2 * (uint32_t)i, ctype1(rd16(a[1] + 2 * (uint32_t)i)));
    return TRUE_;
}
HLE(GetStringTypeA) {   /* (lcid, type, src, len, out) */
    int32_t n = (int32_t)a[3];
    if (n < 0) n = (int32_t)gstr_len(a[2]) + 1;
    for (int32_t i = 0; i < n; i++) wr16(a[4] + 2 * (uint32_t)i, ctype1(cp1252_to_u16(rd8(a[2] + (uint32_t)i))));
    return TRUE_;
}

static uint16_t map_case(uint16_t c, uint32_t flags) {
    if (flags & 0x100) {
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 32;
    }   /* LCMAP_LOWERCASE */
    if (flags & 0x200) {
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 32;
    }   /* LCMAP_UPPERCASE */
    return c;
}
HLE(LCMapStringW) {   /* (lcid, flags, src, srclen, dst, dstlen) */
    int32_t n = (int32_t)a[3];
    if (n < 0) {
        n = 0;
        while (rd16(a[2] + 2 * (uint32_t)n)) n++;
        n++;
    }
    if (!a[5]) return (uint32_t)n;
    if ((int32_t)a[5] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (int32_t i = 0; i < n; i++) wr16(a[4] + 2 * (uint32_t)i, map_case(rd16(a[2] + 2 * (uint32_t)i), a[1]));
    return (uint32_t)n;
}
HLE(LCMapStringA) {
    int32_t n = (int32_t)a[3];
    if (n < 0) n = (int32_t)gstr_len(a[2]) + 1;
    if (!a[5]) return (uint32_t)n;
    if ((int32_t)a[5] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (int32_t i = 0; i < n; i++) {
        int c = u16_to_cp1252(map_case(cp1252_to_u16(rd8(a[2] + (uint32_t)i)), a[1]));
        wr8(a[4] + (uint32_t)i, (uint8_t)(c < 0 ? '?' : c));
    }
    return (uint32_t)n;
}

static int cmp_fold(uint16_t x, uint16_t y, uint32_t flags) {
    if (flags & 1 /* NORM_IGNORECASE */) {
        x = map_case(x, 0x100);
        y = map_case(y, 0x100);
    }
    return (int)x - (int)y;
}
HLE(CompareStringA) {   /* (lcid, flags, s1, n1, s2, n2) -> 1 <, 2 =, 3 > */
    int32_t n1 = (int32_t)a[3], n2 = (int32_t)a[5];
    if (n1 < 0) n1 = (int32_t)gstr_len(a[2]);
    if (n2 < 0) n2 = (int32_t)gstr_len(a[4]);
    for (int32_t i = 0; i < n1 && i < n2; i++) {
        int d = cmp_fold(cp1252_to_u16(rd8(a[2] + (uint32_t)i)), cp1252_to_u16(rd8(a[4] + (uint32_t)i)), a[1]);
        if (d) return d < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}
HLE(CompareStringW) {
    int32_t n1 = (int32_t)a[3], n2 = (int32_t)a[5];
    if (n1 < 0) {
        n1 = 0;
        while (rd16(a[2] + 2 * (uint32_t)n1)) n1++;
    }
    if (n2 < 0) {
        n2 = 0;
        while (rd16(a[4] + 2 * (uint32_t)n2)) n2++;
    }
    for (int32_t i = 0; i < n1 && i < n2; i++) {
        int d = cmp_fold(rd16(a[2] + 2 * (uint32_t)i), rd16(a[4] + 2 * (uint32_t)i), a[1]);
        if (d) return d < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}
