/* user32_int.h -- what the user32 sources share: the window table, the message queue entry points
 * and the virtual-key -> character map (user32_window.c, user32_geom.c, user32_msg.c, user32_input.c). */
#ifndef USER32_INT_H
#define USER32_INT_H
#include "win32.h"
#include "plat.h"

#define WM_CREATE      0x0001
#define WM_DESTROY     0x0002
#define WM_SIZE        0x0005
#define WM_ACTIVATE    0x0006
#define WM_PAINT       0x000F
#define WM_CLOSE       0x0010
#define WM_QUIT        0x0012
#define WM_ERASEBKGND  0x0014
#define WM_ACTIVATEAPP 0x001C
#define WM_NCCREATE    0x0081
#define WM_NCDESTROY   0x0082

#define HWND_DESKTOP_ 0x00010010u
#define HWND_BASE     0x00010020u
#define MAX_WINDOWS   64
#define MAX_CLASSES   32

typedef struct {
    int used;
    char name[64];
    uint16_t atom;
    uint32_t style, wndproc, extra, hinst;
} wclass_t;
typedef struct {
    int used, visible, host;          /* host: backed by the platform window */
    char *title;
    uint32_t wndproc, style, exstyle, hinst, parent, id, userdata;
    int x, y, w, h;
    uint8_t *extra;
    uint32_t nextra;
} wnd_t;

extern pthread_mutex_t g_user32_mu;  /* window classes and the message queue */
extern uint32_t g_main;              /* hwnd of the host-backed window */
wclass_t *class_find(uint32_t name_or_atom);   /* g_user32_mu held */
wnd_t *wnd_get(uint32_t hwnd);
uint32_t user32_send(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);   /* call the WndProc */
void user32_post(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);        /* queue a message */
uint32_t vk_char(uint32_t vk, int shift);   /* character of a virtual key, 0 if none */
uint32_t hle_DestroyWindow(const uint32_t *a);

#endif
