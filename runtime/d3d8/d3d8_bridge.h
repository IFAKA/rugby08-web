/* d3d8_bridge.h -- guest D3D8 COM objects backed by d3d9-webgl (runtime/DESIGN.md "Graphics").
 *
 * Every guest interface pointer is {vtbl, host index} (rt_com_new); its vtable comes from
 * gen/runtime/d3d8_vtables.c (tools/gen/gen_d3d8_bridges.py), whose slots bind handlers named
 * d3d8_<Iface>_<Method> (or d3d8_<Base>_<Method>) with the HLE ABI: a[0] = guest `this`,
 * a[k] = k-th argument slot, return value -> eax. Structures are converted field by field: host
 * pointers, HWND and LONG are 64-bit, guest ones are 32-bit. */
#ifndef D3D8_BRIDGE_H
#define D3D8_BRIDGE_H
#include "rt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint16_t pop;
    rt_hle_fn own, inh, stub;
} d3d8_slot_t;

/* Logs "D3D8 UNIMPL <name>" once; returns E_NOTIMPL for HRESULT methods, else 0. */
uint32_t d3d8_unimpl(const char *name, int hresult);

// clang-format off
#define D3D8_IFACES(X) \
    X(IDirect3D8) X(IDirect3DDevice8) X(IDirect3DSwapChain8) X(IDirect3DResource8) \
    X(IDirect3DBaseTexture8) X(IDirect3DTexture8) X(IDirect3DCubeTexture8) X(IDirect3DVolumeTexture8) \
    X(IDirect3DSurface8) X(IDirect3DVolume8) X(IDirect3DVertexBuffer8) X(IDirect3DIndexBuffer8)
// clang-format on
#define D3D8_DECL_VT(I)                                                                                                \
    extern const d3d8_slot_t d3d8_vt_##I[];                                                                            \
    extern const int d3d8_vt_##I##_n;
D3D8_IFACES(D3D8_DECL_VT)
#undef D3D8_DECL_VT

#ifdef __cplusplus
}
#endif
#endif
