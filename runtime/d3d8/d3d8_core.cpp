// d3d8_core.cpp -- guest D3D8 object model, IUnknown, IDirect3D8, device creation and Present.
// Conversion rules follow third_party/d3d8to9 (the reference for D3D8 -> D3D9 semantics).
#include "d3d8_int.hpp"
#include <array>
#include <algorithm>
#include <vector>
#include "plat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <pthread.h>

extern "C" int user32_bind_host(uint32_t hwnd);   // runtime/win32/user32.c

// ---- objects -----------------------------------------------------------------------------------
static std::map<const d3d8_slot_t *, uint32_t> g_vtables;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static uint32_t vtable(const d3d8_slot_t *vt, int n) {
    pthread_mutex_lock(&g_mu);
    auto it = g_vtables.find(vt);
    uint32_t va = it != g_vtables.end() ? it->second : 0;
    if (!va) {
        rt_method_t *m = (rt_method_t *)calloc((size_t)n, sizeof *m);
        for (int i = 0; i < n; i++)
            m[i] = { vt[i].name, vt[i].own ? vt[i].own : vt[i].inh ? vt[i].inh : vt[i].stub, vt[i].pop };
        va = rt_com_vtable(vt[0].name, m, n);   // the bridges keep m's names; m stays allocated
        for (int i = 0; i < n; i++) {           // everything but the calls that can sleep or wait
            const char *nm = vt[i].name ? vt[i].name : "";
            if (!strstr(nm, "Present") && !strstr(nm, "Reset") && !strstr(nm, "CreateDevice") &&
                !strstr(nm, "TestCooperative"))
                rt_host_bridge_keep_lock(rd32(va + 4u * (uint32_t)i));
        }
        g_vtables[vt] = va;
    }
    pthread_mutex_unlock(&g_mu);
    return va;
}

uint32_t d3d8_new(Obj *o) {
    const d3d8_slot_t *vt = nullptr;
    int n = 0;
    switch (o->kind) {
        // clang-format off
#define K(kind, I) case kind: vt = d3d8_vt_##I; n = d3d8_vt_##I##_n; break;
        K(K_D3D, IDirect3D8)
        K(K_DEVICE, IDirect3DDevice8)
        K(K_TEXTURE, IDirect3DTexture8)
        K(K_CUBE, IDirect3DCubeTexture8)
        K(K_VOLTEX, IDirect3DVolumeTexture8)
        K(K_SURFACE, IDirect3DSurface8)
        K(K_VOLUME, IDirect3DVolume8)
        K(K_VB, IDirect3DVertexBuffer8)
        K(K_IB, IDirect3DIndexBuffer8)
        K(K_SWAP, IDirect3DSwapChain8)
        // clang-format on
#undef K
    }
    o->refs = 1;
    o->guest = rt_com_new(vtable(vt, n), o);
    return o->guest;
}

Obj *d3d8_obj(uint32_t guest) { return guest ? (Obj *)rt_com_host(guest) : nullptr; }

uint32_t d3d8_unimpl(const char *name, int hresult) {
    static std::map<const char *, int> seen;
    pthread_mutex_lock(&g_mu);
    int first = seen[name]++ == 0;
    pthread_mutex_unlock(&g_mu);
    if (first) rt_log("D3D8 UNIMPL %s", name);
    return hresult ? (uint32_t)E_NOTIMPL_ : 0;
}

// Releasing the last reference frees host state; the 16-byte guest object stays, so a stale guest
// pointer reaches a dead Obj (logged) instead of reused memory.
static void destroy(Obj *o) {
    if (o->host && o->owns) o->host->Release();
    o->host = nullptr;
    if (o->shadow) gfree(o->shadow);
    for (uint32_t s : o->lvl_shadow)
        if (s) gfree(s);
    o->shadow = 0;
    o->lvl_shadow.clear();
    o->dead = true;
    rt_com_free(o->guest);
}

