/* sdl_keys.c -- keyboard: SDL scancodes to Win32 virtual keys and DirectInput scan codes, key state. */
#include "sdl_int.h"

keymap_t g_keys[SDL_NUM_SCANCODES];
uint8_t g_vk_down[256], g_dik_down[256];
int g_mdx, g_mdy, g_mdw;
int g_px, g_py, g_pmoved;      /* the pointer in the window, and whether it moved since plat_pointer */
uint8_t g_replay_buttons;      /* mouse buttons held by the input replay (ORed into DirectInput) */
int g_inited;                  /* SDL video initialised (set in video_init) */

uint8_t plat_dik_down(uint32_t dik) { return dik < 256 ? g_dik_down[dik] : 0; }
void plat_mouse_delta(int *dx, int *dy, int *dw) {
    *dx = g_mdx;
    *dy = g_mdy;
    *dw = g_mdw;
#ifdef __EMSCRIPTEN__
    *dx = *dy = 0;   /* the browser: the menu cursor is placed where the pointer is (plat_pointer), not moved */
#endif
    g_mdx = g_mdy = g_mdw = 0;
}
/* The browser: the pointer (mouse or a touch) over the game, in its 640x480 screen units; 1 when it
   moved since the last call. Elsewhere 0 (the game moves its cursor by DirectInput deltas). */
int plat_pointer(float *x, float *y) {
#ifdef __EMSCRIPTEN__
    int w = 640, h = 480;
    if (!g_pmoved || !g_win) return 0;
    if (det_on()) {   /* (lockstep: the pointer is the host's applied events, in the window the game knows) */
        int ox, oy;
        plat_window_rect(&ox, &oy, &w, &h);
    } else SDL_GetWindowSize(g_win, &w, &h);
    *x = (float)g_px * 640.0f / (float)(w > 0 ? w : 640);
    *y = (float)g_py * 480.0f / (float)(h > 0 ? h : 480);
    g_pmoved = 0;
    return 1;
#else
    (void)x, (void)y;
    return 0;
#endif
}

/* This player's own pointer (its real mouse or touch, never another player's) in 640x480 game units;
   1 once it has moved over the game. A party draws it on this screen only (runtime/core/ls_cursor.c). */
int g_lpx, g_lpy, g_lpmoved;
int plat_local_pointer(float *x, float *y) {
    int w = 640, h = 480;
    if (!g_lpmoved || !g_win) return 0;
    SDL_GetWindowSize(g_win, &w, &h);
    *x = (float)g_lpx * 640.0f / (float)(w > 0 ? w : 640);
    *y = (float)g_lpy * 480.0f / (float)(h > 0 ? h : 480);
    return 1;
}

/* The real mouse buttons, or none when only the replay/netplay may drive input. */
uint32_t live_mouse(void) { return g_inited && !virtual_cursor() ? SDL_GetMouseState(NULL, NULL) : 0; }
uint8_t plat_mouse_buttons(void) {
    uint32_t b = live_mouse();
    return (uint8_t)((b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0) | (b & SDL_BUTTON_MMASK ? 4 : 0) |
                     g_replay_buttons);
}

