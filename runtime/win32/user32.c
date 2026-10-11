/* user32.c -- window classes, windows, the message queue and WndProc dispatch, keyboard/cursor
 * state, metrics. The host window (runtime/include/plat.h) is bound to the D3D device window, or
 * to the first top-level window shown; other windows (the game's hidden "Caps Retrieval" probes)
 * are bookkeeping only. The host window outlives the HWND it is bound to (it is hidden, not
 * destroyed), so the GL context D3D created on it survives. Messages from the host arrive while
 * the game pumps (PeekMessage/GetMessage), as on Windows. */
#include "win32.h"
#include "plat.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_ACTIVATEAPP 0x001C
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082

#define HWND_DESKTOP_ 0x00010010u
#define HWND_BASE 0x00010020u
#define MAX_WINDOWS 64
#define MAX_CLASSES 32

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ---- classes --------------------------------------------------------------------------------- */
typedef struct { int used; char name[64]; uint16_t atom; uint32_t style, wndproc, extra, hinst; } wclass_t;
static wclass_t g_cls[MAX_CLASSES];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static wclass_t *class_find(uint32_t name_or_atom) {
    for (int i = 0; i < MAX_CLASSES; i++) {
        if (!g_cls[i].used) continue;
        if (name_or_atom < 0x10000 ? g_cls[i].atom == name_or_atom
                                   : !strcasecmp(g_cls[i].name, gstr(name_or_atom)))
            return &g_cls[i];
    }
    return NULL;
}

static uint32_t class_add(uint32_t style, uint32_t wndproc, uint32_t extra, uint32_t hinst, uint32_t name) {
    pthread_mutex_lock(&g_mu);
    wclass_t *c = class_find(name);
    if (c) { pthread_mutex_unlock(&g_mu); return set_err(1410); }   /* ERROR_CLASS_ALREADY_EXISTS */
    for (int i = 0; i < MAX_CLASSES; i++) {
        if (g_cls[i].used) continue;
        c = &g_cls[i];
        *c = (wclass_t){ 1, "", (uint16_t)(0xC000 + i), style, wndproc, extra, hinst };
        snprintf(c->name, sizeof c->name, "%s", name < 0x10000 ? "" : gstr(name));
        pthread_mutex_unlock(&g_mu);
        rt_log("RegisterClass \"%s\" wndproc 0x%08X", c->name, wndproc);
        return c->atom;
    }
    pthread_mutex_unlock(&g_mu);
    return set_err(ERROR_NOT_ENOUGH_MEMORY_);
}

HLE(RegisterClassExA) {   /* WNDCLASSEXA: cbSize style wndproc clsExtra wndExtra hInst ... name@40 */
    uint32_t p = a[0];
    return class_add(rd32(p + 4), rd32(p + 8), rd32(p + 16), rd32(p + 20), rd32(p + 40));
}

HLE(RegisterClassA) {     /* WNDCLASSA: style wndproc clsExtra wndExtra hInst ... name@36 */
    uint32_t p = a[0];
    return class_add(rd32(p + 0), rd32(p + 4), rd32(p + 12), rd32(p + 16), rd32(p + 36));
}

HLE(UnregisterClassA) {
    pthread_mutex_lock(&g_mu);
    wclass_t *c = class_find(a[0]);
    if (c) c->used = 0;
    pthread_mutex_unlock(&g_mu);
    return c ? TRUE_ : set_err(1411);                                  /* ERROR_CLASS_DOES_NOT_EXIST */
}

/* ---- windows --------------------------------------------------------------------------------- */
typedef struct {
    int used, visible, host;          /* host: backed by the platform window */
    char *title;
    uint32_t wndproc, style, exstyle, hinst, parent, id, userdata;
    int x, y, w, h;
    uint8_t *extra; uint32_t nextra;
} wnd_t;
static wnd_t g_wnd[MAX_WINDOWS];
static uint32_t g_main;               /* hwnd of the host-backed window */

static wnd_t *wnd_get(uint32_t hwnd) {
    uint32_t i = (hwnd - HWND_BASE) / 16;
    if (hwnd < HWND_BASE || (hwnd - HWND_BASE) % 16 || i >= MAX_WINDOWS || !g_wnd[i].used) return NULL;
    return &g_wnd[i];
}

