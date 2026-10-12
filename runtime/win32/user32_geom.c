/* user32_geom.c -- client rectangles and coordinates, window position, focus, painting. */
#include "user32_int.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void client_size(wnd_t *w, int *cw, int *ch) {
    if (w->host && plat_window_is_open()) plat_window_rect(NULL, NULL, cw, ch);
    else { *cw = w->w; *ch = w->h; }
}

HLE(GetClientRect) {
    wnd_t *w = wnd_get(a[0]);
    int cw = 0, ch = 0;
    if (a[0] == HWND_DESKTOP_) plat_desktop_size(&cw, &ch);
    else if (w) client_size(w, &cw, &ch);
    else return set_err(1400);
    wr32(a[1], 0); wr32(a[1] + 4, 0); wr32(a[1] + 8, (uint32_t)cw); wr32(a[1] + 12, (uint32_t)ch);
    return TRUE_;
}

static void origin(uint32_t hwnd, int *x, int *y) {
    wnd_t *w = wnd_get(hwnd);
    *x = *y = 0;
    if (w && w->host) plat_window_rect(x, y, NULL, NULL);
}

HLE(ClientToScreen) {
    int x, y;
    origin(a[0], &x, &y);
    wr32(a[1], rd32(a[1]) + (uint32_t)x); wr32(a[1] + 4, rd32(a[1] + 4) + (uint32_t)y);
    return TRUE_;
}

HLE(ScreenToClient) {
    int x, y;
    origin(a[0], &x, &y);
    wr32(a[1], rd32(a[1]) - (uint32_t)x); wr32(a[1] + 4, rd32(a[1] + 4) - (uint32_t)y);
    return TRUE_;
}

/* The host window has no Win32 frame: client size == window size. */
HLE(AdjustWindowRect) { (void)a; return TRUE_; }
HLE(AdjustWindowRectEx) { (void)a; return TRUE_; }

HLE(SetRect) { wr32(a[0], a[1]); wr32(a[0] + 4, a[2]); wr32(a[0] + 8, a[3]); wr32(a[0] + 12, a[4]); return TRUE_; }

HLE(SetWindowPos) {   /* (hwnd, after, x, y, cx, cy, flags) */
    wnd_t *w = wnd_get(a[0]);
    if (!w) return FALSE_;
    uint32_t fl = a[6];
    int move = !(fl & 2), size = !(fl & 1);                           /* SWP_NOMOVE, SWP_NOSIZE */
    if (move) { w->x = (int)a[2]; w->y = (int)a[3]; }
    if (size) { w->w = (int)a[4]; w->h = (int)a[5]; }
    if (fl & 0x40) w->visible = 1;                                     /* SWP_SHOWWINDOW */
    if (fl & 0x80) w->visible = 0;                                     /* SWP_HIDEWINDOW */
    if (w->host) { plat_window_move(w->x, w->y, w->w, w->h, move, size); if (fl & 0xC0) plat_window_show(w->visible); }
    return TRUE_;
}

HLE(MoveWindow) {     /* (hwnd, x, y, w, h, repaint) */
    wnd_t *w = wnd_get(a[0]);
    if (!w) return FALSE_;
    w->x = (int)a[1]; w->y = (int)a[2]; w->w = (int)a[3]; w->h = (int)a[4];
    if (w->host) plat_window_move(w->x, w->y, w->w, w->h, 1, 1);
    return TRUE_;
}

/* The game always owns the foreground: pausing on focus loss is the host's business. */
HLE(GetForegroundWindow) { (void)a; return g_main; }
HLE(GetDesktopWindow) { (void)a; return HWND_DESKTOP_; }
HLE(SetForegroundWindow) { (void)a; return TRUE_; }
HLE(SetActiveWindow) { (void)a; return g_main; }
HLE(SetFocus) { (void)a; return g_main; }

HLE(BeginPaint) {     /* PAINTSTRUCT: hdc fErase rcPaint(16) fRestore fIncUpdate rgbReserved[32] */
    wnd_t *w = wnd_get(a[0]);
    int cw = 0, ch = 0;
    if (w) client_size(w, &cw, &ch);
    memset(G2H(a[1]), 0, 64);
    wr32(a[1], 0x00F00001u);
    wr32(a[1] + 16, (uint32_t)cw); wr32(a[1] + 20, (uint32_t)ch);
    return 0x00F00001u;
}
HLE(EndPaint) { (void)a; return TRUE_; }
