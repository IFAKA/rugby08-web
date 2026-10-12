/* dinput_device.c -- IDirectInputDevice (2A/7A): the system keyboard and mouse on the host's input
 * (plat.h) and the virtual pads of runtime/core/pads.c; immediate data only. */
#include "dinput_int.h"

/* Mouse motion since process start. Every device instance reports its own delta against the
 * total it last saw, as real DirectInput does: the game keeps two mouse devices, and draining one
 * shared delta per read starved whichever device polled second. */
uint32_t g_mx, g_my, g_mw;
void mouse_drain(void) {
    int dx, dy, dw;
    plat_mouse_delta(&dx, &dy, &dw);
    g_mx += (uint32_t)dx;
    g_my += (uint32_t)dy;
    g_mw += (uint32_t)dw;
}

/* DIDEVICEINSTANCEA (580 bytes): dwSize guidInstance guidProduct dwDevType tszInstanceName[260]
 * tszProductName[260] guidFFDriver wUsagePage wUsage */
void fill_instance(uint32_t p, int kind) {
    uint32_t size = rd32(p) ? rd32(p) : 580;
    memset(G2H(p), 0, size);
    wr32(p, size);
    if (kind >= DEV_PAD) {   /* kind DEV_PAD + n: pad n */
        memcpy(G2H(p + 4), GUID_PadBase, 16);
        wr8(p + 4, (uint8_t)(kind - DEV_PAD));
        memcpy(G2H(p + 20), GUID_PadProduct, 16);
        wr32(p + 36, DEVTYPE_PAD);
        gstr_put(p + 40, PAD_NAME, 260);
        gstr_put(p + 300, PAD_NAME, 260);
        if (size >= 580) {
            wr16(p + 576, 1);
            wr16(p + 578, 5);
        }   /* HID usage: generic desktop, game pad */
        return;
    }
    const uint8_t *g = kind == DEV_KEYBOARD ? GUID_SysKeyboard : GUID_SysMouse;
    memcpy(G2H(p + 4), g, 16);
    memcpy(G2H(p + 20), g, 16);
    wr32(p + 36, kind == DEV_KEYBOARD ? 0x0403u : 0x0202u); /* DIDEVTYPE_KEYBOARD/PCENH, DIDEVTYPE_MOUSE/TRADITIONAL */
    const char *name = kind == DEV_KEYBOARD ? "Keyboard" : "Mouse";
    gstr_put(p + 40, name, 260);
    gstr_put(p + 300, name, 260);
    if (size >= 580) {
        wr16(p + 576, 1);
        wr16(p + 578, kind == DEV_KEYBOARD ? 6 : 2);
    } /* HID usage */
}

/* ---- devices -------------------------------------------------------------------------------- */
static didev_t *dev(const uint32_t *a) { return rt_com_host(a[0]); }

static uint32_t dv_QueryInterface(const uint32_t *a) {
    dev(a)->refs++;
    wr32(a[2], a[0]);
    return DI_OK;
}
static uint32_t dv_AddRef(const uint32_t *a) { return ++dev(a)->refs; }
static uint32_t dv_Release(const uint32_t *a) {
    didev_t *d = dev(a);
    return d->refs ? --d->refs : 0;
}

static uint32_t
dv_GetCapabilities(const uint32_t *a) { /* DIDEVCAPS: dwSize dwFlags dwDevType dwAxes dwButtons dwPOVs ... */
    didev_t *d = dev(a);
    uint32_t p = a[1], size = rd32(p) ? rd32(p) : 44;
    memset(G2H(p), 0, size);
    wr32(p, size);
    wr32(p + 4, 1); /* DIDC_ATTACHED */
    if (d->kind == DEV_PAD) {
        wr32(p + 8, DEVTYPE_PAD);
        wr32(p + 12, 4);
        wr32(p + 16, 12);
        wr32(p + 20, 1);
        return DI_OK;
    }
    wr32(p + 8, d->kind == DEV_KEYBOARD ? 0x0403u : 0x0202u);
    wr32(p + 12, d->kind == DEV_MOUSE ? 3 : 0);
    wr32(p + 16, d->kind == DEV_MOUSE ? 3 : 128);
    return DI_OK;
}

