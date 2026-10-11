/* sdl_plat.c -- runtime/include/plat.h on SDL2: the game window, SDL events translated to the
 * Win32 messages the game's WndProc expects, and keyboard/cursor state. Shared by native and web. */
#include "plat.h"
#include "rt.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- keys: SDL scancode -> Win32 virtual key + set-1 scan code -------------------------------- */
typedef struct { uint8_t vk, scan, ext; } keymap_t;
static keymap_t g_keys[SDL_NUM_SCANCODES];
static uint8_t g_vk_down[256], g_dik_down[256];
static int g_mdx, g_mdy, g_mdw;
static uint8_t g_replay_buttons;      /* mouse buttons held by the input replay (ORed into DirectInput) */
static int g_inited;                  /* SDL video initialised (set in video_init) */

uint8_t plat_dik_down(uint32_t dik) { return dik < 256 ? g_dik_down[dik] : 0; }
void plat_mouse_delta(int *dx, int *dy, int *dw) { *dx = g_mdx; *dy = g_mdy; *dw = g_mdw; g_mdx = g_mdy = g_mdw = 0; }
uint8_t plat_mouse_buttons(void) {
    uint32_t b = g_inited ? SDL_GetMouseState(NULL, NULL) : 0;
    return (uint8_t)((b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0) | (b & SDL_BUTTON_MMASK ? 4 : 0) |
                     g_replay_buttons);
}

