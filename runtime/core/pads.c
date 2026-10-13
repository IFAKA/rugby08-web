/* pads.c -- virtual gamepads (plat.h rt_pad_t), the state dinput.c reports for its joysticks. */
#include "rt.h"
#include "plat.h"
#include <stdlib.h>
#include <string.h>
#include "pad_attach.h"

rt_pad_t rt_pads[RT_MAX_PADS];
int rt_npads = -1;
int rt_pads_first;
uint32_t rt_pads_party;
static int g_scripted[RT_MAX_PADS];   /* a replay drove this pad: plugged in from then on */
static int g_gamepad;                 /* this machine's game controller is connected (slot 0) */

static const struct {
    const char *name;
    uint16_t bit;
} k_buttons[] = {
    { "cross", RT_PAD_CROSS },   { "circle", RT_PAD_CIRCLE },
    { "square", RT_PAD_SQUARE }, { "triangle", RT_PAD_TRIANGLE },
    { "l1", RT_PAD_L1 },         { "r1", RT_PAD_R1 },
    { "l2", RT_PAD_L2 },         { "r2", RT_PAD_R2 },
    { "start", RT_PAD_START },   { "select", RT_PAD_SELECT },
    { "l3", RT_PAD_L3 },         { "r3", RT_PAD_R3 },
};

/* Names: the buttons above, d-pad up/down/left/right, left stick lup/ldown/lleft/lright and right
 * stick rup/rdown/rleft/rright (full deflection while held). */
int rt_pad_event(int pad, const char *name, int down) {
    if (pad < 0 || pad >= RT_MAX_PADS) return 0;
    g_scripted[pad] = 1;
    /* a friend (and lockstep netplay): replay pad events are this player's input, sent to the host */
    rt_pad_t *p = rt_net && rt_net_ha != 1 ? rt_net_local_pad() : &rt_pads[pad];
    for (size_t i = 0; i < sizeof k_buttons / sizeof *k_buttons; i++)
        if (!strcmp(name, k_buttons[i].name)) {
            p->buttons = down ? p->buttons | k_buttons[i].bit : p->buttons & ~k_buttons[i].bit;
            return 1;
        }
    static const char *hats[] = { "up", "right", "down", "left" };
    for (int i = 0; i < 4; i++)
        if (!strcmp(name, hats[i])) {
            uint8_t b = (uint8_t)(1 << i);
            p->hat = down ? p->hat | b : p->hat & ~b;
            return 1;
        }
    int16_t full = down ? 32767 : 0;
    if (!strcmp(name, "lup")) {
        p->ly = (int16_t)-full;
        return 1;
    }
    if (!strcmp(name, "ldown")) {
        p->ly = full;
        return 1;
    }
    if (!strcmp(name, "lleft")) {
        p->lx = (int16_t)-full;
        return 1;
    }
    if (!strcmp(name, "lright")) {
        p->lx = full;
        return 1;
    }
    if (!strcmp(name, "rup")) {
        p->ry = (int16_t)-full;
        return 1;
    }
    if (!strcmp(name, "rdown")) {
        p->ry = full;
        return 1;
    }
    if (!strcmp(name, "rleft")) {
        p->rx = (int16_t)-full;
        return 1;
    }
    if (!strcmp(name, "rright")) {
        p->rx = full;
        return 1;
    }
    return 0;
}

int rt_pad_count(void) {
    if (rt_npads < 0) {
        const char *e = getenv("RT_PADS");
        rt_npads = e ? atoi(e) : RT_MAX_PADS; /* every pad slot exists from boot: the game enumerates
                                                   once; whether one is plugged in is rt_pad_attached */
        if (rt_npads > RT_MAX_PADS) rt_npads = RT_MAX_PADS;
    }
    return rt_npads;
}

/* Plugged in: this machine's game controller (slot 0), a friend in the host's party (slot = its room
   slot), a pad a replay drives; RT_PADS=N (reference runs recorded with N pads): all N. */
int rt_pad_attached(int k) {
    if (k < 0 || k >= rt_pad_count()) return 0;
    if (getenv("RT_PADS") || g_scripted[k]) return 1;
    if (rt_pads_mirroring()) return k > 0 && (rt_pads_party >> k & 1);   /* a friend's Team Select: the party */
    if (k == 0) return g_gamepad;
    return rt_net_ha == 1 && (rt_pads_party >> k & 1);   /* (rt_pads_party: the host's members, net_ha.c) */
}

/* Every Present: this machine's game controller drives pad 0 (a host's too: its keyboard is the game's
   keyboard; a friend's game is driven by the host), then the game's controllers follow the slots. */
void rt_pads_present(uint32_t frame) {
    rt_pad_t g;
    int on = plat_gamepad(&g);
    if (on) rt_pads[0] = g;
    else if (g_gamepad) memset(&rt_pads[0], 0, sizeof rt_pads[0]);
    if (on != g_gamepad) rt_trace("ms: pads: game controller %s at frame %u", on ? "connected" : "gone", frame);
    g_gamepad = on;
    rt_pads_sync_game(frame);
}