/* Window titles are ANSI (code page 1252); the host wants UTF-8 (macOS rejects invalid UTF-8). */
static char *utf8_title(const char *s) {
    static const uint16_t hi[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
        0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    if (!s) s = "";
    char *out = malloc(strlen(s) * 3 + 1), *o = out;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        uint32_t c = *p >= 0x80 && *p < 0xA0 ? hi[*p - 0x80] : *p;
        if (c < 0x80) *o++ = (char)c;
        else if (c < 0x800) { *o++ = (char)(0xC0 | c >> 6); *o++ = (char)(0x80 | (c & 0x3F)); }
        else { *o++ = (char)(0xE0 | c >> 12); *o++ = (char)(0x80 | (c >> 6 & 0x3F)); *o++ = (char)(0x80 | (c & 0x3F)); }
    }
    *o = 0;
    return out;
}

static void unbind(wnd_t *w) {
    w->host = 0;
    plat_window_show(0);
    g_main = 0;
}

/* Back hwnd with the host window (created on first use, then reused: size, title, visibility). */
int user32_bind_host(uint32_t hwnd) {
    wnd_t *w = wnd_get(hwnd);
    if (!w) return -1;
    if (g_main == hwnd) return 0;
    wnd_t *old = wnd_get(g_main);
    if (old) old->host = 0;
    if (!plat_window_is_open()) {
        if (plat_window_open(w->title, w->x, w->y, w->w, w->h) != 0) return -1;
    } else {
        plat_window_title(w->title);
        plat_window_move(w->x, w->y, w->w, w->h, 0, 1);
    }
    w->host = 1;
    g_main = hwnd;
    plat_window_show(1);
    rt_log("user32: host window bound to 0x%08X \"%s\"", hwnd, w->title ? w->title : "");
    return 0;
}

static uint32_t send(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    wnd_t *w = wnd_get(hwnd);
    if (!w || !w->wndproc) return 0;
    return guest_call(w->wndproc, 4, hwnd, msg, wp, lp);
}

HLE(CreateWindowExA) {   /* (exstyle, class, name, style, x, y, w, h, parent, menu, hinst, param) */
    pthread_mutex_lock(&g_mu);
    wclass_t *c = class_find(a[1]);
    wclass_t cls = c ? *c : (wclass_t){ 0 };
    pthread_mutex_unlock(&g_mu);
    if (!c) { rt_log("CreateWindowExA: unknown class"); return set_err(1407); }   /* ERROR_CANNOT_FIND_WND_CLASS */
    int i = 0;
    while (i < MAX_WINDOWS && g_wnd[i].used) i++;
    if (i == MAX_WINDOWS) return set_err(ERROR_NOT_ENOUGH_MEMORY_);
    uint32_t hwnd = HWND_BASE + 16u * (uint32_t)i;
    int cx = (int)a[6], cy = (int)a[7];
    if (a[6] == 0x80000000u) { cx = 800; cy = 600; }                 /* CW_USEDEFAULT */
    wnd_t *w = &g_wnd[i];
    *w = (wnd_t){ .used = 1, .wndproc = cls.wndproc, .style = a[3], .exstyle = a[0], .hinst = a[10],
                  .parent = a[8], .id = a[9], .x = a[4] == 0x80000000u ? 0 : (int)a[4], .y = (int)a[5],
                  .w = cx, .h = cy, .nextra = cls.extra };
    w->extra = cls.extra ? calloc(1, cls.extra) : NULL;
    const char *title = gstr(a[2]);
    w->title = utf8_title(title);
    rt_trace("CreateWindowExA \"%s\" class \"%s\" %dx%d style 0x%08X -> 0x%08X", title ? title : "",
             cls.name, cx, cy, a[3], hwnd);
    /* CREATESTRUCTA: param hinst menu parent cy cx y x style name class exstyle */
    uint32_t cs = gcalloc(48);
    uint32_t f[12] = { a[11], a[10], a[9], a[8], (uint32_t)cy, (uint32_t)cx, a[5], a[4], a[3], a[2], a[1], a[0] };
    for (int k = 0; k < 12; k++) wr32(cs + 4u * (uint32_t)k, f[k]);
    uint32_t ok = send(hwnd, WM_NCCREATE, 0, cs);
    uint32_t rc = ok ? send(hwnd, WM_CREATE, 0, cs) : 0xFFFFFFFFu;
    gfree(cs);
    if (!ok || rc == 0xFFFFFFFFu) {
        rt_log("CreateWindowExA: WM_%sCREATE refused", ok ? "" : "NC");
        if (w->host) unbind(w);
        w->used = 0; free(w->extra); free(w->title);
        return 0;
    }
    if ((a[3] & 0x10000000u) && !w->parent) { w->visible = 1; user32_bind_host(hwnd); }   /* WS_VISIBLE */
    return hwnd;
}

