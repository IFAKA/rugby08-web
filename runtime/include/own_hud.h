/* own_hud.h -- a party: the game's prompts for another team's player left out of this player's picture
 * (own_hud.c). */
#ifndef OWN_HUD_H
#define OWN_HUD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { OWN_HUD_AIM = 1 };   /* a kick's aim arrow */
int own_hud_part(const uint8_t *data, uint32_t len, uint32_t fvf); /* a written static vertex buffer's part, 0 */
int own_hud_hidden(int part);                                      /* this player's picture leaves it out */
void own_hud_event(uint32_t event);   /* every game: a match event the camera heard */
void own_hud_present(void);            /* every Present (the trace of what is left out) */
int own_hud_acting(void);             /* the team acting now (the ball's, else the last event's), -1 */
#ifdef __cplusplus
}
#endif
#endif