static void keymap_init(void) {
    static const char row1[] = "QWERTYUIOP", row2[] = "ASDFGHJKL", row3[] = "ZXCVBNM";
    for (int i = 0; i < 26; i++) g_keys[SDL_SCANCODE_A + i].vk = (uint8_t)('A' + i);
    for (int i = 0; row1[i]; i++) g_keys[SDL_SCANCODE_A + row1[i] - 'A'].scan = (uint8_t)(0x10 + i);
    for (int i = 0; row2[i]; i++) g_keys[SDL_SCANCODE_A + row2[i] - 'A'].scan = (uint8_t)(0x1E + i);
    for (int i = 0; row3[i]; i++) g_keys[SDL_SCANCODE_A + row3[i] - 'A'].scan = (uint8_t)(0x2C + i);
    for (int i = 0; i < 9; i++) g_keys[SDL_SCANCODE_1 + i] = (keymap_t){ (uint8_t)('1' + i), (uint8_t)(2 + i), 0 };
    g_keys[SDL_SCANCODE_0] = (keymap_t){ '0', 0x0B, 0 };
    for (int i = 0; i < 10; i++) g_keys[SDL_SCANCODE_F1 + i] = (keymap_t){ (uint8_t)(0x70 + i), (uint8_t)(0x3B + i), 0 };
    g_keys[SDL_SCANCODE_F11] = (keymap_t){ 0x7A, 0x57, 0 };
    g_keys[SDL_SCANCODE_F12] = (keymap_t){ 0x7B, 0x58, 0 };
    static const struct { int sc; keymap_t k; } t[] = {
        { SDL_SCANCODE_ESCAPE, { 0x1B, 0x01, 0 } }, { SDL_SCANCODE_MINUS, { 0xBD, 0x0C, 0 } },
        { SDL_SCANCODE_EQUALS, { 0xBB, 0x0D, 0 } }, { SDL_SCANCODE_BACKSPACE, { 0x08, 0x0E, 0 } },
        { SDL_SCANCODE_TAB, { 0x09, 0x0F, 0 } }, { SDL_SCANCODE_LEFTBRACKET, { 0xDB, 0x1A, 0 } },
        { SDL_SCANCODE_RIGHTBRACKET, { 0xDD, 0x1B, 0 } }, { SDL_SCANCODE_RETURN, { 0x0D, 0x1C, 0 } },
        { SDL_SCANCODE_LCTRL, { 0xA2, 0x1D, 0 } }, { SDL_SCANCODE_SEMICOLON, { 0xBA, 0x27, 0 } },
        { SDL_SCANCODE_APOSTROPHE, { 0xDE, 0x28, 0 } }, { SDL_SCANCODE_GRAVE, { 0xC0, 0x29, 0 } },
        { SDL_SCANCODE_LSHIFT, { 0xA0, 0x2A, 0 } }, { SDL_SCANCODE_BACKSLASH, { 0xDC, 0x2B, 0 } },
        { SDL_SCANCODE_COMMA, { 0xBC, 0x33, 0 } }, { SDL_SCANCODE_PERIOD, { 0xBE, 0x34, 0 } },
        { SDL_SCANCODE_SLASH, { 0xBF, 0x35, 0 } }, { SDL_SCANCODE_RSHIFT, { 0xA1, 0x36, 0 } },
        { SDL_SCANCODE_KP_MULTIPLY, { 0x6A, 0x37, 0 } }, { SDL_SCANCODE_LALT, { 0xA4, 0x38, 0 } },
        { SDL_SCANCODE_SPACE, { 0x20, 0x39, 0 } }, { SDL_SCANCODE_CAPSLOCK, { 0x14, 0x3A, 0 } },
        { SDL_SCANCODE_NUMLOCKCLEAR, { 0x90, 0x45, 0 } }, { SDL_SCANCODE_SCROLLLOCK, { 0x91, 0x46, 0 } },
        { SDL_SCANCODE_KP_7, { 0x67, 0x47, 0 } }, { SDL_SCANCODE_KP_8, { 0x68, 0x48, 0 } },
        { SDL_SCANCODE_KP_9, { 0x69, 0x49, 0 } }, { SDL_SCANCODE_KP_MINUS, { 0x6D, 0x4A, 0 } },
        { SDL_SCANCODE_KP_4, { 0x64, 0x4B, 0 } }, { SDL_SCANCODE_KP_5, { 0x65, 0x4C, 0 } },
        { SDL_SCANCODE_KP_6, { 0x66, 0x4D, 0 } }, { SDL_SCANCODE_KP_PLUS, { 0x6B, 0x4E, 0 } },
        { SDL_SCANCODE_KP_1, { 0x61, 0x4F, 0 } }, { SDL_SCANCODE_KP_2, { 0x62, 0x50, 0 } },
        { SDL_SCANCODE_KP_3, { 0x63, 0x51, 0 } }, { SDL_SCANCODE_KP_0, { 0x60, 0x52, 0 } },
        { SDL_SCANCODE_KP_PERIOD, { 0x6E, 0x53, 0 } }, { SDL_SCANCODE_KP_ENTER, { 0x0D, 0x1C, 1 } },
        { SDL_SCANCODE_RCTRL, { 0xA3, 0x1D, 1 } }, { SDL_SCANCODE_KP_DIVIDE, { 0x6F, 0x35, 1 } },
        { SDL_SCANCODE_RALT, { 0xA5, 0x38, 1 } }, { SDL_SCANCODE_PAUSE, { 0x13, 0x45, 0 } },
        { SDL_SCANCODE_HOME, { 0x24, 0x47, 1 } }, { SDL_SCANCODE_UP, { 0x26, 0x48, 1 } },
        { SDL_SCANCODE_PAGEUP, { 0x21, 0x49, 1 } }, { SDL_SCANCODE_LEFT, { 0x25, 0x4B, 1 } },
        { SDL_SCANCODE_RIGHT, { 0x27, 0x4D, 1 } }, { SDL_SCANCODE_END, { 0x23, 0x4F, 1 } },
        { SDL_SCANCODE_DOWN, { 0x28, 0x50, 1 } }, { SDL_SCANCODE_PAGEDOWN, { 0x22, 0x51, 1 } },
        { SDL_SCANCODE_INSERT, { 0x2D, 0x52, 1 } }, { SDL_SCANCODE_DELETE, { 0x2E, 0x53, 1 } },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) g_keys[t[i].sc] = t[i].k;
}

