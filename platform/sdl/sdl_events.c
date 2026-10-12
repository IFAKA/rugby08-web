/* sdl_events.c -- SDL events translated to the Win32 messages the game's WndProc expects. */
#include "sdl_int.h"
#include "ui.h"

#define WM_SIZE_        0x0005
#define WM_ACTIVATE_    0x0006
#define WM_PAINT_       0x000F
#define WM_CLOSE_       0x0010
#define WM_ACTIVATEAPP_ 0x001C
#define WM_KEYDOWN_     0x0100
#define WM_KEYUP_       0x0101
#define WM_CHAR_        0x0102
#define WM_SYSKEYDOWN_  0x0104
#define WM_SYSKEYUP_    0x0105
#define WM_MOUSEMOVE_   0x0200
#define WM_MOUSEWHEEL_  0x020A

static plat_msg_t g_pending[8];
static int g_npending;

static void push(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (g_npending < 8) g_pending[g_npending++] = (plat_msg_t){ msg, wp, lp };
}

static uint32_t mk_buttons(void) {
    uint32_t b = live_mouse(), mk = 0;
    if (b & SDL_BUTTON_LMASK) mk |= 0x01;
    if (b & SDL_BUTTON_RMASK) mk |= 0x02;
    if (b & SDL_BUTTON_MMASK) mk |= 0x10;
    if (g_replay_buttons & 1) mk |= 0x01; /* injected (replay, netplay) */
    if (g_replay_buttons & 2) mk |= 0x02;
    if (g_replay_buttons & 4) mk |= 0x10;
    if (plat_vk_down(0x10)) mk |= 0x04;
    if (plat_vk_down(0x11)) mk |= 0x08;
    return mk;
}

/* Interactive play captures the pointer (SDL relative mode) and tracks the in-window position from
   the deltas, so the pointer can never stall at a window edge while the game cursor still has room. */
void rel_set(int on) {
    /* netplay host: its real pointer is captured even under the virtual cursor */
    if (g_desk_applying || (virtual_cursor() && !g_desk_on) || !g_win || on == g_rel) return;
    if (on) {
        int x, y;
        SDL_GetMouseState(&x, &y);
        g_cx = x;
        g_cy = y;
    }
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_rel = on;
}
static void rel_move(int dx, int dy) {
    int w = 640, h = 480;
    if (g_win) SDL_GetWindowSize(g_win, &w, &h);
    g_cx += dx;
    g_cy += dy;
    if (g_cx < 0) g_cx = 0;
    else if (g_cx > w - 1) g_cx = w - 1;
    if (g_cy < 0) g_cy = 0;
    else if (g_cy > h - 1) g_cy = h - 1;
}

/* --log-input: the host events, as they arrive */
static void log_event(const SDL_Event *e) {
    if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP)
        rt_log("input: key %s %s (vk 0x%02X)", SDL_GetScancodeName(e->key.keysym.scancode),
               e->type == SDL_KEYDOWN ? "down" : "up", g_keys[e->key.keysym.scancode].vk);
    else if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP)
        rt_log("input: mouse button %d %s at %d,%d", e->button.button, e->type == SDL_MOUSEBUTTONDOWN ? "down" : "up",
               e->button.x, e->button.y);
    else if (e->type == SDL_WINDOWEVENT &&
             (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST))
        rt_log("input: window focus %s", e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? "gained" : "lost");
}

static void window_event(const SDL_WindowEvent *w) {
    switch (w->event) {
    case SDL_WINDOWEVENT_CLOSE: push(WM_CLOSE_, 0, 0); break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
        rel_set(1);
        push(WM_ACTIVATEAPP_, 1, 0);
        push(WM_ACTIVATE_, 1, 0);
        break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
        rel_set(0);
        push(WM_ACTIVATE_, 0, 0);
        push(WM_ACTIVATEAPP_, 0, 0);
        break;
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        push(WM_SIZE_, 0, ((uint32_t)w->data2 << 16) | ((uint32_t)w->data1 & 0xFFFF));
        break;
    case SDL_WINDOWEVENT_MINIMIZED: push(WM_SIZE_, 1, 0); break;
    case SDL_WINDOWEVENT_EXPOSED: push(WM_PAINT_, 0, 0); break;
    }
}

