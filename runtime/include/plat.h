/* plat.h -- host services the Win32 HLE needs: one game window, its events as Win32 messages,
 * keyboard and cursor state. Implemented once on SDL2 (platform/sdl/sdl_plat.c) for native and web.
 * All calls are made from the thread that created the window (the game's main thread). */
#ifndef PLAT_H
#define PLAT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t msg, wparam, lparam;
} plat_msg_t;

int plat_window_open(const char *title, int x, int y, int w, int h);   /* 0 = ok */
void plat_window_close(void);
int plat_window_is_open(void);
void plat_window_show(int show);
void plat_window_title(const char *title);
void plat_window_move(int x, int y, int w, int h, int move, int size);
void plat_window_rect(int *x, int *y, int *w, int *h);      /* client area, screen coordinates */
int plat_window_minimized(void);
int plat_window_focused(void);
void plat_desktop_size(int *w, int *h);
void *plat_window_handle(void);                              /* SDL_Window* (for the D3D layer) */

/* GL for the D3D layer. Native: an OpenGL 4.1 core context on the game window, with one VAO bound
 * (WebGL2 code relies on the default VAO core profiles lack). Web: d3d9-webgl owns the WebGL
 * context and the browser presents, so plat_gl_swap does nothing. */
int plat_gl_create(void);                                   /* 0 = ok */
void plat_gl_size(int *w, int *h);                           /* drawable size in pixels */
void plat_gl_set_size(int w, int h);                         /* resize the window to w x h */
void plat_gl_swap(void);
/* Netplay keyboard and mouse ("desk"): the host's real key and mouse events travel with its input
 * and every instance injects them at the same frame, like replay input (deterministic). */
typedef struct {
    uint8_t type, a;
    int16_t x, y;
} plat_desk_ev_t;   /* 6 bytes on the wire */
enum { PLAT_DESK_KEY = 1, PLAT_DESK_MOVE, PLAT_DESK_BUTTON, PLAT_DESK_WHEEL };
/* KEY: a = down, x = SDL scancode. MOVE: x, y = relative motion. BUTTON: a = down, x = 1 left,
 * 2 middle, 3 right. WHEEL: y = notches. */
void plat_desk_capture(int on);                             /* record this machine's events */
int plat_desk_take(plat_desk_ev_t *out, int max);           /* recorded events, oldest first */
void plat_desk_apply(const plat_desk_ev_t *ev);             /* inject one into the guest */
void plat_frame_yield(void);   /* web: hand control to the browser and come back at once (JSPI); native: nothing */
void plat_set_gamma(const uint16_t *ramp);                   /* 3 x 256 WORDs (R, G, B), D3DGAMMARAMP layout */

/* Next pending event as a WM_* message (hwnd filled in by user32). 1 = got one, 0 = none.
 * wait_ms > 0 blocks up to that long for the first event. */
int plat_poll(plat_msg_t *m, int wait_ms);
int plat_pending(void);   /* messages queued by the replay or window activation, not yet polled */

uint8_t plat_vk_toggled(uint32_t vk); /* 1 while the Caps Lock / Num Lock (VK_CAPITAL, VK_NUMLOCK) light is on */
uint8_t plat_vk_down(uint32_t vk); /* 1 while virtual key vk is held */
uint8_t plat_dik_down(uint32_t dik); /* 1 while DirectInput key dik (set-1 scan | 0x80 extended) is held */
void plat_mouse_delta(int *dx, int *dy, int *dwheel); /* motion since the last call */
int plat_pointer(float *x, float *y); /* the browser's pointer in game units (640x480); 1 if it moved */
int plat_local_pointer(float *x, float *y); /* this player's own real pointer, game units; 1 once it moved */
uint8_t plat_mouse_buttons(void); /* bit 0 left, 1 right, 2 middle */
uint32_t plat_vk_to_scancode(uint32_t vk); /* Win32 set-1 scan code, 0 if none */
void plat_cursor_pos(int *x, int *y); /* screen coordinates */
void plat_cursor_set_pos(int x, int y);
void plat_cursor_visible(int show);

/* Virtual gamepads (runtime/core/pads.c): the game sees them as DirectInput "Logitech Dual Action"
 * pads (runtime/win32/dinput.c), after the mouse and keyboard, or before the keyboard in a netplay
 * session so that players 1-4 are Team Select's entries 1-4. Sticks -32768..32767 (y down
 * positive); buttons RT_PAD_* bits; hat RT_HAT_* bits. In deterministic mode they change only at
 * Present (replay pad events, netplay frame inputs). */
enum {
    RT_PAD_CROSS = 1,
    RT_PAD_CIRCLE = 2,
    RT_PAD_SQUARE = 4,
    RT_PAD_TRIANGLE = 8,
    RT_PAD_L1 = 16,
    RT_PAD_R1 = 32,
    RT_PAD_L2 = 64,
    RT_PAD_R2 = 128,
    RT_PAD_START = 256,
    RT_PAD_SELECT = 512,
    RT_PAD_L3 = 1024,
    RT_PAD_R3 = 2048
};
enum { RT_HAT_UP = 1, RT_HAT_RIGHT = 2, RT_HAT_DOWN = 4, RT_HAT_LEFT = 8 };
#define RT_MAX_PADS 4
typedef struct {
    int16_t lx, ly, rx, ry;
    uint16_t buttons;
    uint8_t hat, spare;
} rt_pad_t;
extern rt_pad_t rt_pads[RT_MAX_PADS];
extern int rt_npads; /* pads the game can see (RT_PADS=N, netplay players) */
extern int rt_pads_first; /* 1: pads enumerate before the keyboard (netplay) */
int rt_pad_event(int pad, const char *name, int down);
int rt_pad_count(void); /* replay "pad N tap|down|up NAME"; 0 = unknown name */
void plat_local_pad(rt_pad_t *p); /* this player's live devices -> p (unchanged in replay runs) */
int plat_gamepad(rt_pad_t *p);    /* this machine's game controller alone -> p: 1 while connected */
/* Which pad slots are plugged in (pads.c, runtime/core/pad_attach.c): a slot exists from boot (the
   game enumerates once) and the game's controller for it is attached or not, as a pad that comes
   and goes on Windows: slot 0 this machine's game controller, slots 1..3 the party's friends (host). */
