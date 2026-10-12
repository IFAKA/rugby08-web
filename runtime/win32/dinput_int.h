/* dinput_int.h -- what dinput.c (IDirectInput) and dinput_device.c (the devices) share. */
#ifndef DINPUT_INT_H
#define DINPUT_INT_H
#include "win32.h"
#include "plat.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DI_OK              0u
#define DI_NOEFFECT        1u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_DEVICENOTREG 0x80040154u
#define E_NOTIMPL_         0x80004001u

static const uint8_t GUID_SysKeyboard[16] = { 0x61, 0x2B, 0x1D, 0x6F, 0xA0, 0xD5, 0xCF, 0x11,
                                              0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
static const uint8_t GUID_SysMouse[16] = { 0x60, 0x2B, 0x1D, 0x6F, 0xA0, 0xD5, 0xCF, 0x11,
                                           0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };

enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2, DEV_PAD = 3 };
typedef struct {
    int kind;
    uint32_t refs;
    int id;
    uint32_t mx, my, mw;
    int pad;
    int32_t rmin[6], rmax[6];
    uint32_t dz[6];
} didev_t;

/* Pad n: instance GUID = this with byte 0 = n; product = Logitech Dual Action (VID 046D PID C216). */
static const uint8_t GUID_PadBase[16] = { 0x00, 0x7A, 0xD1, 0x52, 0x31, 0x9E, 0x11, 0xEF,
                                          0x80, 0x01, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
static const uint8_t GUID_PadProduct[16] = { 0x6D, 0x04, 0x16, 0xC2, 0,    0,    0,    0,
                                             0,    0,    0x50, 0x49, 0x44, 0x56, 0x49, 0x44 };
static const char PAD_NAME[] =
    "Logitech DualAction";   /* matches controltypes.csv *Logitech*DualAction* with or without space stripping */
#define DEVTYPE_PAD 0x00010204u   /* DIDEVTYPE_HID | DIDEVTYPEJOYSTICK_GAMEPAD << 8 | DIDEVTYPE_JOYSTICK */

extern uint32_t g_mx, g_my, g_mw;           /* mouse motion since process start */
void mouse_drain(void);
void fill_instance(uint32_t p, int kind);   /* DIDEVICEINSTANCEA of a device kind */
uint32_t new_device(int kind);              /* a guest IDirectInputDevice */
void pad_state(const didev_t *d, uint32_t n, uint32_t p);   /* dinput_state.c: GetDeviceState data */
void keyboard_state(uint32_t n, uint32_t p);
void mouse_state(didev_t *d, uint32_t p);

#endif
