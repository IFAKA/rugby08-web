/* own_camera.h -- each player's own camera in a lockstep party (own_camera.c). */
#ifndef OWN_CAMERA_H
#define OWN_CAMERA_H
#include <stdint.h>
#include "plat.h"

#define OWN_CAMERA_SIGN 1.0f   /* which way a stick turns with the camera (measured: tools/net/ls_camera_check.py) */
#define NET_DESK_LOOK   0x40u  /* a desk event in player 1's input: its camera's look, x and z (net_frame.c) */

extern int rt_own_camera;                       /* --own-camera: each player's own camera in a party */
int own_camera_on(void);                        /* on: the state hash is the match's (ls_simhash.c) */
uint64_t ls_simhash(void);                      /* ls_simhash.c */
void own_camera_options(void);                  /* every Present and before the camera hears or steps */
void own_camera_adopt(void);                    /* the options as they are now are this player's (tests) */
void own_camera_before(void);                   /* the camera step begins (match_camera.c) */
void own_camera_after(uint32_t cam);            /* the camera step is done (its LookAtCamera) */
void own_camera_present(void);                  /* after drawing: the match's camera for the match */
int own_camera_look(int16_t *x, int16_t *z);    /* this game's camera's look, quantized; 0: none */
void own_camera_set_ref(int16_t x, int16_t z);  /* player 1's look, from its input */
void own_camera_turn_pad(rt_pad_t *p);          /* a friend's stick into player 1's camera direction */
#endif
