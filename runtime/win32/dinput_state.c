/* dinput_state.c -- GetDeviceState data: DIJOYSTATE of a virtual pad, the 256-byte keyboard state,
 * DIMOUSESTATE (each mouse device reports its own delta since it last read). */
#include "dinput_int.h"

/* DIJOYSTATE: lX lY lZ lRx lRy lRz rglSlider[2] rgdwPOV[4] rgbButtons[32] */
void pad_state(const didev_t *d, uint32_t n, uint32_t p) {
    const rt_pad_t *s = &rt_pads[d->pad];
    int16_t v[6] = { s->lx, s->ly, s->rx, 0, 0, s->ry }; /* Dual Action: right stick on Z / Rz */
    /* The game reads only the sticks (FUN_0081FD80 ignores the POV hat): a d-pad steers the left
       stick while the stick is centred. */
    if (!s->lx && !s->ly && s->hat) {
        v[0] = s->hat & RT_HAT_LEFT ? -32768 : s->hat & RT_HAT_RIGHT ? 32767 : 0;
        v[1] = s->hat & RT_HAT_UP ? -32768 : s->hat & RT_HAT_DOWN ? 32767 : 0;
    }
    for (int i = 0; i < 6 && 4u * (uint32_t)i + 4 <= n; i++) {
        int64_t x = v[i];
        if (d->dz[i] && (x < 0 ? -x : x) * 10000 < (int64_t)d->dz[i] * 32768) x = 0;
        wr32(p + 4u * (uint32_t)i,
             (uint32_t)(d->rmin[i] + (int32_t)((x + 32768) * ((int64_t)d->rmax[i] - d->rmin[i]) / 65535)));
    }
    static const uint32_t hat_deg[16] = {
        0xFFFFFFFFu, 0,     9000,        4500, 18000, 0xFFFFFFFFu, 13500, 9000,
        27000,       31500, 0xFFFFFFFFu, 0,    22500, 27000,       18000, 0xFFFFFFFFu
    };
    if (n >= 48) {
        wr32(p + 32, hat_deg[s->hat & 15]);
        for (int i = 1; i < 4; i++) wr32(p + 32 + 4u * (uint32_t)i, 0xFFFFFFFFu);
    }
    static const uint16_t order[12] = { RT_PAD_SQUARE, RT_PAD_CROSS,  RT_PAD_CIRCLE, RT_PAD_TRIANGLE,
                                        RT_PAD_L1,     RT_PAD_R1,     RT_PAD_L2,     RT_PAD_R2,
                                        RT_PAD_START,  RT_PAD_SELECT, RT_PAD_L3,     RT_PAD_R3 };
    for (uint32_t i = 0; i < 12 && 48 + i < n; i++) wr8(p + 48 + i, s->buttons & order[i] ? 0x80 : 0);
}

void keyboard_state(uint32_t n, uint32_t p) {
    static int seen;
    for (uint32_t k = 0; k < n && k < 256; k++) {
        uint8_t on = plat_dik_down(k);
        wr8(p + k, on ? 0x80 : 0);
        if (on && seen < 8) {
            seen++;
            rt_log("DirectInput keyboard: DIK 0x%02X down", k);
        }
    }
    /* Right Shift also sprints (Space, DIK 0x39). Many keyboards -- Bluetooth ones especially --
       cannot report Space together with two arrow keys: the key press never reaches the host, so
       holding Space while steering with two arrows drops keys. Modifier keys are reported in
       their own byte and never ghost. */
    if (n > 0x39 && plat_dik_down(0x36)) wr8(p + 0x39, 0x80);
}

/* DIMOUSESTATE: lX lY lZ rgbButtons[4] */
void mouse_state(didev_t *d, uint32_t p) {
    mouse_drain();
    int dx = (int)(g_mx - d->mx), dy = (int)(g_my - d->my), dw = (int)(g_mw - d->mw);
    d->mx = g_mx;
    d->my = g_my;
    d->mw = g_mw;
    uint8_t b = plat_mouse_buttons();
    {
        static unsigned polls[8]; /* --log-input: which mouse device the game reads, and how often */
        if (rt_log_input && d->id < 8 && ++polls[d->id] % 300 == 0)
            rt_log("input: DI mouse%d polled %u times", d->id, polls[d->id]);
    }
    {
        static int n, k;
        if (rt_log_input && (dx || dy) && n < 300) {
            n++;
            rt_log("input: DI mouse%d delta %d,%d", d->id, dx, dy);
        }
        if (rt_log_input && (dx || dy) && ++k % 40 == 0)
            rt_log("input: DI mouse%d total %d,%d", d->id, (int)g_mx, (int)g_my);
    }
    wr32(p, (uint32_t)dx);
    wr32(p + 4, (uint32_t)dy);
    wr32(p + 8, (uint32_t)dw);
    for (int i = 0; i < 3; i++) wr8(p + 12 + (uint32_t)i, (b >> i) & 1 ? 0x80 : 0);
}