extern "C" {

HLE_D3D(IUnknown, QueryInterface) {   // (this, riid, ppv): no aggregation or extra interfaces
    if (a[2]) wr32(a[2], 0);
    rt_log("D3D8 QueryInterface on kind %d -> E_NOINTERFACE", d3d8_obj(a[0]) ? d3d8_obj(a[0])->kind : -1);
    return E_NOINTERFACE_;
}

HLE_D3D(IUnknown, AddRef) {
    Obj *o = d3d8_obj(a[0]);
    return o ? ++o->refs : 0;
}

HLE_D3D(IUnknown, Release) {
    Obj *o = d3d8_obj(a[0]);
    if (!o || o->dead) {
        rt_log("D3D8 Release of dead object 0x%08X", a[0]);
        return 0;
    }
    if (--o->refs == 0) {
        if (o->kind == K_SURFACE && o->parent) {          // a level surface keeps its texture alive
            Obj *p = o->parent;
            o->parent = nullptr;
            if (o->level < p->lvl_surf.size()) p->lvl_surf[o->level] = 0;
            uint32_t g = p->guest;
            d3d8_IUnknown_Release(&g);
        }
        destroy(o);
    }
    return o->refs;
}

// ---- IDirect3D8 ------------------------------------------------------------------------------
static IDirect3D9 *g_d3d9;

HLE(Direct3DCreate8) {
    rt_trace("Direct3DCreate8(SDK %u)", a[0]);
    if (!g_d3d9) g_d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    Obj *o = new Obj(K_D3D);
    o->host = nullptr;                                     // g_d3d9 is a process singleton
    return d3d8_new(o);
}

HLE_D3D(IDirect3D8, RegisterSoftwareDevice) {
    (void)a;
    return E_NOTIMPL_;
}
HLE_D3D(IDirect3D8, GetAdapterCount) {
    (void)a;
    return 1;
}
HLE_D3D(IDirect3D8, GetAdapterMonitor) {
    (void)a;
    return 0x00F50001u;
}

// D3DPRESENT_PARAMETERS (D3D8, 52 bytes): W H Format Count MultiSample SwapEffect hWnd Windowed
// AutoDepth AutoDepthFormat Flags RefreshRate PresentationInterval
void d3d8_present_params(uint32_t p, D3DPRESENT_PARAMETERS *pp) {
    memset(pp, 0, sizeof *pp);
    pp->BackBufferWidth = rd32(p);
    pp->BackBufferHeight = rd32(p + 4);
    pp->BackBufferFormat = (D3DFORMAT)rd32(p + 8);
    pp->BackBufferCount = rd32(p + 12);
    pp->MultiSampleType = (D3DMULTISAMPLE_TYPE)rd32(p + 16);
    pp->SwapEffect = (D3DSWAPEFFECT)(rd32(p + 20) == 4 ? 1 : rd32(p + 20)); // COPY_VSYNC -> DISCARD
    pp->hDeviceWindow = plat_window_handle();
    pp->Windowed = (BOOL)rd32(p + 28);
    pp->EnableAutoDepthStencil = (BOOL)rd32(p + 32);
    pp->AutoDepthStencilFormat = (D3DFORMAT)rd32(p + 36);
    pp->Flags = rd32(p + 40);
    pp->FullScreen_RefreshRateInHz = rd32(p + 44);
    pp->PresentationInterval = rd32(p + 48);
    if (!pp->BackBufferWidth || !pp->BackBufferHeight) { // windowed: the client area
        int w, h;
        plat_window_rect(nullptr, nullptr, &w, &h);
        pp->BackBufferWidth = (UINT)w;
        pp->BackBufferHeight = (UINT)h;
    }
}

HLE_D3D(IDirect3D8, CreateDevice) { // (this, adapter, devtype, hFocus, behavior, pp, ppDevice)
    if (a[1] != 0 || !a[5] || !a[6]) return D3DERR_INVALIDCALL_;
    D3DPRESENT_PARAMETERS pp;
    d3d8_present_params(a[5], &pp);
    rt_trace("IDirect3D8::CreateDevice %ux%u fmt %u windowed %d depth %d/%u behavior 0x%X", pp.BackBufferWidth,
             pp.BackBufferHeight, pp.BackBufferFormat, pp.Windowed, pp.EnableAutoDepthStencil,
             pp.AutoDepthStencilFormat, a[4]);
    uint32_t hwnd = rd32(a[5] + 24) ? rd32(a[5] + 24) : a[3]; // hDeviceWindow, else the focus window
    if (user32_bind_host(hwnd) != 0 && !plat_window_is_open())
        plat_window_open("Rugby 08", 0, 0, (int)pp.BackBufferWidth, (int)pp.BackBufferHeight);
    IDirect3DDevice9 *dev9 = nullptr;
    if (g_d3d9->CreateDevice(0, D3DDEVTYPE_HAL, plat_window_handle(), a[4], &pp, &dev9) != D3D_OK || !dev9) {
        rt_log("CreateDevice: d3d9-webgl failed");
        wr32(a[6], 0);
        return D3DERR_NOTAVAILABLE_;
    }
    Obj *o = new Obj(K_DEVICE);
    o->host = dev9;
    o->w = pp.BackBufferWidth;
    o->h = pp.BackBufferHeight;
    o->fmt = pp.BackBufferFormat;
    o->d3d = a[0];
    o->behavior = a[4];
    d3d8_device_init(o, a[3]);
    wr32(a[6], d3d8_new(o));
    return D3D_OK_;
}

} // extern "C"