/* WM_KEY* / WM_SYSKEY* with the generic modifier codes; WM_CHAR comes from the game's TranslateMessage */
static void key_event(const SDL_KeyboardEvent *k8) {
    keymap_t k = g_keys[k8->keysym.scancode];
    if (!k.vk) return;
    int down = k8->type == SDL_KEYDOWN, alt = plat_vk_down(0x12) || k.vk == 0xA4 || k.vk == 0xA5;
    g_vk_down[k.vk] = (uint8_t)down;
    g_dik_down[(k.scan | (k.ext ? 0x80 : 0)) & 0xFF] = (uint8_t)down;
    uint32_t vk = k.vk;
    if (vk == 0xA0 || vk == 0xA1) vk = 0x10;
    else if (vk == 0xA2 || vk == 0xA3) vk = 0x11;
    else if (vk == 0xA4 || vk == 0xA5) vk = 0x12;
    uint32_t lp = 1u | ((uint32_t)k.scan << 16) | ((uint32_t)k.ext << 24) | (alt ? 1u << 29 : 0);
    if (!down) lp |= 3u << 30;
    else if (k8->repeat) lp |= 1u << 30;
    push(alt ? (down ? WM_SYSKEYDOWN_ : WM_SYSKEYUP_) : (down ? WM_KEYDOWN_ : WM_KEYUP_), vk, lp);
}

/* lParam of a mouse message: the captured in-window position, or the event's own */
static uint32_t mouse_lparam(int x, int y) {
    if (g_rel) return ((uint32_t)(g_cy & 0xFFFF) << 16) | (uint32_t)(g_cx & 0xFFFF);
    return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF);
}

static void motion_event(const SDL_MouseMotionEvent *m) {
    g_mdx += m->xrel;
    g_mdy += m->yrel;
    if (!g_rel && plat_window_focused()) rel_set(1);
    if (g_rel) rel_move(m->xrel, m->yrel);
    push(WM_MOUSEMOVE_, mk_buttons(), mouse_lparam(m->x, m->y));
}

static void button_event(const SDL_MouseButtonEvent *bt) {
    int b = bt->button, up = bt->type == SDL_MOUSEBUTTONUP;
    uint32_t base = b == SDL_BUTTON_LEFT ? 0x201 : b == SDL_BUTTON_RIGHT ? 0x204 : b == SDL_BUTTON_MIDDLE ? 0x207 : 0;
    uint8_t vk = b == SDL_BUTTON_LEFT ? 1 : b == SDL_BUTTON_RIGHT ? 2 : b == SDL_BUTTON_MIDDLE ? 4 : 0;
    if (!base) return;
    g_vk_down[vk] = (uint8_t)!up;
    push(base + (uint32_t)up, mk_buttons(), mouse_lparam(bt->x, bt->y));
}

void translate(const SDL_Event *e) {
    if (rt_log_input) log_event(e);
    switch (e->type) {
    case SDL_QUIT: push(WM_CLOSE_, 0, 0); break;
    case SDL_WINDOWEVENT: window_event(&e->window); break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {   /* the in-game Online screens take keys first (runtime/core/ui_online.c) */
        int down = e->type == SDL_KEYDOWN, sc = (int)e->key.keysym.scancode;
        if (down && e->key.repeat ? ui_open() : ui_key(sc, down)) break;
        key_event(&e->key);
        break;
    }
    case SDL_MOUSEMOTION: motion_event(&e->motion); break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: button_event(&e->button); break;
    case SDL_MOUSEWHEEL:
        g_mdw += e->wheel.y * 120;
        push(WM_MOUSEWHEEL_, ((uint32_t)(uint16_t)(int16_t)(e->wheel.y * 120) << 16) | mk_buttons(), 0);
        break;
    }
}

int plat_pending(void) { return g_npending > 0 || g_activate_pending; }

int plat_poll(plat_msg_t *m, int wait_ms) {
    if (!g_inited) return 0;
    SDL_Event e;
    if (g_activate_pending) {
        g_activate_pending = 0;
        push(WM_ACTIVATEAPP_, 1, 0);
        push(WM_ACTIVATE_, 1, 0);
    }
    if (det_on()) {   /* deterministic mode: the host window's own events never reach the guest; only
                         what the replay/netplay pushed at Present and the initial activation do */
        while (SDL_PollEvent(&e)) desk_record(&e);
        if (!g_npending) return 0;
        *m = g_pending[0];
        memmove(g_pending, g_pending + 1, (size_t)--g_npending * sizeof g_pending[0]);
        return 1;
    }
    while (!g_npending) {
        int got = wait_ms > 0 ? SDL_WaitEventTimeout(&e, wait_ms) : SDL_PollEvent(&e);
        if (!got) return 0;
        wait_ms = 0;
        if (virtual_cursor() &&
            (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT || e.type == SDL_MOUSEMOTION ||
             e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL))
            continue;                     /* automated runs: only the replay drives input, never the user's devices */
        translate(&e);
    }
    *m = g_pending[0];
    memmove(g_pending, g_pending + 1, (size_t)--g_npending * sizeof g_pending[0]);
    return 1;
}
