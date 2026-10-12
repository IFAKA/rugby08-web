/* user32_input.c -- keyboard and cursor state, resources and hooks (stand-ins), system metrics. */
#include "user32_int.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- resources, cursor, hooks ------------------------------------------------------------------ */
HLE(LoadIconA) { return 0x00F10000u | (a[1] & 0xFFFF); }
HLE(LoadCursorA) { return 0x00F20000u | (a[1] & 0xFFFF); }
HLE(LoadImageA) { return 0x00F30000u | (a[1] & 0xFFFF); }
HLE(DestroyIcon) { (void)a; return TRUE_; }

static uint32_t g_cursor;
static int g_cursor_count;
HLE(SetCursor) { uint32_t o = g_cursor; g_cursor = a[0]; return o; }
HLE(ShowCursor) { g_cursor_count += a[0] ? 1 : -1; plat_cursor_visible(g_cursor_count >= 0); return (uint32_t)g_cursor_count; }
HLE(GetCursorPos) {
    int x, y; plat_cursor_pos(&x, &y);
    static int n, lx = -1, ly = -1;
    if (rt_log_input && (x != lx || y != ly) && n < 300) { n++; lx = x; ly = y; rt_log("input: GetCursorPos -> %d,%d", x, y); }
    wr32(a[0], (uint32_t)x); wr32(a[0] + 4, (uint32_t)y); return TRUE_;
}
HLE(SetCursorPos) {
    static int n, lx = -1, ly = -1;
    if (rt_log_input && ((int)a[0] != lx || (int)a[1] != ly) && n < 300) { n++; lx = (int)a[0]; ly = (int)a[1]; rt_log("input: SetCursorPos(%d,%d)", (int)a[0], (int)a[1]); }
    plat_cursor_set_pos((int)a[0], (int)a[1]); return TRUE_;
}

/* Hooks (the game installs a keyboard hook to swallow the Windows key) are never called. */
HLE(SetWindowsHookExA) { (void)a; return 0x00F40001u; }
HLE(UnhookWindowsHookEx) { (void)a; return TRUE_; }
HLE(CallNextHookEx) { (void)a; return 0; }

/* ---- keyboard ---------------------------------------------------------------------------------- */
HLE(GetAsyncKeyState) { return plat_vk_down(a[0]) ? 0x8000u : 0; }
HLE(GetKeyState) { return (plat_vk_down(a[0]) ? 0xFF80u : 0) | plat_vk_toggled(a[0]); }
HLE(GetKeyboardState) { for (uint32_t k = 0; k < 256; k++) wr8(a[0] + k, (plat_vk_down(k) ? 0x80 : 0) | plat_vk_toggled(k)); return TRUE_; }
HLE(GetKeyboardLayout) { (void)a; return 0x04090409u; }
HLE(keybd_event) { (void)a; return 0; }

uint32_t vk_char(uint32_t vk, int shift) {
    if (vk >= 'A' && vk <= 'Z') return shift ? vk : vk + 32;
    if (vk >= '0' && vk <= '9') return shift ? (uint32_t)")!@#$%^&*("[vk - '0'] : vk;
    if (vk == 0x20 || vk == 0x0D || vk == 0x08 || vk == 0x1B || vk == 0x09) return vk;
    static const struct { uint8_t vk; char lo, hi; } oem[] = {
        { 0xBA, ';', ':' }, { 0xBB, '=', '+' }, { 0xBC, ',', '<' }, { 0xBD, '-', '_' }, { 0xBE, '.', '>' },
        { 0xBF, '/', '?' }, { 0xC0, '`', '~' }, { 0xDB, '[', '{' }, { 0xDC, '\\', '|' }, { 0xDD, ']', '}' },
        { 0xDE, '\'', '"' } };
    for (size_t i = 0; i < sizeof oem / sizeof oem[0]; i++)
        if (oem[i].vk == vk) return (uint32_t)(shift ? oem[i].hi : oem[i].lo);
    if (vk >= 0x60 && vk <= 0x69) return '0' + vk - 0x60;
    return 0;
}

HLE(MapVirtualKeyExA) {   /* (code, type, hkl) */
    switch (a[1]) {
    case 0: return plat_vk_to_scancode(a[0]);                          /* MAPVK_VK_TO_VSC */
    case 1: case 3:                                                    /* MAPVK_VSC_TO_VK(_EX) */
        for (uint32_t vk = 1; vk < 256; vk++) if (plat_vk_to_scancode(vk) == a[0]) return vk;
        return 0;
    case 2: { uint32_t c = vk_char(a[0], 0); return c >= 'a' && c <= 'z' ? c - 32 : c; }   /* MAPVK_VK_TO_CHAR */
    }
    return 0;
}

HLE(ToAsciiEx) {          /* (vk, scan, keystate, lpChar, flags, hkl) */
    int shift = a[2] && ((rd8(a[2] + 0x10) & 0x80) != 0);
    int caps = a[2] && (rd8(a[2] + 0x14) & 1);
    uint32_t c = vk_char(a[0], shift);
    if (caps && isalpha((int)c)) c ^= 0x20;
    if (!c) return 0;
    wr16(a[3], (uint16_t)c);
    return 1;
}

HLE(CharUpperA) {
    if (a[0] < 0x10000) return (uint32_t)toupper((int)(a[0] & 0xFF));
    for (uint32_t p = a[0]; rd8(p); p++) wr8(p, (uint8_t)toupper(rd8(p)));
    return a[0];
}

/* ---- metrics ----------------------------------------------------------------------------------- */
HLE(GetSystemMetrics) {
    int w, h;
    switch (a[0]) {
    case 0: case 16: case 78: plat_desktop_size(&w, &h); return (uint32_t)w;   /* CXSCREEN, CXFULLSCREEN, CXVIRTUALSCREEN */
    case 1: case 17: case 79: plat_desktop_size(&w, &h); return (uint32_t)h;
    case 4: return 19;                /* SM_CYCAPTION */
    case 5: case 6: return 1;         /* SM_CXBORDER, SM_CYBORDER */
    case 7: case 8: return 3;         /* SM_CXFIXEDFRAME, SM_CYFIXEDFRAME */
    case 32: case 33: return 4;       /* SM_CXFRAME, SM_CYFRAME */
    case 19: return 1;                /* SM_MOUSEPRESENT */
    case 43: return 3;                /* SM_CMOUSEBUTTONS */
    case 80: return 1;                /* SM_CMONITORS */
    }
    return 0;
}

HLE(SystemParametersInfoA) {   /* (action, uiParam, pvParam, winini) */
    uint32_t act = a[0], pv = a[2];
    switch (act) {
    case 0x30: {                      /* SPI_GETWORKAREA */
        int w, h;
        plat_desktop_size(&w, &h);
        wr32(pv, 0); wr32(pv + 4, 0); wr32(pv + 8, (uint32_t)w); wr32(pv + 12, (uint32_t)h);
        return TRUE_;
    }
    case 0x10: if (pv) wr32(pv, 0); return TRUE_;                      /* SPI_GETSCREENSAVEACTIVE */
    case 0x32: case 0x34: case 0x3A: if (pv) wr32(pv + 4, 0); return TRUE_;   /* SPI_GET{FILTER,TOGGLE,STICKY}KEYS: flags */
    }
    return TRUE_;
}
