/* pads.c -- virtual gamepads (plat.h rt_pad_t), the state dinput.c reports for its joysticks. */
#include "rt.h"
#include "plat.h"
#include <stdlib.h>
#include <string.h>

rt_pad_t rt_pads[RT_MAX_PADS];
int rt_npads = -1;
int rt_pads_first;

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
    rt_pad_t *p = rt_net ? rt_net_local_pad() : &rt_pads[pad]; /* netplay: replay pad events are this player's input */
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
        rt_npads = e ? atoi(e) : RT_MAX_PADS;   /* every controller exists from boot: the game enumerates
                                                   them once, and online friends' pads must be among them */
        if (rt_npads > RT_MAX_PADS) rt_npads = RT_MAX_PADS;
    }
    return rt_npads;
}