HLE(DestroyWindow) {
    wnd_t *w = wnd_get(a[0]);
    if (!w) return set_err(1400);                                      /* ERROR_INVALID_WINDOW_HANDLE */
    send(a[0], WM_DESTROY, 0, 0);
    send(a[0], WM_NCDESTROY, 0, 0);
    if (w->host) unbind(w);
    free(w->extra); free(w->title);
    w->used = 0;
    return TRUE_;
}

HLE(ShowWindow) {
    wnd_t *w = wnd_get(a[0]);
    if (!w) return FALSE_;
    uint32_t was = (uint32_t)w->visible;
    w->visible = a[1] != 0;                                            /* SW_HIDE = 0 */
    if (w->visible && !w->parent && !w->host && !g_main) user32_bind_host(a[0]);
    else if (w->host) plat_window_show(w->visible);
    return was;
}

HLE(UpdateWindow) {
    wnd_t *w = wnd_get(a[0]);
    if (!w) return FALSE_;
    if (w->visible) send(a[0], WM_PAINT, 0, 0);
    return TRUE_;
}

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

HLE(SetWindowTextA) {
    wnd_t *w = wnd_get(a[0]);
    if (!w) return FALSE_;
    free(w->title);
    w->title = utf8_title(gstr(a[1]));
    if (w->host) plat_window_title(w->title);
    return TRUE_;
}
HLE(IsIconic) { wnd_t *w = wnd_get(a[0]); return w && w->host && plat_window_minimized() ? TRUE_ : FALSE_; }

