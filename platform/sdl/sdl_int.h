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

typedef struct { uint8_t vk, scan, ext; } keymap_t;   /* SDL scancode -> Win32 virtual key + set-1 scan code */
extern keymap_t g_keys[SDL_NUM_SCANCODES];
extern uint8_t g_vk_down[256], g_dik_down[256];
extern int g_mdx, g_mdy, g_mdw;          /* mouse motion since the last plat_mouse_delta */
extern uint8_t g_replay_buttons;         /* mouse buttons held by the input replay (ORed into DirectInput) */
extern int g_inited;                     /* SDL video initialised */
extern SDL_Window *g_win;
extern SDL_GLContext g_gl;
extern int g_vcur_x, g_vcur_y;           /* the virtual cursor of automated runs (screen coordinates) */
extern int g_rel, g_cx, g_cy;            /* captured-pointer mode and in-window position */
extern int g_activate_pending;           /* plat_window_show in an automated run: WM_ACTIVATE* at the next pump */
int virtual_cursor(void);                /* automated run: never touch the user's real cursor */
void keymap_init(void);
void translate(const SDL_Event *e);      /* an SDL event -> pending Win32 messages and input state */

#endif
