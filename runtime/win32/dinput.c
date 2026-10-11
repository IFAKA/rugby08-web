/* dinput.c -- DirectInput 7 (DirectInputCreateA, version 0x700): the system keyboard and mouse on
 * the host's input (runtime/include/plat.h). One vtable per interface family covers the 2A/7A
 * extensions, so QueryInterface hands back the same object for any IID.
 * Immediate data only: GetDeviceData reports an empty buffer. Joysticks: the virtual pads of
 * runtime/core/pads.c, as "Logitech Dual Action" pads -- a name Data/controltypes.csv maps
 * (*Logitech*DualAction*: 12 buttons, dual analog; Square Cross Circle Triangle L1 R1 L2 R2 Start
 * Select L3 R3 = buttons 1-12), read through c_dfDIJoystick (DIJOYSTATE, 0x91B624). */
#include "win32.h"
#include <math.h>
#include <stdlib.h>
#include "plat.h"
#include <stdlib.h>
#include <string.h>

#define DI_OK 0u
#define DI_NOEFFECT 1u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_DEVICENOTREG 0x80040154u
#define E_NOTIMPL_ 0x80004001u

static const uint8_t GUID_SysKeyboard[16] = { 0x61, 0x2B, 0x1D, 0x6F, 0xA0, 0xD5, 0xCF, 0x11, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
static const uint8_t GUID_SysMouse[16]    = { 0x60, 0x2B, 0x1D, 0x6F, 0xA0, 0xD5, 0xCF, 0x11, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };

enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2, DEV_PAD = 3 };
typedef struct { int kind; uint32_t refs; int id; uint32_t mx, my, mw; int pad; int32_t rmin[6], rmax[6]; uint32_t dz[6]; } didev_t;

