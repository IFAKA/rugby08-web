/* plat.h -- host services the Win32 HLE needs: one game window, its events as Win32 messages,
 * keyboard and cursor state. Implemented once on SDL2 (platform/sdl/sdl_plat.c) for native and web.
 * All calls are made from the thread that created the window (the game's main thread). */
#ifndef PLAT_H
#define PLAT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t msg, wparam, lparam; } plat_msg_t;

int  plat_window_open(const char *title, int x, int y, int w, int h);   /* 0 = ok */
void plat_window_close(void);
int  plat_window_is_open(void);
void plat_window_show(int show);
void plat_window_title(const char *title);
void plat_window_move(int x, int y, int w, int h, int move, int size);
void plat_window_rect(int *x, int *y, int *w, int *h);      /* client area, screen coordinates */
int  plat_window_minimized(void);
int  plat_window_focused(void);
void plat_desktop_size(int *w, int *h);
void *plat_window_handle(void);                              /* SDL_Window* (for the D3D layer) */

/* Next pending event as a WM_* message (hwnd filled in by user32). 1 = got one, 0 = none.
 * wait_ms > 0 blocks up to that long for the first event. */
int  plat_poll(plat_msg_t *m, int wait_ms);

uint8_t plat_vk_down(uint32_t vk);                           /* 1 while virtual key vk is held */
uint32_t plat_vk_to_scancode(uint32_t vk);                   /* Win32 set-1 scan code, 0 if none */
void plat_cursor_pos(int *x, int *y);                        /* screen coordinates */
void plat_cursor_set_pos(int x, int y);
void plat_cursor_visible(int show);

#ifdef __cplusplus
}
#endif
#endif
