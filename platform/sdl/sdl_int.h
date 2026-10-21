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
/* sdl_skip.c: a slow device draws one step in N (fps.skip); the pacers report every frame */
void skip_frame_done(int drawn, double cost_ms, double period_ms);
int skip_next(void);
void skip_reset(void);
int skip_active(void);                   /* some steps go undrawn (fps.interp stays off) */
/* sdl_interp.c: frames between steps on displays faster than 60 Hz (fps.interp) */
void interp_fill(double shown, double next, double slot, double (*clock)(void), int (*wait)(double due),
                 void (*swap)(void));
int interp_frames_take(void);
double interp_period(void);
int interp_refresh(double shown, double slot, double now, void (*swap)(void));
/* native/render_thread.c (the web: off for now): the GL context on its own thread */
int render_threaded(void);
double render_drawn_ms(void);            /* the render thread's time for the last step's frame */
void skip_render_done(double ms);        /* that time, for the frame skipper (sdl_skip.c) */
void render_swapped(double slot_ms);
int render_defer_size(int w, int h);
void render_pump(void);
enum { COST_PROGRAM, COST_TEXTURE, COST_BUFFER, COST_DRAW, COST_N };   /* render/webgl/cost.cpp */
void d3d9w_cost_take(double *ms, int *n);
double plat_display_hz(void);            /* the display's refresh rate (native/frame.c, web/platform.c) */
int automated_run(void);                 /* --frames N or a --replay: hidden window, the replay is the input */
int virtual_cursor(void);                /* automated run or deterministic mode: never touch/read the real cursor */
uint32_t live_mouse(void);               /* the real mouse buttons (SDL mask), 0 under a virtual cursor */
extern int g_desk_on, g_desk_applying;   /* netplay keyboard+mouse (sdl_desk.c) */
void desk_record(const SDL_Event *e);
void rel_set(int on);
void keymap_init(void);
void translate(const SDL_Event *e);      /* an SDL event -> pending Win32 messages and input state */

#endif
