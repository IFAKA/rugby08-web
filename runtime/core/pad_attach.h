/* pad_attach.h -- the game's controllers follow the pad slots (pad_attach.c). */
#pragma once
#include <stdint.h>

void rt_pads_sync_game(uint32_t frame);     /* every Present (pads.c rt_pads_present) */
void rt_pads_mirror(int on, const char *sides); /* a friend's Team Select: the party's sides ([4] keyboard) */
int rt_pads_mirroring(void);
char rt_pads_side(int k);                   /* pad slot k's side ('H', 'A', 0); k = -1: the keyboard */
int rt_dinput_pad_slot(uint32_t di_index);  /* runtime/win32/dinput.c: the pad slot of a device index, or -1 */
