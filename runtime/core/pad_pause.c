/* pad_pause.c -- Start on any controller pauses a match, as on a console. The PC game pauses only on
 * the keyboard's Esc (its pause menu then takes every controller), so a friend in a lockstep party,
 * whose controller is a pad, could not pause. Every game of the party applies the same pads at the
 * same frame (net_frame.c), so every game presses Esc at the same frame. Only in a match (Game
 * [0x00A7ED28] +0x90: BackendState): in the menus Esc is "back"; an open pause menu ignores Esc.
 * Host-authoritative play pauses its own way (net_ha_flow.c). */
#include "rt.h"
#include "plat.h"

#define GAME_PTR   0x00A7ED28u
#define G_STATE    0x90u
#define VT_BACKEND 0x008CC6F0u
#define SC_ESCAPE  41 /* SDL scancode */

static void escape(int down) {
    plat_desk_ev_t e = { PLAT_DESK_KEY, (uint8_t)down, SC_ESCAPE, 0 };
    plat_desk_apply(&e);
}

void rt_pad_pause_frame(uint32_t frame) {
    static uint16_t was[RT_MAX_PADS];
    static uint32_t esc_up;
    if (rt_net_ha) return;
    if (esc_up && frame >= esc_up) escape(0), esc_up = 0;
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + G_STATE) : 0;
    int match = st && rd32(st) == VT_BACKEND;
    for (int k = 0; k < RT_MAX_PADS; k++) {
        uint16_t b = rt_pads[k].buttons;
        if (match && !esc_up && (b & RT_PAD_START) && !(was[k] & RT_PAD_START)) {
            rt_trace("pads: controller of pad %d pressed Start at frame %u: Esc", k, frame);
            escape(1);
            esc_up = frame + 3;   /* held a few frames: the game polls its keyboard once a frame */
        }
        was[k] = b;
    }
}
