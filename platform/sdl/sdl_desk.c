/* sdl_desk.c -- netplay keyboard and mouse: the host records its events, every game injects them. */
#include "sdl_int.h"

/* ---- netplay keyboard and mouse (plat.h plat_desk_*) -------------------------------------------- */
/* The host records its real events instead of handing them to its own game; netplay sends them with
   its input and every instance (the host's too) injects them at the same frame through translate(),
   as the replay does. Positions are window coordinates (the game sizes the window: equal everywhere)
   and the host window's real position never enters, so the guest sees the same cursor everywhere. */
int g_desk_on;      /* netplay: this machine's keyboard and mouse go to every game (desk) */
int g_desk_applying; /* inside plat_desk_apply: the event is the same on every machine */
static plat_desk_ev_t g_desk[512];
static int g_desk_n;
static uint8_t g_desk_down[SDL_NUM_SCANCODES], g_desk_btn;   /* recorded as held: released on focus loss */
void plat_desk_capture(int on) { g_desk_on = on; }
static void desk_put(uint8_t type, uint8_t a, int x, int y) {
    if (type == PLAT_DESK_MOVE && g_desk_n && g_desk[g_desk_n - 1].type == PLAT_DESK_MOVE) {   /* one move per run */
        plat_desk_ev_t *m = &g_desk[g_desk_n - 1];
        int nx = m->x + x, ny = m->y + y;
        if (nx >= -32768 && nx <= 32767 && ny >= -32768 && ny <= 32767) {
            m->x = (int16_t)nx;
            m->y = (int16_t)ny;
            return;
        }
    }
    if (g_desk_n < (int)(sizeof g_desk / sizeof *g_desk))
        g_desk[g_desk_n++] = (plat_desk_ev_t){ type, a, (int16_t)x, (int16_t)y };
}
void desk_record(const SDL_Event *e) {
    if (!g_desk_on) return;
    if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {   /* nothing stays held */
        for (int i = 0; i < SDL_NUM_SCANCODES; i++)
            if (g_desk_down[i]) {
                g_desk_down[i] = 0;
                desk_put(PLAT_DESK_KEY, 0, i, 0);
            }
        for (int b = 1; b <= 3; b++)
            if (g_desk_btn & (1 << b)) desk_put(PLAT_DESK_BUTTON, 0, b, 0);
        g_desk_btn = 0;
        rel_set(0);
        return;
    }
    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        int sc = e->key.keysym.scancode, down = e->type == SDL_KEYDOWN;
        if (e->key.repeat || sc <= 0 || sc >= SDL_NUM_SCANCODES || g_desk_down[sc] == down) break;
        g_desk_down[sc] = (uint8_t)down;
        desk_put(PLAT_DESK_KEY, (uint8_t)down, sc, 0);
        break;
    }
    case SDL_MOUSEMOTION: desk_put(PLAT_DESK_MOVE, 0, e->motion.xrel, e->motion.yrel); break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int b = e->button.button, down = e->type == SDL_MOUSEBUTTONDOWN;
        if (b < 1 || b > 3) break;
        if (down && !g_rel) rel_set(1);   /* capture the pointer, as in normal play (the browser: on this click) */
        desk_put(PLAT_DESK_BUTTON, (uint8_t)down, b, 0);
        g_desk_btn = (uint8_t)(down ? g_desk_btn | (1 << b) : g_desk_btn & ~(1 << b));
        break;
    }
    case SDL_MOUSEWHEEL: desk_put(PLAT_DESK_WHEEL, 0, 0, e->wheel.y); break;
    }
}
int plat_desk_take(plat_desk_ev_t *out, int max) {
    if (g_inited) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) desk_record(&e);
    }
    /* A press and its release never share a frame: the game polls keys and buttons once a frame
       (DirectInput) and would never see a quick click or tap (or several that a hitch bunched up). */
    int n = 0;
    for (; n < g_desk_n && n < max; n++) {
        const plat_desk_ev_t *d = &g_desk[n];
        if ((d->type != PLAT_DESK_KEY && d->type != PLAT_DESK_BUTTON) || d->a) continue;
        int pressed = 0;
        for (int j = 0; j < n && !pressed; j++)
            pressed = g_desk[j].type == d->type && g_desk[j].x == d->x && g_desk[j].a;
        if (pressed) break;
    }
    memcpy(out, g_desk, (size_t)n * sizeof *out);
    memmove(g_desk, g_desk + n, (size_t)(g_desk_n - n) * sizeof *g_desk);
    g_desk_n -= n;
    return n;
}
void plat_desk_apply(const plat_desk_ev_t *d) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    Uint32 wid = g_win ? SDL_GetWindowID(g_win) : 0;
    int ox, oy, ow, oh;   /* the window as the guest knows it (det mode: what it asked for) */
    plat_window_rect(&ox, &oy, &ow, &oh);
    switch (d->type) {
    case PLAT_DESK_KEY:
        if (d->x <= 0 || d->x >= SDL_NUM_SCANCODES) return;
        e.type = d->a ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = d->a ? SDL_PRESSED : SDL_RELEASED;
        e.key.windowID = wid;
        e.key.keysym.scancode = (SDL_Scancode)d->x;
        break;
    case PLAT_DESK_MOVE: { /* relative, like the mouse the game reads (DirectInput deltas); the cursor stays in the window */
        int cx = g_vcur_x - ox + d->x, cy = g_vcur_y - oy + d->y;
        cx = cx < 0 ? 0 : cx > ow - 1 ? ow - 1 : cx;
        cy = cy < 0 ? 0 : cy > oh - 1 ? oh - 1 : cy;
        g_vcur_x = ox + cx;
        g_vcur_y = oy + cy; /* screen coordinates (GetCursorPos) */
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = wid;
        e.motion.x = cx;
        e.motion.y = cy;
        e.motion.xrel = d->x;
        e.motion.yrel = d->y;
        break;
    }
    case PLAT_DESK_BUTTON: {
        uint8_t bit = d->x == 1 ? 1 : d->x == 3 ? 2 : d->x == 2 ? 4 : 0;
        if (!bit) return;
        g_replay_buttons = (uint8_t)(d->a ? g_replay_buttons | bit : g_replay_buttons & ~bit);
        e.type = d->a ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.windowID = wid;
        e.button.button = (Uint8)d->x;
        e.button.state = d->a ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = 1;
        e.button.x = g_vcur_x - ox;
        e.button.y = g_vcur_y - oy;
        break;
    }
    case PLAT_DESK_WHEEL:
        e.type = SDL_MOUSEWHEEL;
        e.wheel.windowID = wid;
        e.wheel.y = d->y;
        break;
    default: return;
    }
    int rel = g_rel; /* the host's own pointer capture and focus must not change what its game sees */
    g_rel = 0;
    g_desk_applying = 1;
    if (g_inited) translate(&e);
    g_rel = rel;
    g_desk_applying = 0;
}
