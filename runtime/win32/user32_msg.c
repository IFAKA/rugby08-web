/* user32_msg.c -- the message queue and WndProc dispatch. Messages from the host arrive while the
 * game pumps (PeekMessage/GetMessage), as on Windows. */
#include "user32_int.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_ms(void) {
    if (det_on()) return (uint32_t)(det_now_ns() / 1000000ull);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ---- message queue ---------------------------------------------------------------------------- */
typedef struct {
    uint32_t hwnd, msg, wp, lp, time;
} qmsg_t;
#define QCAP 512
static qmsg_t g_q[QCAP];
static int g_qn;

void user32_post(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    pthread_mutex_lock(&g_user32_mu);
    if (g_qn < QCAP) g_q[g_qn++] = (qmsg_t){ hwnd, msg, wp, lp, now_ms() };
    else rt_log("message queue full, dropped 0x%04X", msg);
    pthread_mutex_unlock(&g_user32_mu);
}

/* Host events: state changes are sent (as Windows sends them inside the pump), input is posted. */
static void pump(int wait_ms) {
    plat_msg_t m;
    while (plat_poll(&m, wait_ms)) {
        wait_ms = 0;
        if (!g_main) continue;
        if (m.msg == WM_SIZE || m.msg == WM_ACTIVATE || m.msg == WM_ACTIVATEAPP)
            user32_send(g_main, m.msg, m.wparam, m.lparam);
        else user32_post(g_main, m.msg, m.wparam, m.lparam);
    }
}

static int match(const qmsg_t *q, uint32_t hwnd, uint32_t lo, uint32_t hi) {
    if (hwnd && hwnd != 0xFFFFFFFFu && q->hwnd != hwnd) return 0;
    if (hwnd == 0xFFFFFFFFu && q->hwnd) return 0;
    if ((lo || hi) && (q->msg < lo || q->msg > hi)) return 0;
    return 1;
}

static int take(uint32_t out, uint32_t hwnd, uint32_t lo, uint32_t hi, int remove) {
    pthread_mutex_lock(&g_user32_mu);
    for (int i = 0; i < g_qn; i++) {
        if (!match(&g_q[i], hwnd, lo, hi)) continue;
        qmsg_t q = g_q[i];
        if (remove) memmove(&g_q[i], &g_q[i + 1], (size_t)(g_qn - i - 1) * sizeof q), g_qn--;
        pthread_mutex_unlock(&g_user32_mu);
        int x, y;                     /* MSG: hwnd message wParam lParam time pt.x pt.y */
        plat_cursor_pos(&x, &y);
        uint32_t f[7] = { q.hwnd, q.msg, q.wp, q.lp, q.time, (uint32_t)x, (uint32_t)y };
        for (int k = 0; k < 7; k++) wr32(out + 4u * (uint32_t)k, f[k]);
        return 1;
    }
    pthread_mutex_unlock(&g_user32_mu);
    return 0;
}

HLE(PeekMessageA) {   /* (msg, hwnd, min, max, remove) */
    static int first = 1;
    if (first) {
        first = 0;
        rt_trace("message pump: first PeekMessageA");
    }
    if (take(a[0], a[1], a[2], a[3], a[4] & 1)) return TRUE_;
    pump(0);
    return take(a[0], a[1], a[2], a[3], a[4] & 1) ? TRUE_ : FALSE_;
}

static int has_msg(void *p) {   /* det_wait readiness for GetMessageA(hwnd, lo, hi) */
    const uint32_t *f = p;
    pthread_mutex_lock(&g_user32_mu);
    int any = 0;
    for (int i = 0; i < g_qn && !any; i++) any = match(&g_q[i], f[0], f[1], f[2]);
    pthread_mutex_unlock(&g_user32_mu);
    return any || plat_pending();
}

HLE(GetMessageA) {    /* (msg, hwnd, min, max): 0 on WM_QUIT */
    static int first = 1;
    if (first) {
        first = 0;
        rt_trace("message pump: first GetMessageA");
    }
    if (det_on() && det_thread_id() >= 0) {   /* messages come only from guest threads and Present (det.c) */
        uint32_t f[3] = { a[1], a[2], a[3] };
        while (!take(a[0], a[1], a[2], a[3], 1)) {
            pump(0);
            if (!take(a[0], a[1], a[2], a[3], 1)) det_wait(has_msg, f, UINT64_MAX);
            else break;
        }
        return rd32(a[0] + 4) == WM_QUIT ? 0 : TRUE_;
    }
    while (!take(a[0], a[1], a[2], a[3], 1)) pump(10);
    return rd32(a[0] + 4) == WM_QUIT ? 0 : TRUE_;
}

/* As Windows does: a WM_KEYDOWN / WM_SYSKEYDOWN of a character key posts WM_CHAR / WM_SYSCHAR. The host
 * layer no longer turns text input into WM_CHAR (macOS text input showed its accent popup on key hold). */
HLE(TranslateMessage) {
    uint32_t m = a[0], msg = rd32(m + 4), wp = rd32(m + 8), lp = rd32(m + 12);
    if (msg != 0x0100 && msg != 0x0104) return FALSE_;
    uint32_t c = vk_char(wp, plat_vk_down(0x10));
    if (!c) return FALSE_;
    user32_post(rd32(m), msg == 0x0100 ? 0x0102u : 0x0106u, c, lp);
    return TRUE_;
}

HLE(DispatchMessageA) {
    uint32_t m = a[0];
    static int first = 1;
    if (first && rd32(m)) {
        first = 0;
        rt_trace("message pump: first DispatchMessageA msg 0x%04X", rd32(m + 4));
    }
    return user32_send(rd32(m), rd32(m + 4), rd32(m + 8), rd32(m + 12));
}

HLE(PostMessageA) {
    user32_post(a[0], a[1], a[2], a[3]);
    return TRUE_;
}
HLE(PostQuitMessage) {
    user32_post(0, WM_QUIT, a[0], 0);
    return 0;
}
HLE(SendMessageA) { return user32_send(a[0], a[1], a[2], a[3]); }
HLE(SendNotifyMessageA) {
    user32_send(a[0], a[1], a[2], a[3]);
    return TRUE_;
}

HLE(DefWindowProcA) {  /* (hwnd, msg, wp, lp) */
    switch (a[1]) {
    case WM_NCCREATE: return TRUE_;
    case WM_CLOSE: {
        uint32_t h = a[0];
        return hle_DestroyWindow(&h);
    }
    case WM_ERASEBKGND: return TRUE_;
    }
    return 0;
}

HLE(MessageBoxA) {     /* (hwnd, text, caption, type) */
    rt_trace("MessageBoxA \"%s\": %s", gstr(a[2]) ? gstr(a[2]) : "", gstr(a[1]) ? gstr(a[1]) : "");
    switch (a[3] & 0xF) {
    case 3:
    case 4: return 6;         /* MB_YESNO(CANCEL) -> IDYES */
    case 2: return 3;                 /* MB_ABORTRETRYIGNORE -> IDABORT */
    case 5: return 2;                 /* MB_RETRYCANCEL -> IDCANCEL */
    }
    return 1;                         /* IDOK */
}
