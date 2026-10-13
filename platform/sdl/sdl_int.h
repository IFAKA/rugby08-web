/* sdl_int.h -- state the SDL platform sources share (sdl_keys.c, sdl_window.c, sdl_frame.c,
 * sdl_events.c, sdl_replay.c). */
#ifndef SDL_INT_H
#define SDL_INT_H
#include "plat.h"
#include "rt.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint8_t vk, scan, ext;
} keymap_t;   /* SDL scancode -> Win32 virtual key + set-1 scan code */
extern keymap_t g_keys[SDL_NUM_SCANCODES];
extern uint8_t g_vk_down[256], g_dik_down[256];
extern int g_mdx, g_mdy, g_mdw;          /* mouse motion since the last plat_mouse_delta */
extern int g_px, g_py, g_pmoved;         /* the pointer in the window (plat_pointer) */
extern int g_lpx, g_lpy, g_lpmoved;      /* this player's own pointer (plat_local_pointer) */
extern uint8_t g_replay_buttons;         /* mouse buttons held by the input replay (ORed into DirectInput) */
extern int g_inited;                     /* SDL video initialised */
extern SDL_Window *g_win;
extern SDL_GLContext g_gl;
extern int g_vcur_x, g_vcur_y;           /* the virtual cursor of automated runs (screen coordinates) */
extern int g_rel, g_cx, g_cy;            /* captured-pointer mode and in-window position */
extern int g_activate_pending;           /* plat_window_show in an automated run: WM_ACTIVATE* at the next pump */
int automated_run(void);                 /* --frames N or a --replay: hidden window, the replay is the input */
int virtual_cursor(void);                /* automated run or deterministic mode: never touch/read the real cursor */
uint32_t live_mouse(void);               /* the real mouse buttons (SDL mask), 0 under a virtual cursor */
extern int g_desk_on, g_desk_applying;   /* netplay keyboard+mouse (sdl_desk.c) */
void desk_record(const SDL_Event *e);
void rel_set(int on);
void keymap_init(void);
void translate(const SDL_Event *e);      /* an SDL event -> pending Win32 messages and input state */

#endif
