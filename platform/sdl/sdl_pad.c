/* sdl_pad.c -- this player's live pad for netplay: a game controller, else the keyboard. */
#include "sdl_int.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* ---- this player's pad (plat.h plat_local_pad): a game controller, else the keyboard ------------ */
/* Keyboard layout = the game's own keyboard map (docs/CONTROLS.md): S Cross, Q Square, W Triangle,
 * E Circle, A L1, D R1, Left Shift L2, Space / Right Shift R2, Left Ctrl or Esc Start, Left Alt
 * Select, Return Cross, Backspace Triangle; arrows left stick, numpad 8/2/4/6 right stick. */
#ifdef __EMSCRIPTEN__
/* The browser: this player's held keys straight from the page (KeyboardEvent.code), not SDL's key
   state, which a page that got focus before SDL registered its handlers never updated (a friend's
   keys did not reach its controller in 4 of 5 drop-in runs). Sampled once per frame, so a press is
   also remembered until the next sample: a tap shorter than a frame (a slow page: two games in one
   machine, a movie waiting for its data) still reaches the pad once. Cleared when the page loses focus. */
// clang-format off
EM_JS(int, js_key_held, (const char *code), {
    if (!Module.rtKeys) {
        const k = Module.rtKeys = new Set(), t = Module.rtTaps = new Set();
        addEventListener('keydown', e => { k.add(e.code); t.add(e.code); }, true);
        addEventListener('keyup', e => k.delete(e.code), true);
        addEventListener('blur', () => { k.clear(); t.clear(); });
    }
    const c = UTF8ToString(code), tapped = Module.rtTaps.delete(c);
    return document.hasFocus() && (Module.rtKeys.has(c) || tapped) ? 1 : 0;
});
// clang-format on
static const char *web_code(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_S: return "KeyS";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_Q: return "KeyQ";
    case SDL_SCANCODE_W: return "KeyW";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_E: return "KeyE";
    case SDL_SCANCODE_A: return "KeyA";
    case SDL_SCANCODE_D: return "KeyD";
    case SDL_SCANCODE_LSHIFT: return "ShiftLeft";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_RSHIFT: return "ShiftRight";
    case SDL_SCANCODE_LCTRL: return "ControlLeft";
    case SDL_SCANCODE_ESCAPE: return "Escape";
    case SDL_SCANCODE_LALT: return "AltLeft";
    case SDL_SCANCODE_LEFT: return "ArrowLeft";
    case SDL_SCANCODE_RIGHT: return "ArrowRight";
    case SDL_SCANCODE_UP: return "ArrowUp";
    case SDL_SCANCODE_DOWN: return "ArrowDown";
    case SDL_SCANCODE_KP_4: return "Numpad4";
    case SDL_SCANCODE_KP_6: return "Numpad6";
    case SDL_SCANCODE_KP_8: return "Numpad8";
    case SDL_SCANCODE_KP_2: return "Numpad2";
    default: return "";
    }
}
#endif

/* Keyboard layout (docs/CONTROLS.md) -> pad buttons. */
static const struct {
    SDL_Scancode sc;
    uint16_t bit;
} k_kb[] = {
    { SDL_SCANCODE_S, RT_PAD_CROSS },
    { SDL_SCANCODE_RETURN, RT_PAD_CROSS },
    { SDL_SCANCODE_Q, RT_PAD_SQUARE },
    { SDL_SCANCODE_W, RT_PAD_TRIANGLE },
    { SDL_SCANCODE_BACKSPACE, RT_PAD_TRIANGLE },
    { SDL_SCANCODE_E, RT_PAD_CIRCLE },
    { SDL_SCANCODE_A, RT_PAD_L1 },
    { SDL_SCANCODE_D, RT_PAD_R1 },
    { SDL_SCANCODE_LSHIFT, RT_PAD_L2 },
    { SDL_SCANCODE_SPACE, RT_PAD_R2 },
    { SDL_SCANCODE_RSHIFT, RT_PAD_R2 },
    { SDL_SCANCODE_LCTRL, RT_PAD_START },
    { SDL_SCANCODE_ESCAPE, RT_PAD_START },
    { SDL_SCANCODE_LALT, RT_PAD_SELECT },
};
#define NKB (sizeof k_kb / sizeof *k_kb)