uint32_t plat_vk_to_scancode(uint32_t vk) {
    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
        if (g_keys[i].vk == vk && g_keys[i].scan) return g_keys[i].scan;
    if (vk == 0x10) return 0x2A;          /* VK_SHIFT/CONTROL/MENU -> left key */
    if (vk == 0x11) return 0x1D;
    if (vk == 0x12) return 0x38;
    return 0;
}

uint8_t plat_vk_toggled(uint32_t vk) {
    SDL_Keymod m = SDL_GetModState();
    return vk == 0x14 ? (m & KMOD_CAPS) != 0 : vk == 0x90 ? (m & KMOD_NUM) != 0 : 0;
}

uint8_t plat_vk_down(uint32_t vk) {
    if (vk >= 256) return 0;
    switch (vk) {                         /* generic modifiers: either side */
    case 0x10: return g_vk_down[0xA0] | g_vk_down[0xA1];
    case 0x11: return g_vk_down[0xA2] | g_vk_down[0xA3];
    case 0x12: return g_vk_down[0xA4] | g_vk_down[0xA5];
    }
    return g_vk_down[vk];
}

/* ---- cursor --------------------------------------------------------------------------------- */
/* Automated runs (--frames N or --replay) never move the user's real cursor: the game calls
   SetCursorPos every frame, which pinned the desktop pointer during checks. They keep a virtual
   cursor in screen coordinates instead; replay clicks set it. */
static int g_vcur_x, g_vcur_y;
static int g_rel, g_cx, g_cy;   /* captured-pointer mode and in-window position */
static int virtual_cursor(void) { return rt_frame_limit > 0 || rt_replay_path[0]; }

/* ---- window --------------------------------------------------------------------------------- */
static SDL_Window *g_win;
static int g_inited;

static void video_init(void) {
    if (g_inited) return;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) rt_fatal("SDL_Init: %s", SDL_GetError());
    keymap_init();
    g_inited = 1;
}

static int g_activate_pending;   /* plat_window_show in an automated run: WM_ACTIVATE* at the next pump */

int plat_window_open(const char *title, int x, int y, int w, int h) {
    video_init();
    if (g_win) return 0;
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
    (void)x; (void)y;     /* the host decides placement */
    int flags = SDL_WINDOW_OPENGL;
    if (virtual_cursor()) flags |= SDL_WINDOW_HIDDEN;   /* hide in automated tests; show for interactive play */
    g_win = SDL_CreateWindow(title ? title : "Rugby 08", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w, h, flags);
    if (!g_win) { rt_log("SDL_CreateWindow: %s", SDL_GetError()); return -1; }
    SDL_StopTextInput();   /* no macOS text input: holding a key opened the accent popup (WM_CHAR comes from TranslateMessage) */
    rt_log("plat: window %dx%d \"%s\"", w, h, title ? title : "");
    return 0;
}

void plat_window_close(void) { if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; } }
int  plat_window_is_open(void) { return g_win != NULL; }
void *plat_window_handle(void) { return g_win; }
void plat_window_show(int show) {
    if (!g_win) return;
    if (show && virtual_cursor()) {         /* automated runs stay hidden even when the game calls ShowWindow, */
        g_activate_pending = 1;             /* but are active as a shown window is on Windows: a hidden window */
        return;                             /* never gains focus, and the game ignored device input until it did */
    }
    if (show) {
        SDL_ShowWindow(g_win);
    } else {
        SDL_HideWindow(g_win);
    }
}
void plat_window_title(const char *t) { if (g_win) SDL_SetWindowTitle(g_win, t ? t : ""); }
int  plat_window_minimized(void) { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_MINIMIZED); }
int  plat_window_focused(void) { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS); }

void plat_window_move(int x, int y, int w, int h, int move, int size) {
    if (!g_win) return;
    if (size && w > 0 && h > 0) SDL_SetWindowSize(g_win, w, h);
    if (move) SDL_SetWindowPosition(g_win, x, y);
}