void keymap_init(void) {
    static const char row1[] = "QWERTYUIOP", row2[] = "ASDFGHJKL", row3[] = "ZXCVBNM";
    for (int i = 0; i < 26; i++) g_keys[SDL_SCANCODE_A + i].vk = (uint8_t)('A' + i);
    for (int i = 0; row1[i]; i++) g_keys[SDL_SCANCODE_A + row1[i] - 'A'].scan = (uint8_t)(0x10 + i);
    for (int i = 0; row2[i]; i++) g_keys[SDL_SCANCODE_A + row2[i] - 'A'].scan = (uint8_t)(0x1E + i);
    for (int i = 0; row3[i]; i++) g_keys[SDL_SCANCODE_A + row3[i] - 'A'].scan = (uint8_t)(0x2C + i);
    for (int i = 0; i < 9; i++) g_keys[SDL_SCANCODE_1 + i] = (keymap_t){ (uint8_t)('1' + i), (uint8_t)(2 + i), 0 };
    g_keys[SDL_SCANCODE_0] = (keymap_t){ '0', 0x0B, 0 };
    for (int i = 0; i < 10; i++)
        g_keys[SDL_SCANCODE_F1 + i] = (keymap_t){ (uint8_t)(0x70 + i), (uint8_t)(0x3B + i), 0 };
    g_keys[SDL_SCANCODE_F11] = (keymap_t){ 0x7A, 0x57, 0 };
    g_keys[SDL_SCANCODE_F12] = (keymap_t){ 0x7B, 0x58, 0 };
    static const struct {
        int sc;
        keymap_t k;
    } t[] = {
        { SDL_SCANCODE_ESCAPE, { 0x1B, 0x01, 0 } },       { SDL_SCANCODE_MINUS, { 0xBD, 0x0C, 0 } },
        { SDL_SCANCODE_EQUALS, { 0xBB, 0x0D, 0 } },       { SDL_SCANCODE_BACKSPACE, { 0x08, 0x0E, 0 } },
        { SDL_SCANCODE_TAB, { 0x09, 0x0F, 0 } },          { SDL_SCANCODE_LEFTBRACKET, { 0xDB, 0x1A, 0 } },
        { SDL_SCANCODE_RIGHTBRACKET, { 0xDD, 0x1B, 0 } }, { SDL_SCANCODE_RETURN, { 0x0D, 0x1C, 0 } },
        { SDL_SCANCODE_LCTRL, { 0xA2, 0x1D, 0 } },        { SDL_SCANCODE_SEMICOLON, { 0xBA, 0x27, 0 } },
        { SDL_SCANCODE_APOSTROPHE, { 0xDE, 0x28, 0 } },   { SDL_SCANCODE_GRAVE, { 0xC0, 0x29, 0 } },
        { SDL_SCANCODE_LSHIFT, { 0xA0, 0x2A, 0 } },       { SDL_SCANCODE_BACKSLASH, { 0xDC, 0x2B, 0 } },
        { SDL_SCANCODE_COMMA, { 0xBC, 0x33, 0 } },        { SDL_SCANCODE_PERIOD, { 0xBE, 0x34, 0 } },
        { SDL_SCANCODE_SLASH, { 0xBF, 0x35, 0 } },        { SDL_SCANCODE_RSHIFT, { 0xA1, 0x36, 0 } },
        { SDL_SCANCODE_KP_MULTIPLY, { 0x6A, 0x37, 0 } },  { SDL_SCANCODE_LALT, { 0xA4, 0x38, 0 } },
        { SDL_SCANCODE_SPACE, { 0x20, 0x39, 0 } },        { SDL_SCANCODE_CAPSLOCK, { 0x14, 0x3A, 0 } },
        { SDL_SCANCODE_NUMLOCKCLEAR, { 0x90, 0x45, 0 } }, { SDL_SCANCODE_SCROLLLOCK, { 0x91, 0x46, 0 } },
        { SDL_SCANCODE_KP_7, { 0x67, 0x47, 0 } },         { SDL_SCANCODE_KP_8, { 0x68, 0x48, 0 } },
        { SDL_SCANCODE_KP_9, { 0x69, 0x49, 0 } },         { SDL_SCANCODE_KP_MINUS, { 0x6D, 0x4A, 0 } },
        { SDL_SCANCODE_KP_4, { 0x64, 0x4B, 0 } },         { SDL_SCANCODE_KP_5, { 0x65, 0x4C, 0 } },
        { SDL_SCANCODE_KP_6, { 0x66, 0x4D, 0 } },         { SDL_SCANCODE_KP_PLUS, { 0x6B, 0x4E, 0 } },
        { SDL_SCANCODE_KP_1, { 0x61, 0x4F, 0 } },         { SDL_SCANCODE_KP_2, { 0x62, 0x50, 0 } },
        { SDL_SCANCODE_KP_3, { 0x63, 0x51, 0 } },         { SDL_SCANCODE_KP_0, { 0x60, 0x52, 0 } },
        { SDL_SCANCODE_KP_PERIOD, { 0x6E, 0x53, 0 } },    { SDL_SCANCODE_KP_ENTER, { 0x0D, 0x1C, 1 } },
        { SDL_SCANCODE_RCTRL, { 0xA3, 0x1D, 1 } },        { SDL_SCANCODE_KP_DIVIDE, { 0x6F, 0x35, 1 } },
        { SDL_SCANCODE_RALT, { 0xA5, 0x38, 1 } },         { SDL_SCANCODE_PAUSE, { 0x13, 0x45, 0 } },
        { SDL_SCANCODE_HOME, { 0x24, 0x47, 1 } },         { SDL_SCANCODE_UP, { 0x26, 0x48, 1 } },
        { SDL_SCANCODE_PAGEUP, { 0x21, 0x49, 1 } },       { SDL_SCANCODE_LEFT, { 0x25, 0x4B, 1 } },
        { SDL_SCANCODE_RIGHT, { 0x27, 0x4D, 1 } },        { SDL_SCANCODE_END, { 0x23, 0x4F, 1 } },
        { SDL_SCANCODE_DOWN, { 0x28, 0x50, 1 } },         { SDL_SCANCODE_PAGEDOWN, { 0x22, 0x51, 1 } },
        { SDL_SCANCODE_INSERT, { 0x2D, 0x52, 1 } },       { SDL_SCANCODE_DELETE, { 0x2E, 0x53, 1 } },
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
    SDL_Keymod m = virtual_cursor() ? KMOD_NONE : SDL_GetModState();
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