/* This player's held keys: k[scancode] != 0; *focused: the keys may count (the window/page has focus). */
static const Uint8 *held_keys(int *focused) {
#ifdef __EMSCRIPTEN__
    static Uint8 k[SDL_NUM_SCANCODES];
    static const SDL_Scancode more[] = { SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP,   SDL_SCANCODE_DOWN,
                                         SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6,  SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_2 };
    for (size_t i = 0; i < NKB; i++) k[k_kb[i].sc] = (Uint8)js_key_held(web_code(k_kb[i].sc));
    for (size_t i = 0; i < sizeof more / sizeof *more; i++) k[more[i]] = (Uint8)js_key_held(web_code(more[i]));
    *focused = 1;   /* (js_key_held: only while the page has focus) */
    return k;
#else
    *focused = plat_window_focused();
    return SDL_GetKeyboardState(NULL);
#endif
}

static int16_t axis_keys(const Uint8 *k, SDL_Scancode neg, SDL_Scancode pos) {
    return (int16_t)(k[neg] ? -32768 : k[pos] ? 32767 : 0);
}

/* The host's keyboard is also the game's keyboard (desk), but in a session that device comes after
   the 4 pads and the game only reads controllers 1-4 (menus, movie skip, Team Select): it plays
   controller 1 through this pad, like a friend's keyboard. */
static void keyboard_pad(rt_pad_t *q) {
    int focused;
    const Uint8 *k = held_keys(&focused);
    if (rt_log_input) {   /* why a held key does not reach the pad */
        static int said;
        int any = 0;
        for (size_t i = 0; i < NKB; i++) any |= k[k_kb[i].sc];
        if (any && !said++) rt_log("input: pad keys held (focused %d, host keyboard %d)", focused, g_desk_on);
    }
    if (!focused) return;
    for (size_t i = 0; i < NKB; i++)
        if (k[k_kb[i].sc]) q->buttons |= k_kb[i].bit;
    q->lx = axis_keys(k, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    q->ly = axis_keys(k, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    q->rx = axis_keys(k, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6);
    q->ry = axis_keys(k, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_2);
}

static SDL_GameController *controller(void) {   /* the first game controller, (re)opened as it comes and goes */
    static SDL_GameController *gc;
    if (gc && !SDL_GameControllerGetAttached(gc)) {
        SDL_GameControllerClose(gc);
        gc = NULL;
    }
    for (int i = 0; !gc && i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) gc = SDL_GameControllerOpen(i);
    return gc;
}

static void controller_pad(SDL_GameController *gc, rt_pad_t *q) {
    static const struct {
        SDL_GameControllerButton b;
        uint16_t bit;
    } gb[] = {
        { SDL_CONTROLLER_BUTTON_A, RT_PAD_CROSS },         { SDL_CONTROLLER_BUTTON_B, RT_PAD_CIRCLE },
        { SDL_CONTROLLER_BUTTON_X, RT_PAD_SQUARE },        { SDL_CONTROLLER_BUTTON_Y, RT_PAD_TRIANGLE },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, RT_PAD_L1 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, RT_PAD_R1 },
        { SDL_CONTROLLER_BUTTON_START, RT_PAD_START },     { SDL_CONTROLLER_BUTTON_BACK, RT_PAD_SELECT },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, RT_PAD_L3 },    { SDL_CONTROLLER_BUTTON_RIGHTSTICK, RT_PAD_R3 },
    };
    static const struct {
        SDL_GameControllerButton b;
        uint8_t bit;
    } hat[] = { { SDL_CONTROLLER_BUTTON_DPAD_UP, RT_HAT_UP },
                { SDL_CONTROLLER_BUTTON_DPAD_DOWN, RT_HAT_DOWN },
                { SDL_CONTROLLER_BUTTON_DPAD_LEFT, RT_HAT_LEFT },
                { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, RT_HAT_RIGHT } };
    for (size_t i = 0; i < sizeof gb / sizeof *gb; i++)
        if (SDL_GameControllerGetButton(gc, gb[i].b)) q->buttons |= gb[i].bit;
    for (size_t i = 0; i < sizeof hat / sizeof *hat; i++)
        if (SDL_GameControllerGetButton(gc, hat[i].b)) q->hat |= hat[i].bit;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) q->buttons |= RT_PAD_L2;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) q->buttons |= RT_PAD_R2;
    static const SDL_GameControllerAxis ax[4] = { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
                                                  SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY };
    int16_t *dst[4] = { &q->lx, &q->ly, &q->rx, &q->ry };
    for (int i = 0; i < 4; i++) {
        int16_t v = SDL_GameControllerGetAxis(gc, ax[i]);
        if ((v < 0 ? -v : v) > 8000 && !*dst[i]) *dst[i] = v;   /* 8000: stick dead zone; keys win */
    }
}

void plat_local_pad(rt_pad_t *p) {
    if (automated_run() || !g_inited) return;   /* automated runs: the replay is the input */
    SDL_GameController *gc = controller();
    rt_pad_t q = { 0 };
    keyboard_pad(&q);
    if (gc) controller_pad(gc, &q);
    *p = q;
}