HLE(GetWindowLongA) {
    wnd_t *w = wnd_get(a[0]);
    if (!w) return set_err(1400);
    switch ((int32_t)a[1]) {
    case -4:  return w->wndproc;      /* GWL_WNDPROC */
    case -6:  return w->hinst;        /* GWL_HINSTANCE */
    case -8:  return w->parent;       /* GWL_HWNDPARENT */
    case -12: return w->id;           /* GWL_ID */
    case -16: return w->style;        /* GWL_STYLE */
    case -20: return w->exstyle;      /* GWL_EXSTYLE */
    case -21: return w->userdata;     /* GWL_USERDATA */
    }
    if (a[1] + 4 <= w->nextra) { uint32_t v; memcpy(&v, w->extra + a[1], 4); return v; }
    return set_err(1413);                                              /* ERROR_INVALID_INDEX */
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

/* ---- message queue ---------------------------------------------------------------------------- */
typedef struct { uint32_t hwnd, msg, wp, lp, time; } qmsg_t;
#define QCAP 512
static qmsg_t g_q[QCAP];
static int g_qn;

static void post(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    pthread_mutex_lock(&g_mu);
    if (g_qn < QCAP) g_q[g_qn++] = (qmsg_t){ hwnd, msg, wp, lp, now_ms() };
    else rt_log("message queue full, dropped 0x%04X", msg);
    pthread_mutex_unlock(&g_mu);
}

/* Host events: state changes are sent (as Windows sends them inside the pump), input is posted. */
static void pump(int wait_ms) {
    plat_msg_t m;
    while (plat_poll(&m, wait_ms)) {
        wait_ms = 0;
        if (!g_main) continue;
        if (m.msg == WM_SIZE || m.msg == WM_ACTIVATE || m.msg == WM_ACTIVATEAPP)
            send(g_main, m.msg, m.wparam, m.lparam);
        else
            post(g_main, m.msg, m.wparam, m.lparam);
    }
}

static int match(const qmsg_t *q, uint32_t hwnd, uint32_t lo, uint32_t hi) {
    if (hwnd && hwnd != 0xFFFFFFFFu && q->hwnd != hwnd) return 0;
    if (hwnd == 0xFFFFFFFFu && q->hwnd) return 0;
    if ((lo || hi) && (q->msg < lo || q->msg > hi)) return 0;
    return 1;
}

static int take(uint32_t out, uint32_t hwnd, uint32_t lo, uint32_t hi, int remove) {
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_qn; i++) {
        if (!match(&g_q[i], hwnd, lo, hi)) continue;
        qmsg_t q = g_q[i];
        if (remove) memmove(&g_q[i], &g_q[i + 1], (size_t)(g_qn - i - 1) * sizeof q), g_qn--;
        pthread_mutex_unlock(&g_mu);
        int x, y;                     /* MSG: hwnd message wParam lParam time pt.x pt.y */
        plat_cursor_pos(&x, &y);
        uint32_t f[7] = { q.hwnd, q.msg, q.wp, q.lp, q.time, (uint32_t)x, (uint32_t)y };
        for (int k = 0; k < 7; k++) wr32(out + 4u * (uint32_t)k, f[k]);
        return 1;
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
}

HLE(PeekMessageA) {   /* (msg, hwnd, min, max, remove) */
    static int first = 1;
    if (first) { first = 0; rt_trace("message pump: first PeekMessageA"); }
    if (take(a[0], a[1], a[2], a[3], a[4] & 1)) return TRUE_;
    pump(0);
    return take(a[0], a[1], a[2], a[3], a[4] & 1) ? TRUE_ : FALSE_;
}

HLE(GetMessageA) {    /* (msg, hwnd, min, max): 0 on WM_QUIT */
    static int first = 1;
    if (first) { first = 0; rt_trace("message pump: first GetMessageA"); }
    while (!take(a[0], a[1], a[2], a[3], 1)) pump(10);
    return rd32(a[0] + 4) == WM_QUIT ? 0 : TRUE_;
}

static uint32_t vk_char(uint32_t vk, int shift);
/* As Windows does: a WM_KEYDOWN / WM_SYSKEYDOWN of a character key posts WM_CHAR / WM_SYSCHAR. The host
 * layer no longer turns text input into WM_CHAR (macOS text input showed its accent popup on key hold). */
HLE(TranslateMessage) {
    uint32_t m = a[0], msg = rd32(m + 4), wp = rd32(m + 8), lp = rd32(m + 12);
    if (msg != 0x0100 && msg != 0x0104) return FALSE_;
    uint32_t c = vk_char(wp, plat_vk_down(0x10));
    if (!c) return FALSE_;
    post(rd32(m), msg == 0x0100 ? 0x0102u : 0x0106u, c, lp);
    return TRUE_;
}

HLE(DispatchMessageA) {
    uint32_t m = a[0];
    static int first = 1;
    if (first && rd32(m)) { first = 0; rt_trace("message pump: first DispatchMessageA msg 0x%04X", rd32(m + 4)); }
    return send(rd32(m), rd32(m + 4), rd32(m + 8), rd32(m + 12));
}

HLE(PostMessageA) { post(a[0], a[1], a[2], a[3]); return TRUE_; }
HLE(PostQuitMessage) { post(0, WM_QUIT, a[0], 0); return 0; }
HLE(SendMessageA) { return send(a[0], a[1], a[2], a[3]); }
HLE(SendNotifyMessageA) { send(a[0], a[1], a[2], a[3]); return TRUE_; }

HLE(DefWindowProcA) {  /* (hwnd, msg, wp, lp) */
    switch (a[1]) {
    case WM_NCCREATE: return TRUE_;
    case WM_CLOSE: { uint32_t h = a[0]; return hle_DestroyWindow(&h); }
    case WM_ERASEBKGND: return TRUE_;
    }
    return 0;
}

HLE(MessageBoxA) {     /* (hwnd, text, caption, type) */
    rt_trace("MessageBoxA \"%s\": %s", gstr(a[2]) ? gstr(a[2]) : "", gstr(a[1]) ? gstr(a[1]) : "");
    switch (a[3] & 0xF) {
    case 3: case 4: return 6;         /* MB_YESNO(CANCEL) -> IDYES */
    case 2: return 3;                 /* MB_ABORTRETRYIGNORE -> IDABORT */
    case 5: return 2;                 /* MB_RETRYCANCEL -> IDCANCEL */
    }
    return 1;                         /* IDOK */
}

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

static uint32_t vk_char(uint32_t vk, int shift) {
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