static uint32_t dv_ok(const uint32_t *a) {
    (void)a;
    return DI_OK;
}
static uint32_t dv_noeffect(const uint32_t *a) {
    (void)a;
    return DI_NOEFFECT;
}
static uint32_t dv_notimpl(const uint32_t *a) {
    (void)a;
    return E_NOTIMPL_;
}

static uint32_t dv_GetDeviceState(const uint32_t *a) { /* (this, cbData, lpvData) */
    didev_t *d = dev(a);
    static int logged[4];
    if (!logged[d->kind]++)
        rt_log("DirectInput GetDeviceState(%s, %u bytes) first call, buffer 0x%08X",
               d->kind == DEV_KEYBOARD ? "keyboard"
               : d->kind == DEV_MOUSE  ? "mouse"
                                       : "pad",
               a[1], a[2]);
    uint32_t n = a[1], p = a[2];
    memset(G2H(p), 0, n);
    if (d->kind == DEV_PAD) pad_state(d, n, p);
    else if (d->kind == DEV_KEYBOARD) keyboard_state(n, p);
    else if (n >= 16) mouse_state(d, p);
    return DI_OK;
}

static uint32_t dv_GetDeviceData(const uint32_t *a) { /* (this, cbObjectData, rgdod, pdwInOut, flags) */
    static int logged;
    if (!logged++)
        rt_log("DirectInput GetDeviceData(cb %u, n %u, flags 0x%X) first call", a[1], a[3] ? rd32(a[3]) : 0, a[4]);
    if (a[3]) wr32(a[3], 0);
    return DI_OK;
}

static uint32_t dv_GetDeviceInfo(const uint32_t *a) {
    didev_t *d = dev(a);
    fill_instance(a[1], d->kind == DEV_PAD ? DEV_PAD + d->pad : d->kind);
    return DI_OK;
}
static uint32_t dv_GetProperty(const uint32_t *a) {
    (void)a;
    return DI_OK;
}

/* SetProperty(this, rguidProp, pdiph): DIPROP_RANGE (4) and DIPROP_DEADZONE (5) on a pad axis
 * (DIPH_BYOFFSET: dwObj = DIJOYSTATE offset) or every axis (DIPH_DEVICE). */
static uint32_t dv_SetProperty(const uint32_t *a) {
    didev_t *d = dev(a);
    uint32_t prop = a[1], h = a[2];
    if (d->kind != DEV_PAD || (prop != 4 && prop != 5)) return DI_OK;
    uint32_t how = rd32(h + 12), obj = rd32(h + 8);
    for (int i = 0; i < 6; i++) {
        if (how == 1 && obj != 4u * (uint32_t)i) continue;
        if (prop == 4) {
            d->rmin[i] = (int32_t)rd32(h + 16);
            d->rmax[i] = (int32_t)rd32(h + 20);
        } else d->dz[i] = rd32(h + 16);
    }
    return DI_OK;
}

/* EnumObjects(this, callback, ref, flags): the pad's X Y Z Rz axes, 12 buttons and a hat.
 * DIDEVICEOBJECTINSTANCEA: dwSize guidType dwOfs dwType dwFlags tszName[260] ... (316 bytes). */
