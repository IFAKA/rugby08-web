/* dinput.c -- DirectInput 7 (DirectInputCreateA, version 0x700): the system keyboard and mouse on
 * the host's input (runtime/include/plat.h). One vtable per interface family covers the 2A/7A
 * extensions, so QueryInterface hands back the same object for any IID.
 * Immediate data only: GetDeviceData reports an empty buffer. Joysticks are not enumerated yet. */
#include "win32.h"
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

enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2 };
typedef struct { int kind; uint32_t refs; } didev_t;

/* DIDEVICEINSTANCEA (580 bytes): dwSize guidInstance guidProduct dwDevType tszInstanceName[260]
 * tszProductName[260] guidFFDriver wUsagePage wUsage */
static void fill_instance(uint32_t p, int kind) {
    uint32_t size = rd32(p) ? rd32(p) : 580;
    memset(G2H(p), 0, size);
    wr32(p, size);
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
    wr32(p + 8, d->kind == DEV_KEYBOARD ? 0x0403u : 0x0202u);
    wr32(p + 12, d->kind == DEV_MOUSE ? 3 : 0); wr32(p + 16, d->kind == DEV_MOUSE ? 3 : 128);
    return DI_OK;
}

static uint32_t dv_ok(const uint32_t *a) { (void)a; return DI_OK; }
static uint32_t dv_noeffect(const uint32_t *a) { (void)a; return DI_NOEFFECT; }
static uint32_t dv_notimpl(const uint32_t *a) { (void)a; return E_NOTIMPL_; }

static uint32_t dv_GetDeviceState(const uint32_t *a) {   /* (this, cbData, lpvData) */
    didev_t *d = dev(a);
    uint32_t n = a[1], p = a[2];
    memset(G2H(p), 0, n);
    if (d->kind == DEV_KEYBOARD) {
        for (uint32_t k = 0; k < n && k < 256; k++) wr8(p + k, plat_dik_down(k) ? 0x80 : 0);
    } else if (n >= 16) {                                      /* DIMOUSESTATE: lX lY lZ rgbButtons[4] */
        int dx, dy, dw;
        plat_mouse_delta(&dx, &dy, &dw);
        uint8_t b = plat_mouse_buttons();
        wr32(p, (uint32_t)dx); wr32(p + 4, (uint32_t)dy); wr32(p + 8, (uint32_t)dw);
        for (int i = 0; i < 3; i++) wr8(p + 12 + (uint32_t)i, (b >> i) & 1 ? 0x80 : 0);
    }
    return DI_OK;
}

static uint32_t dv_GetDeviceData(const uint32_t *a) {    /* (this, cbObjectData, rgdod, pdwInOut, flags) */
    if (a[3]) wr32(a[3], 0);
    return DI_OK;
}

static uint32_t dv_GetDeviceInfo(const uint32_t *a) { fill_instance(a[1], dev(a)->kind); return DI_OK; }
static uint32_t dv_GetProperty(const uint32_t *a) { (void)a; return DI_OK; }

static const rt_method_t dev_methods[] = {
    { "IDirectInputDevice7A::QueryInterface", dv_QueryInterface, 12 }, { "AddRef", dv_AddRef, 4 }, { "Release", dv_Release, 4 },
    { "GetCapabilities", dv_GetCapabilities, 8 }, { "EnumObjects", dv_ok, 16 }, { "GetProperty", dv_GetProperty, 12 },
    { "SetProperty", dv_ok, 12 }, { "Acquire", dv_ok, 4 }, { "Unacquire", dv_ok, 4 },
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
    static uint32_t vt;
    if (!vt) vt = rt_com_vtable("IDirectInputDevice7A", dev_methods, (int)(sizeof dev_methods / sizeof dev_methods[0]));
    didev_t *d = calloc(1, sizeof *d);
    d->kind = kind; d->refs = 1;
    return rt_com_new(vt, d);
}

/* ---- IDirectInputA / 2A / 7A ------------------------------------------------------------------- */
static uint32_t di_QueryInterface(const uint32_t *a) { wr32(a[2], a[0]); return DI_OK; }
static uint32_t di_AddRef(const uint32_t *a) { (void)a; return 1; }
static uint32_t di_Release(const uint32_t *a) { (void)a; return 0; }

static int guid_kind(uint32_t g) {
    if (!memcmp(G2H(g), GUID_SysKeyboard, 16)) return DEV_KEYBOARD;
    if (!memcmp(G2H(g), GUID_SysMouse, 16)) return DEV_MOUSE;
    return 0;
}

static uint32_t di_CreateDevice(const uint32_t *a) {   /* (this, rguid, lplpDevice, pUnkOuter) */
    int k = guid_kind(a[1]);
    if (!k) { wr32(a[2], 0); return DIERR_DEVICENOTREG; }
    wr32(a[2], new_device(k));
    rt_log("DirectInput CreateDevice(%s)", k == DEV_KEYBOARD ? "keyboard" : "mouse");
    return DI_OK;
}

static uint32_t di_EnumDevices(const uint32_t *a) {    /* (this, dwDevType, callback, pvRef, flags) */
    uint32_t type = a[1] & 0xFF;
    uint32_t inst = gcalloc(580);
    for (int k = DEV_KEYBOARD; k <= DEV_MOUSE; k++) {
        if (type && type != (k == DEV_KEYBOARD ? 3u : 2u)) continue;   /* DIDEVTYPE_KEYBOARD / _MOUSE */
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
