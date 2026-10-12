/* dinput.c -- DirectInput 7 (DirectInputCreateA, version 0x700): the system keyboard and mouse on
 * the host's input (runtime/include/plat.h). One vtable per interface family covers the 2A/7A
 * extensions, so QueryInterface hands back the same object for any IID.
 * Immediate data only: GetDeviceData reports an empty buffer. Joysticks: the virtual pads of
 * runtime/core/pads.c, as "Logitech Dual Action" pads -- a name Data/controltypes.csv maps
 * (*Logitech*DualAction*: 12 buttons, dual analog; Square Cross Circle Triangle L1 R1 L2 R2 Start
 * Select L3 R3 = buttons 1-12), read through c_dfDIJoystick (DIJOYSTATE, 0x91B624). */

#include "dinput_int.h"

/* ---- IDirectInputA / 2A / 7A ------------------------------------------------------------------- */
static uint32_t di_QueryInterface(const uint32_t *a) {
    wr32(a[2], a[0]);
    return DI_OK;
}
static uint32_t di_AddRef(const uint32_t *a) {
    (void)a;
    return 1;
}
static uint32_t di_Release(const uint32_t *a) {
    (void)a;
    return 0;
}

static int guid_kind(uint32_t g) { /* DEV_PAD + n for pad n */
    if (!memcmp(G2H(g), GUID_SysKeyboard, 16)) return DEV_KEYBOARD;
    if (!memcmp(G2H(g), GUID_SysMouse, 16)) return DEV_MOUSE;
    if (!memcmp((const uint8_t *)G2H(g) + 1, GUID_PadBase + 1, 15) && rd8(g) < rt_pad_count()) return DEV_PAD + rd8(g);
    return 0;
}

static uint32_t di_CreateDevice(const uint32_t *a) { /* (this, rguid, lplpDevice, pUnkOuter) */
    int k = guid_kind(a[1]);
    if (!k) {
        wr32(a[2], 0);
        return DIERR_DEVICENOTREG;
    }
    wr32(a[2], new_device(k));
    rt_log("DirectInput CreateDevice(%s)", k == DEV_KEYBOARD ? "keyboard" : k == DEV_MOUSE ? "mouse" : "pad");
    return DI_OK;
}

static uint32_t di_EnumDevices(const uint32_t *a) { /* (this, dwDevType, callback, pvRef, flags) */
    uint32_t type = a[1] & 0xFF;
    uint32_t inst = gcalloc(580);
    /* Windows order (Wine dinput.c enum_devices: mouse, keyboard, joysticks). The game's controller
       list follows it and Team Select only offers entries 1..4 (FUN_0081A5A0): keyboard first made
       it entry 0, never assignable, so only the mouse could take a team (and became the pad). */
    /* Pads follow (joysticks, as on Windows); in a netplay session they come before the keyboard so
       that players 1-4 are entries 1-4. */
    int order[2 + RT_MAX_PADS], n = 0, np = rt_pad_count();
    order[n++] = DEV_MOUSE;
    if (!rt_pads_first) order[n++] = DEV_KEYBOARD;
    for (int i = 0; i < np; i++) order[n++] = DEV_PAD + i;
    if (rt_pads_first) order[n++] = DEV_KEYBOARD;
    for (int j = 0; j < n; j++) {
        int k = order[j];
        if (type && type != (k == DEV_KEYBOARD ? 3u : k == DEV_MOUSE ? 2u : 4u))
            continue; /* DIDEVTYPE_KEYBOARD / _MOUSE / _JOYSTICK */
        wr32(inst, 580);
        fill_instance(inst, k);
        if (guest_call(a[2], 2, inst, a[3]) == 0) break; /* DIENUM_STOP */
    }
    gfree(inst);
    rt_trace("DirectInput EnumDevices type %u: %d devices (%d pads) at frame %u", type, n, np, rt_present_frame);
    return DI_OK;
}

static uint32_t di_GetDeviceStatus(const uint32_t *a) { return guid_kind(a[1]) ? DI_OK : DIERR_DEVICENOTREG; }
static uint32_t di_ok(const uint32_t *a) {
    (void)a;
    return DI_OK;
}
static uint32_t di_FindDevice(const uint32_t *a) {
    (void)a;
    return DIERR_DEVICENOTREG;
}
static uint32_t di_CreateDeviceEx(const uint32_t *a) { /* (this, rguid, riid, pvOut, pUnkOuter) */
    uint32_t b[4] = { a[0], a[1], a[3], a[4] };
    return di_CreateDevice(b);
}

static const rt_method_t di_methods[] = {
    { "IDirectInput7A::QueryInterface", di_QueryInterface, 12 },
    { "AddRef", di_AddRef, 4 },
    { "Release", di_Release, 4 },
    { "CreateDevice", di_CreateDevice, 16 },
    { "EnumDevices", di_EnumDevices, 20 },
    { "GetDeviceStatus", di_GetDeviceStatus, 8 },
    { "RunControlPanel", di_ok, 12 },
    { "Initialize", di_ok, 12 },
    { "FindDevice", di_FindDevice, 16 }, /* IDirectInput2A */
    { "CreateDeviceEx", di_CreateDeviceEx, 20 }, /* IDirectInput7A */
};

HLE(DirectInputCreateA) { /* (hinst, version, lplpDirectInput, pUnkOuter) */
    static uint32_t di;
    if (!di)
        di = rt_com_new(rt_com_vtable("IDirectInput7A", di_methods, (int)(sizeof di_methods / sizeof di_methods[0])),
                        NULL);
    rt_log("DirectInputCreateA(version 0x%X)", a[1]);
    if (a[2]) wr32(a[2], di);
    return DI_OK;
}