void plat_window_rect(int *x, int *y, int *w, int *h) {
    int px = 0, py = 0, pw = 800, ph = 600;
    if (g_win) { SDL_GetWindowPosition(g_win, &px, &py); SDL_GetWindowSize(g_win, &pw, &ph); }
    if (x) *x = px;
    if (y) *y = py;
    if (w) *w = pw;
    if (h) *h = ph;
}

void plat_desktop_size(int *w, int *h) {
    video_init();
    SDL_DisplayMode m;
    if (SDL_GetDesktopDisplayMode(0, &m) != 0) { m.w = 1024; m.h = 768; }
    *w = m.w; *h = m.h;
}

/* ---- GL ----------------------------------------------------------------------------------- */
static SDL_GLContext g_gl;

int plat_gl_create(void) {
    if (g_gl) return 0;
    if (!g_win) { rt_log("plat_gl_create: no window"); return -1; }
#ifndef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) { rt_log("SDL_GL_CreateContext: %s", SDL_GetError()); return -1; }
    SDL_GL_MakeCurrent(g_win, g_gl);
    SDL_GL_SetSwapInterval(1);
#ifndef __EMSCRIPTEN__
    void (*gen)(int, unsigned *) = (void (*)(int, unsigned *))SDL_GL_GetProcAddress("glGenVertexArrays");
    void (*bind)(unsigned) = (void (*)(unsigned))SDL_GL_GetProcAddress("glBindVertexArray");
    unsigned vao = 0;
    if (gen && bind) { gen(1, &vao); bind(vao); }
#endif
    int w, h;
    SDL_GL_GetDrawableSize(g_win, &w, &h);
    rt_log("plat: GL context %dx%d", w, h);
    return 0;
}

void plat_gl_size(int *w, int *h) {
    *w = 800; *h = 600;
    if (g_win) SDL_GL_GetDrawableSize(g_win, w, h);
}

void plat_set_gamma(const uint16_t *ramp) {
#ifndef __EMSCRIPTEN__   /* the browser has no display gamma table */
    if (g_win && SDL_SetWindowGammaRamp(g_win, ramp, ramp + 256, ramp + 512) != 0)
        rt_log("plat: SetWindowGammaRamp: %s", SDL_GetError());
#else
    (void)ramp;
#endif
}

void plat_gl_set_size(int w, int h) {
    if (g_win && w > 0 && h > 0) SDL_SetWindowSize(g_win, w, h);
}

/* Present at most 60 frames per second (RT_FPS=N, 0 = uncapped), as on the 60 Hz displays the game
   was made for. Vsync alone runs it at the host's refresh rate (120 Hz on ProMotion Macs), and the
   game's per-frame steering loses turns at a quarter of the time step it was tuned for: sprinting
   players could not change direction. */
static void frame_pace(void) {
    static double period = -1, next;
    if (period < 0) {
        const char *e = getenv("RT_FPS");
        int fps = e ? atoi(e) : 60;
        period = fps > 0 ? (double)SDL_GetPerformanceFrequency() / fps : 0;
        next = 0;
    }
    if (period <= 0) return;
    double now = (double)SDL_GetPerformanceCounter();
    if (next == 0 || now - next > 4 * period) next = now;   /* first frame, or fell far behind: resync */
    next += period;
    double wait = next - now;
    if (wait > 0) {
        uint32_t ms = (uint32_t)(wait * 1000.0 / (double)SDL_GetPerformanceFrequency());
        if (ms > 1) SDL_Delay(ms - 1);
        while ((double)SDL_GetPerformanceCounter() < next) { }
    }
}

void plat_gl_swap(void) {
#ifndef __EMSCRIPTEN__
    if (g_win && g_gl) SDL_GL_SwapWindow(g_win);
    frame_pace();
#endif
}

void plat_cursor_pos(int *x, int *y) {
    if (virtual_cursor()) { *x = g_vcur_x; *y = g_vcur_y; return; }
    int gx = 0, gy = 0;
    if (g_rel && g_win) { int wx, wy; SDL_GetWindowPosition(g_win, &wx, &wy); *x = wx + g_cx; *y = wy + g_cy; return; }
    if (g_inited) SDL_GetGlobalMouseState(&gx, &gy);
    *x = gx; *y = gy;
}

