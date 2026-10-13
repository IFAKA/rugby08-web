/* ui.h -- the in-game Online screens (runtime/core/ui_*.c): drawn by the runtime into an RGBA image over
 * the game's picture at Present (render/webgl/overlay.cpp), driven by keys the game does not see while
 * they are open (platform/sdl). In every game (RT_ONLINE=0 turns them off). */
#ifndef RT_UI_H
#define RT_UI_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int w, h;
    uint32_t *px; /* RGBA bytes in memory order (little-endian: 0xAABBGGRR) */
} ui_image_t;

#define UI_WHITE  0xFFFFFFFFu
#define UI_GOLD   0xFF40C8F0u /* the game's highlighted menu item */
#define UI_GREY   0xFFB0B0B0u
#define UI_SHADOW 0xC0000000u
#define UI_PANEL  0xD0140C08u
#define UI_CLEAR  0x00000000u

/* ui_font.c */
void ui_text(ui_image_t *im, int x, int y, int scale, const char *s, uint32_t rgba);
int ui_text_width(const char *s, int scale);
void ui_rect(ui_image_t *im, int x, int y, int w, int h, uint32_t rgba);
int ui_catchup(int w, int h, int drawn,
               const uint8_t **img); /* ui_badge.c: the joining screen (DRAWN: over the game) */

/* ui_badge.c (the party's screens are the game's own: fe_party.c) */
int ui_key(int scancode, int down);   /* 1: typing a party code took it (the game must not see it) */
int ui_open(void);                    /* (0: nothing of ours takes every key) */
/* ui_camera.c: C in a match cycles this player's own view (runtime/d3d8/d3d8_pov.cpp, drawing only) */
int ui_camera_key(int scancode, int down);
void rt_pov_cycle(void);
const char *rt_pov_name(void);
/* At every Present: 1 if there is something to draw; *img the image (w x h), *changed since the last */
int ui_present(uint32_t frame, int w, int h, const uint8_t **img, int *changed);

/* ui_lobby.c: rooms on the relay (server/), then host-authoritative play (net_ha.c) */
enum { LOBBY_IDLE, LOBBY_WAITING, LOBBY_IN, LOBBY_ERROR };
void ui_lobby_host(void);
void ui_lobby_join(const char *code);
void ui_lobby_leave(void);
void ui_lobby_poll(void);
void ui_lobby_text(const char *json, int n);   /* a lobby message (net_ha.c passes them on) */
int ui_lobby_state(void);
const char *ui_lobby_code(void);
const char *ui_lobby_error(void);
int ui_lobby_members(void);
uint32_t ui_lobby_member_mask(void); /* bit k: room slot k has a member */
int ui_lobby_slot(void);             /* this player's room slot (0 = the host) */
int ui_lobby_ended(void);            /* the relay closed the party: its host left */
int ui_lobby_is_host(void);

#ifdef __cplusplus
}
#endif
#endif
