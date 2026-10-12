/* advapi32.c -- registry. No keys exist: the game falls back to its defaults (install path =
 * current directory, options from its own files). Every lookup is logged so a needed value can be
 * added deliberately. */
#include "win32.h"

static const char *root_name(uint32_t hk) {
    switch (hk) {
    case 0x80000000u: return "HKCR";
    case 0x80000001u: return "HKCU";
    case 0x80000002u: return "HKLM";
    case 0x80000003u: return "HKU";
    }
    return "HKEY";
}

HLE(RegOpenKeyA) {     /* (hkey, subkey, phkResult) */
    rt_log("RegOpenKeyA %s\\%s -> not found", root_name(a[0]), gstr(a[1]) ? gstr(a[1]) : "");
    if (a[2]) wr32(a[2], 0);
    return ERROR_FILE_NOT_FOUND_;
}

HLE(RegOpenKeyExA) {   /* (hkey, subkey, options, sam, phkResult) */
    rt_log("RegOpenKeyExA %s\\%s -> not found", root_name(a[0]), gstr(a[1]) ? gstr(a[1]) : "");
    if (a[4]) wr32(a[4], 0);
    return ERROR_FILE_NOT_FOUND_;
}

HLE(RegQueryValueExA) {   /* (hkey, name, reserved, type, data, cbData) */
    rt_log("RegQueryValueExA 0x%08X \"%s\" -> not found", a[0], gstr(a[1]) ? gstr(a[1]) : "");
    return ERROR_FILE_NOT_FOUND_;
}

HLE(RegEnumValueA) {
    (void)a;
    return ERROR_NO_MORE_ITEMS_;
}
HLE(RegCloseKey) {
    (void)a;
    return ERROR_SUCCESS_;
}
