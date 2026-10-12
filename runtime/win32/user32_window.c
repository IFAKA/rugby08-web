/* user32_window.c -- window classes and windows. The host window (runtime/include/plat.h) is bound
 * to the D3D device window, or to the first top-level window shown; other windows (the game's
 * hidden "Caps Retrieval" probes) are bookkeeping only. The host window outlives the HWND it is
 * bound to (it is hidden, not destroyed), so the GL context D3D created on it survives. */
#include "user32_int.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

pthread_mutex_t g_user32_mu = PTHREAD_MUTEX_INITIALIZER;
uint32_t g_main;

/* ---- classes --------------------------------------------------------------------------------- */
static wclass_t g_cls[MAX_CLASSES];

wclass_t *class_find(uint32_t name_or_atom) {
    for (int i = 0; i < MAX_CLASSES; i++) {
        if (!g_cls[i].used) continue;
        if (name_or_atom < 0x10000 ? g_cls[i].atom == name_or_atom
                                   : !strcasecmp(g_cls[i].name, gstr(name_or_atom)))
            return &g_cls[i];
    }
    return NULL;
}

static uint32_t class_add(uint32_t style, uint32_t wndproc, uint32_t extra, uint32_t hinst, uint32_t name) {
    pthread_mutex_lock(&g_user32_mu);
    wclass_t *c = class_find(name);
    if (c) { pthread_mutex_unlock(&g_user32_mu); return set_err(1410); }   /* ERROR_CLASS_ALREADY_EXISTS */
    for (int i = 0; i < MAX_CLASSES; i++) {
        if (g_cls[i].used) continue;
        c = &g_cls[i];
        *c = (wclass_t){ 1, "", (uint16_t)(0xC000 + i), style, wndproc, extra, hinst };
        snprintf(c->name, sizeof c->name, "%s", name < 0x10000 ? "" : gstr(name));
        pthread_mutex_unlock(&g_user32_mu);
        rt_log("RegisterClass \"%s\" wndproc 0x%08X", c->name, wndproc);
        return c->atom;
    }
    pthread_mutex_unlock(&g_user32_mu);
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
    pthread_mutex_lock(&g_user32_mu);
    wclass_t *c = class_find(a[0]);
    if (c) c->used = 0;
    pthread_mutex_unlock(&g_user32_mu);
    return c ? TRUE_ : set_err(1411);                                  /* ERROR_CLASS_DOES_NOT_EXIST */
}

/* ---- windows --------------------------------------------------------------------------------- */
static wnd_t g_wnd[MAX_WINDOWS];

wnd_t *wnd_get(uint32_t hwnd) {
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

uint32_t user32_send(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    wnd_t *w = wnd_get(hwnd);
    if (!w || !w->wndproc) return 0;
    return guest_call(w->wndproc, 4, hwnd, msg, wp, lp);
}

HLE(CreateWindowExA) {   /* (exstyle, class, name, style, x, y, w, h, parent, menu, hinst, param) */
    pthread_mutex_lock(&g_user32_mu);
    wclass_t *c = class_find(a[1]);
    wclass_t cls = c ? *c : (wclass_t){ 0 };
    pthread_mutex_unlock(&g_user32_mu);
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
    uint32_t ok = user32_send(hwnd, WM_NCCREATE, 0, cs);
    uint32_t rc = ok ? user32_send(hwnd, WM_CREATE, 0, cs) : 0xFFFFFFFFu;
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
    user32_send(a[0], WM_DESTROY, 0, 0);
    user32_send(a[0], WM_NCDESTROY, 0, 0);
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
    if (w->visible) user32_send(a[0], WM_PAINT, 0, 0);
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