void plat_cursor_set_pos(int x, int y) {
    if (virtual_cursor()) { g_vcur_x = x; g_vcur_y = y; return; }
    (void)x; (void)y;
    /* The original game uses SetCursorPos to maintain its virtual menu cursor.
       Warping the macOS pointer here makes it fight the user's real pointer and
       also feeds back a second coordinate system through SDL mouse events.
       GetCursorPos already returns the real desktop position, so leave the
       host pointer alone and let the game's rendered cursor follow it. */
}

void plat_cursor_visible(int show) { if (g_inited) SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE); }

/* ---- events -> messages ---------------------------------------------------------------------- */
#define WM_SIZE_ 0x0005
#define WM_ACTIVATE_ 0x0006
#define WM_PAINT_ 0x000F
#define WM_CLOSE_ 0x0010
#define WM_ACTIVATEAPP_ 0x001C
#define WM_KEYDOWN_ 0x0100
#define WM_KEYUP_ 0x0101
#define WM_CHAR_ 0x0102
#define WM_SYSKEYDOWN_ 0x0104
#define WM_SYSKEYUP_ 0x0105
#define WM_MOUSEMOVE_ 0x0200
#define WM_LBUTTONDOWN_ 0x0201
#define WM_MOUSEWHEEL_ 0x020A

static plat_msg_t g_pending[8];
static int g_npending;

static void push(uint32_t msg, uint32_t wp, uint32_t lp) {
    if (g_npending < 8) g_pending[g_npending++] = (plat_msg_t){ msg, wp, lp };
}

static uint32_t mk_buttons(void) {
    uint32_t b = SDL_GetMouseState(NULL, NULL), mk = 0;
    if (b & SDL_BUTTON_LMASK) mk |= 0x01;
    if (b & SDL_BUTTON_RMASK) mk |= 0x02;
    if (b & SDL_BUTTON_MMASK) mk |= 0x10;
    if (plat_vk_down(0x10)) mk |= 0x04;
    if (plat_vk_down(0x11)) mk |= 0x08;
    return mk;
}

/* Interactive play captures the pointer (SDL relative mode) and tracks the in-window position from
   the deltas, so the pointer can never stall at a window edge while the game cursor still has room. */
static void rel_set(int on) {
    if (virtual_cursor() || !g_win || on == g_rel) return;
    if (on) { int x, y; SDL_GetMouseState(&x, &y); g_cx = x; g_cy = y; }
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_rel = on;
}
static void rel_move(int dx, int dy) {
    int w = 640, h = 480;
    if (g_win) SDL_GetWindowSize(g_win, &w, &h);
    g_cx += dx; g_cy += dy;
    if (g_cx < 0) g_cx = 0; else if (g_cx > w - 1) g_cx = w - 1;
    if (g_cy < 0) g_cy = 0; else if (g_cy > h - 1) g_cy = h - 1;
}

