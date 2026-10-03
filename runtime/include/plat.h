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

/* GL for the D3D layer. Native: an OpenGL 4.1 core context on the game window, with one VAO bound
 * (WebGL2 code relies on the default VAO core profiles lack). Web: d3d9-webgl owns the WebGL
 * context and the browser presents, so plat_gl_swap does nothing. */
int  plat_gl_create(void);                                   /* 0 = ok */
void plat_gl_size(int *w, int *h);                           /* drawable size in pixels */
void plat_gl_set_size(int w, int h);                         /* resize the window to w x h */
void plat_gl_swap(void);

/* Next pending event as a WM_* message (hwnd filled in by user32). 1 = got one, 0 = none.
 * wait_ms > 0 blocks up to that long for the first event. */
int  plat_poll(plat_msg_t *m, int wait_ms);

uint8_t plat_vk_down(uint32_t vk);                           /* 1 while virtual key vk is held */
uint8_t plat_dik_down(uint32_t dik);                         /* 1 while DirectInput key dik (set-1 scan | 0x80 extended) is held */
void plat_mouse_delta(int *dx, int *dy, int *dwheel);        /* motion since the last call */
uint8_t plat_mouse_buttons(void);                            /* bit 0 left, 1 right, 2 middle */
uint32_t plat_vk_to_scancode(uint32_t vk);                   /* Win32 set-1 scan code, 0 if none */
void plat_cursor_pos(int *x, int *y);                        /* screen coordinates */
void plat_cursor_set_pos(int x, int y);
void plat_cursor_visible(int show);

/* Input replay (--replay FILE, tests/replays/): lines "<frame> tap|down|up <SDL key name>", '#'
 * comments. Called once per presented frame; due events become ordinary SDL key events, so they
 * reach the game as WM_KEY* messages and DirectInput state exactly like a real key. */
void plat_replay_tick(uint32_t frame);

/* Audio output: 44.1 kHz stereo float. mix(out, frames) fills interleaved L/R frames; it runs on
 * the host's audio thread (or a pacing thread when no device opens), so it must take its own
 * locks. Started once; with --audio-capture PATH the mixed output is also written as a 16-bit WAV. */
enum { PLAT_AUDIO_RATE = 44100 };
typedef void (*plat_audio_mix_fn)(float *out, int frames);
int  plat_audio_start(plat_audio_mix_fn mix);                /* 0 = device opened, 1 = paced without one */

#ifdef __cplusplus
}
#endif
#endif
