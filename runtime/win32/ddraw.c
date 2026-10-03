/* ddraw.c -- DirectDraw. The game creates a DirectDraw object only to query the adapter; reporting
 * failure (as on a machine without DirectDraw) keeps it on its D3D8 path. */
#include "win32.h"

HLE(DirectDrawCreate) {   /* (lpGUID, lplpDD, pUnkOuter) */
    rt_log("DirectDrawCreate -> DDERR_NODIRECTDRAWSUPPORT");
    if (a[1]) wr32(a[1], 0);
    return 0x887600FFu;   /* DDERR_NODIRECTDRAWSUPPORT */
}