static void translate(const SDL_Event *e) {
    if (rt_log_input) {
        if (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP)
            rt_log("input: key %s %s (vk 0x%02X)", SDL_GetScancodeName(e->key.keysym.scancode),
                   e->type == SDL_KEYDOWN ? "down" : "up", g_keys[e->key.keysym.scancode].vk);
        else if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP)
            rt_log("input: mouse button %d %s at %d,%d", e->button.button, e->type == SDL_MOUSEBUTTONDOWN ? "down" : "up", e->button.x, e->button.y);
        else if (e->type == SDL_WINDOWEVENT && (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e->window.event == SDL_WINDOWEVENT_FOCUS_LOST))
            rt_log("input: window focus %s", e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? "gained" : "lost");
    }
    switch (e->type) {
    case SDL_QUIT: push(WM_CLOSE_, 0, 0); break;
    case SDL_WINDOWEVENT:
        switch (e->window.event) {
        case SDL_WINDOWEVENT_CLOSE: push(WM_CLOSE_, 0, 0); break;
        case SDL_WINDOWEVENT_FOCUS_GAINED: rel_set(1); push(WM_ACTIVATEAPP_, 1, 0); push(WM_ACTIVATE_, 1, 0); break;
        case SDL_WINDOWEVENT_FOCUS_LOST: rel_set(0); push(WM_ACTIVATE_, 0, 0); push(WM_ACTIVATEAPP_, 0, 0); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            push(WM_SIZE_, 0, ((uint32_t)e->window.data2 << 16) | ((uint32_t)e->window.data1 & 0xFFFF)); break;
        case SDL_WINDOWEVENT_MINIMIZED: push(WM_SIZE_, 1, 0); break;
        case SDL_WINDOWEVENT_EXPOSED: push(WM_PAINT_, 0, 0); break;
        }
        break;
    case SDL_KEYDOWN: case SDL_KEYUP: {
        keymap_t k = g_keys[e->key.keysym.scancode];
        if (!k.vk) break;
        int down = e->type == SDL_KEYDOWN, alt = plat_vk_down(0x12) || k.vk == 0xA4 || k.vk == 0xA5;
        g_vk_down[k.vk] = (uint8_t)down;
        g_dik_down[(k.scan | (k.ext ? 0x80 : 0)) & 0xFF] = (uint8_t)down;
        uint32_t vk = k.vk;               /* WM_KEY* carry the generic modifier codes */
        if (vk == 0xA0 || vk == 0xA1) vk = 0x10;
        else if (vk == 0xA2 || vk == 0xA3) vk = 0x11;
        else if (vk == 0xA4 || vk == 0xA5) vk = 0x12;
        uint32_t lp = 1u | ((uint32_t)k.scan << 16) | ((uint32_t)k.ext << 24) | (alt ? 1u << 29 : 0);
        if (!down) lp |= 3u << 30;
        else if (e->key.repeat) lp |= 1u << 30;
        push(alt ? (down ? WM_SYSKEYDOWN_ : WM_SYSKEYUP_) : (down ? WM_KEYDOWN_ : WM_KEYUP_), vk, lp);
        break;                            /* WM_CHAR: the game's TranslateMessage (user32.c) */
    }
    case SDL_MOUSEMOTION:
        g_mdx += e->motion.xrel; g_mdy += e->motion.yrel;
        if (!g_rel && plat_window_focused()) rel_set(1);
        if (g_rel) { rel_move(e->motion.xrel, e->motion.yrel); push(WM_MOUSEMOVE_, mk_buttons(), ((uint32_t)(g_cy & 0xFFFF) << 16) | (uint32_t)(g_cx & 0xFFFF)); break; }
        push(WM_MOUSEMOVE_, mk_buttons(), ((uint32_t)(e->motion.y & 0xFFFF) << 16) | (uint32_t)(e->motion.x & 0xFFFF));
        break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
        int b = e->button.button, up = e->type == SDL_MOUSEBUTTONUP;
        uint32_t base = b == SDL_BUTTON_LEFT ? 0x201 : b == SDL_BUTTON_RIGHT ? 0x204 : b == SDL_BUTTON_MIDDLE ? 0x207 : 0;
        uint8_t vk = b == SDL_BUTTON_LEFT ? 1 : b == SDL_BUTTON_RIGHT ? 2 : b == SDL_BUTTON_MIDDLE ? 4 : 0;
        if (!base) break;
        g_vk_down[vk] = (uint8_t)!up;
        if (g_rel) { push(base + (uint32_t)up, mk_buttons(), ((uint32_t)(g_cy & 0xFFFF) << 16) | (uint32_t)(g_cx & 0xFFFF)); break; }
        push(base + (uint32_t)up, mk_buttons(), ((uint32_t)(e->button.y & 0xFFFF) << 16) | (uint32_t)(e->button.x & 0xFFFF));
        break;
    }
    case SDL_MOUSEWHEEL:
        g_mdw += e->wheel.y * 120;
        push(WM_MOUSEWHEEL_, ((uint32_t)(uint16_t)(int16_t)(e->wheel.y * 120) << 16) | mk_buttons(), 0);
        break;
    }
    (void)WM_LBUTTONDOWN_;
}

