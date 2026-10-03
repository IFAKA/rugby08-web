/* d3d8.c -- D3D8 entry point. The IDirect3D8 object (d3d8to9 -> d3d9-webgl, runtime/DESIGN.md)
 * arrives with native.d3d.present; until then creation fails the way a machine without D3D8 does. */
#include "rt.h"

HLE(Direct3DCreate8) {   /* (SDKVersion) */
    rt_trace("Direct3DCreate8(SDK %u) -> NULL (IDirect3D8 not implemented yet)", a[0]);
    return 0;
}