static uint32_t dv_EnumObjects(const uint32_t *a) {
    didev_t *d = dev(a);
    if (d->kind != DEV_PAD) return DI_OK;
    static const uint8_t guid_axis[16] = { 0xE0, 0x02, 0x6D, 0xA3, 0xF3, 0xC9, 0xCF, 0x11,
                                           0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
    struct {
        uint8_t g0;
        uint32_t ofs, type;
        const char *name;
    } objs[17];
    int n = 0;
    static const uint8_t axis_g[4] = { 0xE0, 0xE1, 0xE2, 0xE5 }; /* X Y Z Rz */
    static const uint32_t axis_ofs[4] = { 0, 4, 8, 20 };
    static const char *axis_name[4] = { "X Axis", "Y Axis", "Z Axis", "Z Rotation" };
    uint32_t want = a[3] & 0xFF;
    for (int i = 0; i < 4; i++)
        if (!want || (want & 3))
            objs[n++] = (typeof(objs[0])){ axis_g[i], axis_ofs[i], 2u | (uint32_t)i << 8, axis_name[i] };
    static const char *bn[12] = { "Button 1", "Button 2", "Button 3", "Button 4",  "Button 5",  "Button 6",
                                  "Button 7", "Button 8", "Button 9", "Button 10", "Button 11", "Button 12" };
    for (int i = 0; i < 12; i++)
        if (!want || (want & 0xC))
            objs[n++] = (typeof(objs[0])){ 0xF0, 48u + (uint32_t)i, 4u | (uint32_t)i << 8, bn[i] };
    if (!want || (want & 0x10)) objs[n++] = (typeof(objs[0])){ 0xF2, 32, 0x10u, "Hat Switch" };
    uint32_t inst = gcalloc(316);
    for (int i = 0; i < n; i++) {
        memset(G2H(inst), 0, 316);
        wr32(inst, 316);
        memcpy(G2H(inst + 4), guid_axis, 16);
        wr8(inst + 4, objs[i].g0);
        wr32(inst + 20, objs[i].ofs);
        wr32(inst + 24, objs[i].type);
        gstr_put(inst + 32, objs[i].name, 260);
        if (guest_call(a[1], 2, inst, a[2]) == 0) break; /* DIENUM_STOP */
    }
    gfree(inst);
    return DI_OK;
}

static const rt_method_t dev_methods[] = {
    { "IDirectInputDevice7A::QueryInterface", dv_QueryInterface, 12 },
    { "AddRef", dv_AddRef, 4 },
    { "Release", dv_Release, 4 },
    { "GetCapabilities", dv_GetCapabilities, 8 },
    { "EnumObjects", dv_EnumObjects, 16 },
    { "GetProperty", dv_GetProperty, 12 },
    { "SetProperty", dv_SetProperty, 12 },
    { "Acquire", dv_ok, 4 },
    { "Unacquire", dv_ok, 4 },
    { "GetDeviceState", dv_GetDeviceState, 12 },
    { "GetDeviceData", dv_GetDeviceData, 20 },
    { "SetDataFormat", dv_ok, 8 },
    { "SetEventNotification", dv_ok, 8 },
    { "SetCooperativeLevel", dv_ok, 12 },
    { "GetObjectInfo", dv_notimpl, 16 },
    { "GetDeviceInfo", dv_GetDeviceInfo, 8 },
    { "RunControlPanel", dv_ok, 12 },
    { "Initialize", dv_ok, 16 },
    /* IDirectInputDevice2A */
    { "CreateEffect", dv_notimpl, 20 },
    { "EnumEffects", dv_ok, 16 },
    { "GetEffectInfo", dv_notimpl, 12 },
    { "GetForceFeedbackState", dv_notimpl, 8 },
    { "SendForceFeedbackCommand", dv_notimpl, 8 },
    { "EnumCreatedEffectObjects", dv_ok, 16 },
    { "Escape", dv_notimpl, 8 },
    { "Poll", dv_noeffect, 4 },
    { "SendDeviceData", dv_notimpl, 20 },
    /* IDirectInputDevice7A */
    { "EnumEffectsInFile", dv_notimpl, 20 },
    { "WriteEffectToFile", dv_notimpl, 20 },
};

uint32_t new_device(int kind) {
    if (kind >= DEV_PAD) {
        static uint32_t vt;
        if (!vt)
            vt = rt_com_vtable("IDirectInputDevice7A", dev_methods, (int)(sizeof dev_methods / sizeof dev_methods[0]));
        didev_t *d = calloc(1, sizeof *d);
        d->kind = DEV_PAD;
        d->refs = 1;
        d->pad = kind - DEV_PAD;
        for (int i = 0; i < 6; i++) {
            d->rmin[i] = 0;
            d->rmax[i] = 65535;
        }
        return rt_com_new(vt, d);
    }
    static uint32_t vt;
    if (!vt) vt = rt_com_vtable("IDirectInputDevice7A", dev_methods, (int)(sizeof dev_methods / sizeof dev_methods[0]));
    didev_t *d = calloc(1, sizeof *d);
    static int ids;
    d->kind = kind;
    d->refs = 1;
    d->id = ++ids;
    if (kind == DEV_MOUSE) {
        mouse_drain();
        d->mx = g_mx;
        d->my = g_my;
        d->mw = g_mw;
    } /* no motion from before creation */
    return rt_com_new(vt, d);
}