int plat_poll(plat_msg_t *m, int wait_ms) {
    if (!g_inited) return 0;
    SDL_Event e;
    if (g_activate_pending) { g_activate_pending = 0; push(WM_ACTIVATEAPP_, 1, 0); push(WM_ACTIVATE_, 1, 0); }
    while (!g_npending) {
        int got = wait_ms > 0 ? SDL_WaitEventTimeout(&e, wait_ms) : SDL_PollEvent(&e);
        if (!got) return 0;
        wait_ms = 0;
        if (virtual_cursor() && (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT ||
            e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP ||
            e.type == SDL_MOUSEWHEEL))
            continue;                     /* automated runs: only the replay drives input, never the user's devices */
        translate(&e);
    }
    *m = g_pending[0];
    memmove(g_pending, g_pending + 1, (size_t)--g_npending * sizeof g_pending[0]);
    return 1;
}

/* ---- input replay (plat.h plat_replay_tick) ---------------------------------------------------- */
typedef struct { uint32_t frame; uint8_t down, mouse; SDL_Scancode sc; int x, y; char *path; } replay_ev_t;   /* mouse 3: shot */
static replay_ev_t *g_replay;
static int g_nreplay, g_ireplay, g_replay_loaded;

static void replay_add(uint32_t frame, int down, SDL_Scancode sc) {
    g_replay = realloc(g_replay, (size_t)(g_nreplay + 1) * sizeof *g_replay);
    g_replay[g_nreplay++] = (replay_ev_t){ frame, (uint8_t)down, 0, sc, 0, 0 };
}

static int replay_cmp(const void *a, const void *b) {
    const replay_ev_t *x = a, *y = b;
    return x->frame != y->frame ? (x->frame < y->frame ? -1 : 1) : 0;
}

static void replay_load(void) {
    g_replay_loaded = 1;
    if (!rt_replay_path[0]) return;
    FILE *f = fopen(rt_replay_path, "r");
    if (!f) { rt_log("replay: cannot open %s", rt_replay_path); return; }
    char line[256], act[16], key[64];
    unsigned frame;
    int mx, my;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%u click %d %d", &frame, &mx, &my) == 3) {   /* left click at window x,y */
            g_replay = realloc(g_replay, (size_t)(g_nreplay + 2) * sizeof *g_replay);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 1, 1, SDL_SCANCODE_UNKNOWN, mx, my };
            g_replay[g_nreplay++] = (replay_ev_t){ frame + 3, 0, 1, SDL_SCANCODE_UNKNOWN, mx, my };
            continue;
        }
        mx = 0;
        unsigned hold = 3;                                                 /* `press [frames held]` */
        if (sscanf(line, "%u press%n %u", &frame, &mx, &hold) >= 1 && mx > 0) {   /* left click wherever the cursor is */
            g_replay = realloc(g_replay, (size_t)(g_nreplay + 2) * sizeof *g_replay);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 1, 1, SDL_SCANCODE_UNKNOWN, -1, -1 };
            g_replay[g_nreplay++] = (replay_ev_t){ frame + (hold ? hold : 3), 0, 1, SDL_SCANCODE_UNKNOWN, -1, -1 };
            continue;
        }
        char shot[512];
        if (sscanf(line, "%u shot %511s", &frame, shot) == 2) {          /* PNG of that presented frame */
            g_replay = realloc(g_replay, (size_t)(g_nreplay + 1) * sizeof *g_replay);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 0, 3, SDL_SCANCODE_UNKNOWN, 0, 0, strdup(shot) };
            continue;
        }
        if (sscanf(line, "%u move %d %d", &frame, &mx, &my) == 3) {    /* relative mouse motion (DirectInput) */
            g_replay = realloc(g_replay, (size_t)(g_nreplay + 1) * sizeof *g_replay);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 0, 2, SDL_SCANCODE_UNKNOWN, mx, my };
            continue;
        }
        if (line[0] == '#' || sscanf(line, "%u %15s %63[^\n]", &frame, act, key) != 3) continue;
        SDL_Scancode sc = SDL_GetScancodeFromName(key);
        if (sc == SDL_SCANCODE_UNKNOWN) { rt_log("replay: unknown key '%s'", key); continue; }
        if (!strcmp(act, "tap")) { replay_add(frame, 1, sc); replay_add(frame + 3, 0, sc); }   /* held 3 frames */
        else if (!strcmp(act, "down")) replay_add(frame, 1, sc);
        else if (!strcmp(act, "up")) replay_add(frame, 0, sc);
    }
    fclose(f);
    qsort(g_replay, (size_t)g_nreplay, sizeof *g_replay, replay_cmp);
    rt_log("replay: %d events from %s", g_nreplay, rt_replay_path);
}