/* Pad n: instance GUID = this with byte 0 = n; product = Logitech Dual Action (VID 046D PID C216). */
static const uint8_t GUID_PadBase[16] = { 0x00, 0x7A, 0xD1, 0x52, 0x31, 0x9E, 0x11, 0xEF, 0x80, 0x01, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
static const uint8_t GUID_PadProduct[16] = { 0x6D, 0x04, 0x16, 0xC2, 0, 0, 0, 0, 0, 0, 0x50, 0x49, 0x44, 0x56, 0x49, 0x44 };
static const char PAD_NAME[] = "Logitech DualAction";   /* matches controltypes.csv *Logitech*DualAction* with or without space stripping */
#define DEVTYPE_PAD 0x00010204u   /* DIDEVTYPE_HID | DIDEVTYPEJOYSTICK_GAMEPAD << 8 | DIDEVTYPE_JOYSTICK */

/* Mouse motion since process start. Every device instance reports its own delta against the
 * total it last saw, as real DirectInput does: the game keeps two mouse devices, and draining one
 * shared delta per read starved whichever device polled second. */
static uint32_t g_mx, g_my, g_mw;
static void mouse_drain(void) {
    int dx, dy, dw;
    plat_mouse_delta(&dx, &dy, &dw);
    g_mx += (uint32_t)dx; g_my += (uint32_t)dy; g_mw += (uint32_t)dw;
}

/* DIDEVICEINSTANCEA (580 bytes): dwSize guidInstance guidProduct dwDevType tszInstanceName[260]
 * tszProductName[260] guidFFDriver wUsagePage wUsage */
static void fill_instance(uint32_t p, int kind) {
    uint32_t size = rd32(p) ? rd32(p) : 580;
    memset(G2H(p), 0, size);
    wr32(p, size);
    if (kind >= DEV_PAD) {   /* kind DEV_PAD + n: pad n */
        memcpy(G2H(p + 4), GUID_PadBase, 16); wr8(p + 4, (uint8_t)(kind - DEV_PAD));
        memcpy(G2H(p + 20), GUID_PadProduct, 16);
        wr32(p + 36, DEVTYPE_PAD);
        gstr_put(p + 40, PAD_NAME, 260);
        gstr_put(p + 300, PAD_NAME, 260);
        if (size >= 580) { wr16(p + 576, 1); wr16(p + 578, 5); }   /* HID usage: generic desktop, game pad */
        return;
    }
    const uint8_t *g = kind == DEV_KEYBOARD ? GUID_SysKeyboard : GUID_SysMouse;
    memcpy(G2H(p + 4), g, 16);
    memcpy(G2H(p + 20), g, 16);
    wr32(p + 36, kind == DEV_KEYBOARD ? 0x0403u : 0x0202u);   /* DIDEVTYPE_KEYBOARD/PCENH, DIDEVTYPE_MOUSE/TRADITIONAL */
    const char *name = kind == DEV_KEYBOARD ? "Keyboard" : "Mouse";
    gstr_put(p + 40, name, 260);
    gstr_put(p + 300, name, 260);
    if (size >= 580) { wr16(p + 576, 1); wr16(p + 578, kind == DEV_KEYBOARD ? 6 : 2); }   /* HID usage */
}

/* ---- devices -------------------------------------------------------------------------------- */
static didev_t *dev(const uint32_t *a) { return rt_com_host(a[0]); }

static uint32_t dv_QueryInterface(const uint32_t *a) { dev(a)->refs++; wr32(a[2], a[0]); return DI_OK; }
static uint32_t dv_AddRef(const uint32_t *a) { return ++dev(a)->refs; }
static uint32_t dv_Release(const uint32_t *a) { didev_t *d = dev(a); return d->refs ? --d->refs : 0; }

static uint32_t dv_GetCapabilities(const uint32_t *a) {   /* DIDEVCAPS: dwSize dwFlags dwDevType dwAxes dwButtons dwPOVs ... */
    didev_t *d = dev(a);
    uint32_t p = a[1], size = rd32(p) ? rd32(p) : 44;
    memset(G2H(p), 0, size);
    wr32(p, size); wr32(p + 4, 1);                             /* DIDC_ATTACHED */
    if (d->kind == DEV_PAD) { wr32(p + 8, DEVTYPE_PAD); wr32(p + 12, 4); wr32(p + 16, 12); wr32(p + 20, 1); return DI_OK; }
    wr32(p + 8, d->kind == DEV_KEYBOARD ? 0x0403u : 0x0202u);
    wr32(p + 12, d->kind == DEV_MOUSE ? 3 : 0); wr32(p + 16, d->kind == DEV_MOUSE ? 3 : 128);
    return DI_OK;
}

static uint32_t dv_ok(const uint32_t *a) { (void)a; return DI_OK; }
static uint32_t dv_noeffect(const uint32_t *a) { (void)a; return DI_NOEFFECT; }
static uint32_t dv_notimpl(const uint32_t *a) { (void)a; return E_NOTIMPL_; }

static uint32_t dv_GetDeviceState(const uint32_t *a) {   /* (this, cbData, lpvData) */
    didev_t *d = dev(a);
    static int logged[4];
    if (!logged[d->kind]++) rt_log("DirectInput GetDeviceState(%s, %u bytes) first call, buffer 0x%08X", d->kind == DEV_KEYBOARD ? "keyboard" : d->kind == DEV_MOUSE ? "mouse" : "pad", a[1], a[2]);
    uint32_t n = a[1], p = a[2];
    memset(G2H(p), 0, n);
    if (d->kind == DEV_PAD) {   /* DIJOYSTATE: lX lY lZ lRx lRy lRz rglSlider[2] rgdwPOV[4] rgbButtons[32] */
        const rt_pad_t *s = &rt_pads[d->pad];
        int16_t v[6] = { s->lx, s->ly, s->rx, 0, 0, s->ry };   /* Dual Action: right stick on Z / Rz */
        /* The game reads only the sticks (FUN_0081FD80 ignores the POV hat): a d-pad steers the left
           stick while the stick is centred. */
        if (!s->lx && !s->ly && s->hat) {
            v[0] = s->hat & RT_HAT_LEFT ? -32768 : s->hat & RT_HAT_RIGHT ? 32767 : 0;
            v[1] = s->hat & RT_HAT_UP ? -32768 : s->hat & RT_HAT_DOWN ? 32767 : 0;
        }
        for (int i = 0; i < 6 && 4u * (uint32_t)i + 4 <= n; i++) {
            int64_t x = v[i];
            if (d->dz[i] && (x < 0 ? -x : x) * 10000 < (int64_t)d->dz[i] * 32768) x = 0;
            wr32(p + 4u * (uint32_t)i, (uint32_t)(d->rmin[i] + (int32_t)((x + 32768) * ((int64_t)d->rmax[i] - d->rmin[i]) / 65535)));
        }
        static const uint32_t hat_deg[16] = { 0xFFFFFFFFu, 0, 9000, 4500, 18000, 0xFFFFFFFFu, 13500, 9000,
                                              27000, 31500, 0xFFFFFFFFu, 0, 22500, 27000, 18000, 0xFFFFFFFFu };
        if (n >= 48) { wr32(p + 32, hat_deg[s->hat & 15]); for (int i = 1; i < 4; i++) wr32(p + 32 + 4u * (uint32_t)i, 0xFFFFFFFFu); }
        static const uint16_t order[12] = { RT_PAD_SQUARE, RT_PAD_CROSS, RT_PAD_CIRCLE, RT_PAD_TRIANGLE, RT_PAD_L1, RT_PAD_R1,
                                            RT_PAD_L2, RT_PAD_R2, RT_PAD_START, RT_PAD_SELECT, RT_PAD_L3, RT_PAD_R3 };
        for (uint32_t i = 0; i < 12 && 48 + i < n; i++) wr8(p + 48 + i, s->buttons & order[i] ? 0x80 : 0);
        return DI_OK;
    }
    if (d->kind == DEV_KEYBOARD) {
        static int seen;
        for (uint32_t k = 0; k < n && k < 256; k++) {
            uint8_t on = plat_dik_down(k);
            wr8(p + k, on ? 0x80 : 0);
            if (on && seen < 8) { seen++; rt_log("DirectInput keyboard: DIK 0x%02X down", k); }
        }
        /* Right Shift also sprints (Space, DIK 0x39). Many keyboards -- Bluetooth ones especially --
           cannot report Space together with two arrow keys: the key press never reaches the host, so
           holding Space while steering with two arrows drops keys. Modifier keys are reported in
           their own byte and never ghost. */
        if (n > 0x39 && plat_dik_down(0x36)) wr8(p + 0x39, 0x80);
    } else if (n >= 16) {                                      /* DIMOUSESTATE: lX lY lZ rgbButtons[4] */
        mouse_drain();
        int dx = (int)(g_mx - d->mx), dy = (int)(g_my - d->my), dw = (int)(g_mw - d->mw);
        d->mx = g_mx; d->my = g_my; d->mw = g_mw;
        uint8_t b = plat_mouse_buttons();
        { static unsigned polls[8];       /* --log-input: which mouse device the game reads, and how often */
          if (rt_log_input && d->id < 8 && ++polls[d->id] % 300 == 0) rt_log("input: DI mouse%d polled %u times", d->id, polls[d->id]); }
        { static int n, k;
          if (rt_log_input && (dx || dy) && n < 300) { n++; rt_log("input: DI mouse%d delta %d,%d", d->id, dx, dy); }
          if (rt_log_input && (dx || dy) && ++k % 40 == 0) rt_log("input: DI mouse%d total %d,%d", d->id, (int)g_mx, (int)g_my); }
        wr32(p, (uint32_t)dx); wr32(p + 4, (uint32_t)dy); wr32(p + 8, (uint32_t)dw);
        for (int i = 0; i < 3; i++) wr8(p + 12 + (uint32_t)i, (b >> i) & 1 ? 0x80 : 0);
    }
    return DI_OK;
}

static uint32_t dv_GetDeviceData(const uint32_t *a) {    /* (this, cbObjectData, rgdod, pdwInOut, flags) */
    static int logged;
    if (!logged++) rt_log("DirectInput GetDeviceData(cb %u, n %u, flags 0x%X) first call", a[1], a[3] ? rd32(a[3]) : 0, a[4]);
    if (a[3]) wr32(a[3], 0);
    return DI_OK;
}

static uint32_t dv_GetDeviceInfo(const uint32_t *a) { didev_t *d = dev(a); fill_instance(a[1], d->kind == DEV_PAD ? DEV_PAD + d->pad : d->kind); return DI_OK; }
static uint32_t dv_GetProperty(const uint32_t *a) { (void)a; return DI_OK; }

/* SetProperty(this, rguidProp, pdiph): DIPROP_RANGE (4) and DIPROP_DEADZONE (5) on a pad axis
 * (DIPH_BYOFFSET: dwObj = DIJOYSTATE offset) or every axis (DIPH_DEVICE). */
static uint32_t dv_SetProperty(const uint32_t *a) {
    didev_t *d = dev(a);
    uint32_t prop = a[1], h = a[2];
    if (d->kind != DEV_PAD || (prop != 4 && prop != 5)) return DI_OK;
    uint32_t how = rd32(h + 12), obj = rd32(h + 8);
    for (int i = 0; i < 6; i++) {
        if (how == 1 && obj != 4u * (uint32_t)i) continue;
        if (prop == 4) { d->rmin[i] = (int32_t)rd32(h + 16); d->rmax[i] = (int32_t)rd32(h + 20); }
        else d->dz[i] = rd32(h + 16);
    }
    return DI_OK;
}

/* EnumObjects(this, callback, ref, flags): the pad's X Y Z Rz axes, 12 buttons and a hat.
 * DIDEVICEOBJECTINSTANCEA: dwSize guidType dwOfs dwType dwFlags tszName[260] ... (316 bytes). */
static uint32_t dv_EnumObjects(const uint32_t *a) {
    didev_t *d = dev(a);
    if (d->kind != DEV_PAD) return DI_OK;
    static const uint8_t guid_axis[16] = { 0xE0, 0x02, 0x6D, 0xA3, 0xF3, 0xC9, 0xCF, 0x11, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    struct { uint8_t g0; uint32_t ofs, type; const char *name; } objs[17];
    int n = 0;
    static const uint8_t axis_g[4] = { 0xE0, 0xE1, 0xE2, 0xE5 };   /* X Y Z Rz */
    static const uint32_t axis_ofs[4] = { 0, 4, 8, 20 };
    static const char *axis_name[4] = { "X Axis", "Y Axis", "Z Axis", "Z Rotation" };
    uint32_t want = a[3] & 0xFF;
    for (int i = 0; i < 4; i++) if (!want || (want & 3)) objs[n++] = (typeof(objs[0])){ axis_g[i], axis_ofs[i], 2u | (uint32_t)i << 8, axis_name[i] };
    static const char *bn[12] = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5", "Button 6", "Button 7", "Button 8", "Button 9", "Button 10", "Button 11", "Button 12" };
    for (int i = 0; i < 12; i++) if (!want || (want & 0xC)) objs[n++] = (typeof(objs[0])){ 0xF0, 48u + (uint32_t)i, 4u | (uint32_t)i << 8, bn[i] };
    if (!want || (want & 0x10)) objs[n++] = (typeof(objs[0])){ 0xF2, 32, 0x10u, "Hat Switch" };
    uint32_t inst = gcalloc(316);
    for (int i = 0; i < n; i++) {
        memset(G2H(inst), 0, 316);
        wr32(inst, 316);
        memcpy(G2H(inst + 4), guid_axis, 16); wr8(inst + 4, objs[i].g0);
        wr32(inst + 20, objs[i].ofs); wr32(inst + 24, objs[i].type);
        gstr_put(inst + 32, objs[i].name, 260);
        if (guest_call(a[1], 2, inst, a[2]) == 0) break;   /* DIENUM_STOP */
    }
    gfree(inst);
    return DI_OK;
}

static const rt_method_t dev_methods[] = {
    { "IDirectInputDevice7A::QueryInterface", dv_QueryInterface, 12 }, { "AddRef", dv_AddRef, 4 }, { "Release", dv_Release, 4 },
    { "GetCapabilities", dv_GetCapabilities, 8 }, { "EnumObjects", dv_EnumObjects, 16 }, { "GetProperty", dv_GetProperty, 12 },
    { "SetProperty", dv_SetProperty, 12 }, { "Acquire", dv_ok, 4 }, { "Unacquire", dv_ok, 4 },
    { "GetDeviceState", dv_GetDeviceState, 12 }, { "GetDeviceData", dv_GetDeviceData, 20 }, { "SetDataFormat", dv_ok, 8 },
    { "SetEventNotification", dv_ok, 8 }, { "SetCooperativeLevel", dv_ok, 12 }, { "GetObjectInfo", dv_notimpl, 16 },
    { "GetDeviceInfo", dv_GetDeviceInfo, 8 }, { "RunControlPanel", dv_ok, 12 }, { "Initialize", dv_ok, 16 },
    /* IDirectInputDevice2A */
    { "CreateEffect", dv_notimpl, 20 }, { "EnumEffects", dv_ok, 16 }, { "GetEffectInfo", dv_notimpl, 12 },
    { "GetForceFeedbackState", dv_notimpl, 8 }, { "SendForceFeedbackCommand", dv_notimpl, 8 },
    { "EnumCreatedEffectObjects", dv_ok, 16 }, { "Escape", dv_notimpl, 8 }, { "Poll", dv_noeffect, 4 },
    { "SendDeviceData", dv_notimpl, 20 },
    /* IDirectInputDevice7A */
    { "EnumEffectsInFile", dv_notimpl, 20 }, { "WriteEffectToFile", dv_notimpl, 20 },
};

static uint32_t new_device(int kind) {
    if (kind >= DEV_PAD) {
        static uint32_t vt;
        if (!vt) vt = rt_com_vtable("IDirectInputDevice7A", dev_methods, (int)(sizeof dev_methods / sizeof dev_methods[0]));
        didev_t *d = calloc(1, sizeof *d);
        d->kind = DEV_PAD; d->refs = 1; d->pad = kind - DEV_PAD;
        for (int i = 0; i < 6; i++) { d->rmin[i] = 0; d->rmax[i] = 65535; }
        return rt_com_new(vt, d);
    }
    static uint32_t vt;
    if (!vt) vt = rt_com_vtable("IDirectInputDevice7A", dev_methods, (int)(sizeof dev_methods / sizeof dev_methods[0]));
    didev_t *d = calloc(1, sizeof *d);
    static int ids;
    d->kind = kind; d->refs = 1; d->id = ++ids;
    if (kind == DEV_MOUSE) { mouse_drain(); d->mx = g_mx; d->my = g_my; d->mw = g_mw; }   /* no motion from before creation */
    return rt_com_new(vt, d);
}

/* ---- IDirectInputA / 2A / 7A ------------------------------------------------------------------- */
static uint32_t di_QueryInterface(const uint32_t *a) { wr32(a[2], a[0]); return DI_OK; }
static uint32_t di_AddRef(const uint32_t *a) { (void)a; return 1; }
static uint32_t di_Release(const uint32_t *a) { (void)a; return 0; }

static int guid_kind(uint32_t g) {   /* DEV_PAD + n for pad n */
    if (!memcmp(G2H(g), GUID_SysKeyboard, 16)) return DEV_KEYBOARD;
    if (!memcmp(G2H(g), GUID_SysMouse, 16)) return DEV_MOUSE;
    if (!memcmp((const uint8_t *)G2H(g) + 1, GUID_PadBase + 1, 15) && rd8(g) < rt_pad_count()) return DEV_PAD + rd8(g);
    return 0;
}

static uint32_t di_CreateDevice(const uint32_t *a) {   /* (this, rguid, lplpDevice, pUnkOuter) */
    int k = guid_kind(a[1]);
    if (!k) { wr32(a[2], 0); return DIERR_DEVICENOTREG; }
    wr32(a[2], new_device(k));
    rt_log("DirectInput CreateDevice(%s)", k == DEV_KEYBOARD ? "keyboard" : k == DEV_MOUSE ? "mouse" : "pad");
    return DI_OK;
}

static uint32_t di_EnumDevices(const uint32_t *a) {    /* (this, dwDevType, callback, pvRef, flags) */
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
        if (type && type != (k == DEV_KEYBOARD ? 3u : k == DEV_MOUSE ? 2u : 4u)) continue;   /* DIDEVTYPE_KEYBOARD / _MOUSE / _JOYSTICK */
        wr32(inst, 580);
        fill_instance(inst, k);
        if (guest_call(a[2], 2, inst, a[3]) == 0) break;              /* DIENUM_STOP */
    }
    gfree(inst);
    return DI_OK;
}

static uint32_t di_GetDeviceStatus(const uint32_t *a) { return guid_kind(a[1]) ? DI_OK : DIERR_DEVICENOTREG; }
static uint32_t di_ok(const uint32_t *a) { (void)a; return DI_OK; }
static uint32_t di_FindDevice(const uint32_t *a) { (void)a; return DIERR_DEVICENOTREG; }
static uint32_t di_CreateDeviceEx(const uint32_t *a) {  /* (this, rguid, riid, pvOut, pUnkOuter) */
    uint32_t b[4] = { a[0], a[1], a[3], a[4] };
    return di_CreateDevice(b);
}

static const rt_method_t di_methods[] = {
    { "IDirectInput7A::QueryInterface", di_QueryInterface, 12 }, { "AddRef", di_AddRef, 4 }, { "Release", di_Release, 4 },
    { "CreateDevice", di_CreateDevice, 16 }, { "EnumDevices", di_EnumDevices, 20 }, { "GetDeviceStatus", di_GetDeviceStatus, 8 },
    { "RunControlPanel", di_ok, 12 }, { "Initialize", di_ok, 12 },
    { "FindDevice", di_FindDevice, 16 },                                                   /* IDirectInput2A */
    { "CreateDeviceEx", di_CreateDeviceEx, 20 },                                           /* IDirectInput7A */
};

HLE(DirectInputCreateA) {   /* (hinst, version, lplpDirectInput, pUnkOuter) */
    static uint32_t di;
    if (!di) di = rt_com_new(rt_com_vtable("IDirectInput7A", di_methods, (int)(sizeof di_methods / sizeof di_methods[0])), NULL);
    rt_log("DirectInputCreateA(version 0x%X)", a[1]);
    if (a[2]) wr32(a[2], di);
    return DI_OK;
}
