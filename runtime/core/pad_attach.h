/* pad_attach.h -- the game's controllers follow the pad slots (pad_attach.c). */
#pragma once
#include <stdint.h>

void rt_pads_sync_game(uint32_t frame);     /* every Present (pads.c rt_pads_present) */
int rt_dinput_pad_slot(uint32_t di_index);  /* runtime/win32/dinput.c: the pad slot of a device index, or -1 */
