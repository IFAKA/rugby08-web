/* sdl_window.c -- the game window, its GL context and the cursor. */
#include "sdl_int.h"

/* ---- cursor --------------------------------------------------------------------------------- */
/* Automated runs (--frames N or --replay) never move the user's real cursor: the game calls
   SetCursorPos every frame, which pinned the desktop pointer during checks. They keep a virtual
   cursor in screen coordinates instead; replay clicks set it. */
int g_vcur_x, g_vcur_y;
int g_rel, g_cx, g_cy;   /* captured-pointer mode and in-window position */
int virtual_cursor(void) { return rt_frame_limit > 0 || (rt_replay_path[0] && !rt_replay_live); }

/* ---- window --------------------------------------------------------------------------------- */
SDL_Window *g_win;

static void video_init(void) {
    if (g_inited) return;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) rt_fatal("SDL_Init: %s", SDL_GetError());
    keymap_init();
    g_inited = 1;
}

int g_activate_pending;   /* plat_window_show in an automated run: WM_ACTIVATE* at the next pump */

int plat_window_open(const char *title, int x, int y, int w, int h) {
    video_init();
    if (g_win) return 0;
    if (w <= 0 || h <= 0) {
        w = 800;
        h = 600;
    }
    (void)x;
    (void)y;     /* the host decides placement */
    int flags = SDL_WINDOW_OPENGL;
    if (virtual_cursor()) flags |= SDL_WINDOW_HIDDEN;   /* hide in automated tests; show for interactive play */
    g_win = SDL_CreateWindow(title ? title : "Rugby 08", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!g_win) {
        rt_log("SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    SDL_StopTextInput(); /* no macOS text input: holding a key opened the accent popup (WM_CHAR comes from TranslateMessage) */
    rt_log("plat: window %dx%d \"%s\"", w, h, title ? title : "");
    return 0;
}

void plat_window_close(void) {
    if (g_win) {
        SDL_DestroyWindow(g_win);
        g_win = NULL;
    }
}
int plat_window_is_open(void) { return g_win != NULL; }
void *plat_window_handle(void) { return g_win; }
void plat_window_show(int show) {
    if (!g_win) return;
    if (show && virtual_cursor()) { /* automated runs stay hidden even when the game calls ShowWindow, */
        g_activate_pending = 1; /* but are active as a shown window is on Windows: a hidden window */
        return; /* never gains focus, and the game ignored device input until it did */
    }
    if (show) {
        SDL_ShowWindow(g_win);
    } else {
        SDL_HideWindow(g_win);
    }
}
void plat_window_title(const char *t) {
    if (g_win) SDL_SetWindowTitle(g_win, t ? t : "");
}
int plat_window_minimized(void) { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_MINIMIZED); }
int plat_window_focused(void) { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS); }

void plat_window_move(int x, int y, int w, int h, int move, int size) {
    if (!g_win) return;
    if (size && w > 0 && h > 0) SDL_SetWindowSize(g_win, w, h);
    if (move) SDL_SetWindowPosition(g_win, x, y);
}

void plat_window_rect(int *x, int *y, int *w, int *h) {
    int px = 0, py = 0, pw = 800, ph = 600;
    if (g_win) {
        SDL_GetWindowPosition(g_win, &px, &py);
        SDL_GetWindowSize(g_win, &pw, &ph);
    }
    if (x) *x = px;
    if (y) *y = py;
    if (w) *w = pw;
    if (h) *h = ph;
}

void plat_desktop_size(int *w, int *h) {
    video_init();
    SDL_DisplayMode m;
    if (SDL_GetDesktopDisplayMode(0, &m) != 0) {
        m.w = 1024;
        m.h = 768;
    }
    *w = m.w;
    *h = m.h;
}

/* ---- GL ----------------------------------------------------------------------------------- */
SDL_GLContext g_gl;

int plat_gl_create(void) {
    if (g_gl) return 0;
    if (!g_win) {
        rt_log("plat_gl_create: no window");
        return -1;
    }
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
    if (!g_gl) {
        rt_log("SDL_GL_CreateContext: %s", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_win, g_gl);
    {
        const char *vs = getenv("RT_VSYNC");
        SDL_GL_SetSwapInterval(vs ? atoi(vs) : 0);
    } /* the frame pacer times the frames: a vsync-blocking swap on top quantized slow scenes to 30 fps (RT_VSYNC=1 restores it) */
#ifndef __EMSCRIPTEN__
    void (*gen)(int, unsigned *) = (void (*)(int, unsigned *))SDL_GL_GetProcAddress("glGenVertexArrays");
    void (*bind)(unsigned) = (void (*)(unsigned))SDL_GL_GetProcAddress("glBindVertexArray");
    unsigned vao = 0;
    if (gen && bind) {
        gen(1, &vao);
        bind(vao);
    }
#endif
    int w, h;
    SDL_GL_GetDrawableSize(g_win, &w, &h);
    rt_log("plat: GL context %dx%d", w, h);
    return 0;
}

void plat_gl_size(int *w, int *h) {
    *w = 800;
    *h = 600;
    if (g_win) SDL_GL_GetDrawableSize(g_win, w, h);
}

void plat_set_gamma(const uint16_t *ramp) {
#ifndef __EMSCRIPTEN__ /* the browser has no display gamma table */
    if (g_win && SDL_SetWindowGammaRamp(g_win, ramp, ramp + 256, ramp + 512) != 0)
        rt_log("plat: SetWindowGammaRamp: %s", SDL_GetError());
#else
    (void)ramp;
#endif
}

void plat_gl_set_size(int w, int h) {
    if (g_win && w > 0 && h > 0) SDL_SetWindowSize(g_win, w, h);
}

void plat_cursor_pos(int *x, int *y) {
    if (virtual_cursor()) {
        *x = g_vcur_x;
        *y = g_vcur_y;
        return;
    }
    int gx = 0, gy = 0;
    if (g_rel && g_win) {
        int wx, wy;
        SDL_GetWindowPosition(g_win, &wx, &wy);
        *x = wx + g_cx;
        *y = wy + g_cy;
        return;
    }
    if (g_inited) SDL_GetGlobalMouseState(&gx, &gy);
    *x = gx;
    *y = gy;
}

void plat_cursor_set_pos(int x, int y) {
    if (virtual_cursor()) {
        g_vcur_x = x;
        g_vcur_y = y;
        return;
    }
    (void)x;
    (void)y;
    /* The original game uses SetCursorPos to maintain its virtual menu cursor.
       Warping the macOS pointer here makes it fight the user's real pointer and
       also feeds back a second coordinate system through SDL mouse events.
       GetCursorPos already returns the real desktop position, so leave the
       host pointer alone and let the game's rendered cursor follow it. */
}

void plat_cursor_visible(int show) {
    if (g_inited) SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}