extern uint32_t rt_pads_party; /* host: bit k = room slot k has a member */
int rt_pad_attached(int k);
void rt_pads_present(uint32_t frame); /* every Present, before the frame's input reaches the game */
void rt_pad_pause_frame(uint32_t frame); /* every Present, after it: Start pauses a match (pad_pause.c) */

/* Netplay (runtime/core/netplay.c). Transport: platform/native/net_native.c, platform/web/. */
extern int rt_net;
extern int rt_ff; /* catching up (netplay): skip drawing and sound */
int rt_net_start(int argc, char **argv); /* --net URL --net-room CODE ...: 1 if a session started */
int rt_net_begin(int slot, int creator);
extern int rt_net_cfg_delay; /* --net-delay D (net_join.c; 0: default) */
extern const char *rt_net_room; /* --net-room CODE (net_join.c) */
rt_pad_t *rt_net_local_pad(void);
int rt_net_active(int k);   /* lockstep: room slot k plays at the frame being resolved */
int rt_net_slot(void);      /* lockstep: this player's room slot */
int rt_net_external_source(void);                       /* net_external.c: this game reads its machine */
void rt_net_external_put(const void *data, uint32_t len); /* ... and shares what it read */
void rt_net_external_get(void *data, uint32_t len);     /* ... or gets the bytes another game read */
const char *plat_session_code(void);      /* lockstep: this session's room code ("": none) */
void plat_session_join(const char *code); /* lockstep: play in that room instead (the page reloads) */
void plat_session_leave(void);           /* lockstep: leave for a fresh own game (the page reloads) */
/* joining a party: its screen from the start (platform/web/join_view.c; native: none) */
int plat_join_preview(int *slot, uint32_t *players, char *code, int cap); /* 1: joining that room as SLOT */
void plat_join_snapshot(int menu_x); /* the frame just drawn is that party's screen: keep it (and the menu's x) */
int plat_join_menu_x(void);         /* after the reload: that menu x (front-end units), -1 none */
int plat_join_backdrop(uint8_t *rgba, int w, int h); /* while catching up: the kept picture, W x H RGBA */
int plat_join_progress(int slot); /* the others: player SLOT joining, percent caught up; -1 not joining */
const char *plat_page_message(void);     /* a message for the game to show (the page: join errors...), or NULL */
extern int rt_net_progress;              /* lockstep: catching up, percent done */
/* Host-authoritative mode (runtime/core/net_ha.c, --net-mode ha) */
extern int rt_net_ha; /* 1 host, 2 friend */
int rt_net_begin_ha(int slot, int creator);
void rt_net_end_ha(void); /* back to playing alone (the Online screens' LEAVE) */
extern const char *rt_net_url; /* the relay (--net URL; ui_lobby.c) */
void net_ha_present(uint32_t frame); /* d3d8 Present, before the frame's input */
int net_tx_open(const char *url); /* 0 = connected */
int net_tx_send(const void *data, int len, int text);
void net_tx_close(void);
int net_tx_recv(void *buf, int cap, int wait_ms); /* >0 length, 0 none, -1 closed */
int net_tx_recv_peer(void *buf, int cap); /* a peer's direct copy of an 'I'/'H' (web), >0 length, 0 none */
int net_tx_delay(void);
int net_tx_hints(int64_t *grant, int64_t *front);
/* lockstep: the server's hints the page holds (1: some) */ /* this player's input delay in frames (0: no preference) */
void net_tx_status(int kind, int value); /* for the page: 0 running, 1 waiting for slot value,
                                                     2 catching up value %, 3 connection lost (offline) */

/* Input replay (--replay FILE, tests/replays/): lines "<frame> tap|down|up <SDL key name>", '#'
 * comments. Called once per presented frame; due events become ordinary SDL key events, so they
 * reach the game as WM_KEY* messages and DirectInput state exactly like a real key. */
void plat_replay_tick(uint32_t frame);
uint32_t plat_replay_last_event(void); /* frame of the most recent injected replay event (0 = none) */

/* Audio output: 44.1 kHz stereo float. mix(out, frames) fills interleaved L/R frames; it runs on
 * the host's audio thread (or a pacing thread when no device opens), so it must take its own
 * locks. Started once; with --audio-capture PATH the mixed output is also written as a 16-bit WAV. */
enum { PLAT_AUDIO_RATE = 44100 };
typedef void (*plat_audio_mix_fn)(float *out, int frames);
int plat_audio_start(plat_audio_mix_fn mix); /* 0 = device opened, 1 = paced without one */

/* The guest arena: guest VA v is at arena + v for every v < *size (runtime/core/mem.c). */
void *plat_guest_arena(uint32_t *size);

#ifdef __cplusplus
}
#endif
#endif