static uint32_t g_replay_last;
uint32_t plat_replay_last_event(void) { return g_replay_last; }

void plat_replay_tick(uint32_t frame) {
    if (!g_replay_loaded) replay_load();
    for (; g_ireplay < g_nreplay && g_replay[g_ireplay].frame <= frame; g_ireplay++) {
        const replay_ev_t *r = &g_replay[g_ireplay];
        g_replay_last = frame;
        SDL_Event e;
        memset(&e, 0, sizeof e);
        if (r->mouse == 3) { snprintf(rt_shot_path, sizeof rt_shot_path, "%s", r->path); continue; }
        if (r->mouse == 2) {              /* like a real mouse: DirectInput delta, cursor position and WM_MOUSEMOVE */
            int wx = 0, wy = 0, ww = 640, wh = 480;
            if (g_win) { SDL_GetWindowPosition(g_win, &wx, &wy); SDL_GetWindowSize(g_win, &ww, &wh); }
            int cx = g_vcur_x - wx + r->x, cy = g_vcur_y - wy + r->y;
            cx = cx < 0 ? 0 : cx > ww - 1 ? ww - 1 : cx; cy = cy < 0 ? 0 : cy > wh - 1 ? wh - 1 : cy;
            g_vcur_x = wx + cx; g_vcur_y = wy + cy;
            e.type = SDL_MOUSEMOTION;
            e.motion.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
            e.motion.x = cx; e.motion.y = cy; e.motion.xrel = r->x; e.motion.yrel = r->y;
            if (g_inited) translate(&e);
            rt_trace("replay: frame %u mouse move %d,%d", frame, r->x, r->y);
            continue;
        }
        if (r->mouse) {
            int wx = 0, wy = 0;
            if (g_win) SDL_GetWindowPosition(g_win, &wx, &wy);
            if (r->x >= 0) { g_vcur_x = wx + r->x; g_vcur_y = wy + r->y; }
            int bx = g_vcur_x - wx, by = g_vcur_y - wy;
            e.type = r->down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.state = r->down ? SDL_PRESSED : SDL_RELEASED;
            e.button.clicks = 1;
            e.button.x = bx; e.button.y = by;
            if (g_inited) translate(&e);   /* direct: real input is filtered out in automated runs */
            g_replay_buttons = r->down ? 1 : 0;
            rt_trace("replay: frame %u mouse %s at %d,%d", frame, r->down ? "down" : "up", bx, by);
            continue;
        }
        e.type = r->down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = r->down ? SDL_PRESSED : SDL_RELEASED;
        e.key.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
        e.key.keysym.scancode = r->sc;
        e.key.keysym.sym = SDL_GetKeyFromScancode(r->sc);
        if (g_inited) translate(&e);   /* direct: real input is filtered out in automated runs */
        keymap_t k = g_keys[r->sc];          /* DirectInput state now, not when the pump runs */
        if (k.scan) g_dik_down[(k.scan | (k.ext ? 0x80 : 0)) & 0xFF] = r->down;
        rt_trace("replay: frame %u %s %s", frame, r->down ? "down" : "up", SDL_GetScancodeName(r->sc));
    }
}
