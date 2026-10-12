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
#define K(kind, I)                                                                                                     \
    case kind:                                                                                                         \
        vt = d3d8_vt_##I;                                                                                              \
        n = d3d8_vt_##I##_n;                                                                                           \
        break;
        K(K_D3D, IDirect3D8)
        K(K_DEVICE, IDirect3DDevice8) K(K_TEXTURE, IDirect3DTexture8) K(K_CUBE, IDirect3DCubeTexture8)
            K(K_VOLTEX, IDirect3DVolumeTexture8) K(K_SURFACE, IDirect3DSurface8) K(K_VOLUME, IDirect3DVolume8)
                K(K_VB, IDirect3DVertexBuffer8) K(K_IB, IDirect3DIndexBuffer8) K(K_SWAP, IDirect3DSwapChain8)
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

HLE_D3D(IDirect3D8, GetAdapterIdentifier) {   // (this, adapter, flags, D3DADAPTER_IDENTIFIER8*)
    if (a[1] != 0) return D3DERR_INVALIDCALL_;
    uint32_t p = a[3];                                     // Driver[512] Description[512] DriverVersion(8)
    memset(G2H(p), 0, 1068);                               // VendorId DeviceId SubSysId Revision GUID WHQLLevel
    gstr_put(p, "rugby08-d3d8.dll", 512);
    gstr_put(p + 512, "Rugby 08 D3D8 bridge (OpenGL)", 512);
    wr32(p + 1024, 0x0000255C);
    wr32(p + 1028, 0x0006000E);   // 6.14.0.9564
    wr32(p + 1032, 0x10DE);
    wr32(p + 1036, 0x0391);          // a GeForce 7600 GT class adapter
    wr32(p + 1064, 1);
    return D3D_OK_;
}

// Display modes: one list, offered in X8R8G8B8 and R5G6B5 (D3D8 enumerates all formats together).
static const uint32_t kBaseModes[][2] = { { 640, 480 },  { 800, 600 },   { 1024, 768 },  { 1152, 864 }, { 1280, 720 },
                                          { 1280, 960 }, { 1280, 1024 }, { 1600, 1200 }, { 1920, 1080 } };
// kBaseModes plus the desktop's own size (so the Options menu can pick native resolution), ordered by area.
static std::vector<std::array<uint32_t, 2>> &modes() {
    static std::vector<std::array<uint32_t, 2>> m;
    if (m.empty()) {
        for (auto &b : kBaseModes) m.push_back({ b[0], b[1] });
        int w, h;
        plat_desktop_size(&w, &h);
        std::array<uint32_t, 2> d = { (uint32_t)w, (uint32_t)h };
        if (w >= 640 && h >= 480 && std::find(m.begin(), m.end(), d) == m.end()) m.push_back(d);
        std::stable_sort(m.begin(), m.end(),
                         [](auto &x, auto &y) { return (uint64_t)x[0] * x[1] < (uint64_t)y[0] * y[1]; });
    }
    return m;
}

HLE_D3D(IDirect3D8, GetAdapterModeCount) { return a[1] == 0 ? 2 * (uint32_t)modes().size() : 0; }

HLE_D3D(IDirect3D8, EnumAdapterModes) {       // (this, adapter, mode, D3DDISPLAYMODE*)
    uint32_t n = (uint32_t)modes().size();
    if (a[1] != 0 || a[2] >= 2 * n) return D3DERR_INVALIDCALL_;
    uint32_t i = a[2] % n, f = a[2] < n ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
    wr32(a[3], modes()[i][0]);
    wr32(a[3] + 4, modes()[i][1]);
    wr32(a[3] + 8, 60);
    wr32(a[3] + 12, f);
    return D3D_OK_;
}

HLE_D3D(IDirect3D8, GetAdapterDisplayMode) {  // (this, adapter, D3DDISPLAYMODE*)
    int w, h;
    plat_desktop_size(&w, &h);
    wr32(a[2], (uint32_t)w);
    wr32(a[2] + 4, (uint32_t)h);
    wr32(a[2] + 8, 60);
    wr32(a[2] + 12, D3DFMT_X8R8G8B8);
    return D3D_OK_;
}

static bool display_format(uint32_t f) { return f == D3DFMT_X8R8G8B8 || f == D3DFMT_R5G6B5 || f == D3DFMT_A8R8G8B8; }

HLE_D3D(IDirect3D8, CheckDeviceType) {        // (this, adapter, devtype, display fmt, backbuffer fmt, windowed)
    return a[1] == 0 && display_format(a[3]) && (display_format(a[4]) || a[4] == D3DFMT_UNKNOWN) ? D3D_OK_
                                                                                                 : D3DERR_NOTAVAILABLE_;
}

// What d3d9-webgl can sample / render to / depth-test with.
bool d3d8_format_ok(uint32_t usage, uint32_t rtype, uint32_t f) {
    if (usage & 2u)                                        // D3DUSAGE_DEPTHSTENCIL
        return f == D3DFMT_D16 || f == D3DFMT_D24S8 || f == FMT_D24X8 || f == FMT_D32;
    if (usage & 1u)                                        // D3DUSAGE_RENDERTARGET
        return f == D3DFMT_A8R8G8B8 || f == D3DFMT_X8R8G8B8 || f == D3DFMT_R5G6B5;
    if (rtype == 4 || rtype == 5)                          // volume / cube textures: not in d3d9-webgl
        return false;
    switch (f) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case FMT_X1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_DXT1:
    case D3DFMT_DXT3:
    case D3DFMT_DXT5: return true;
    }
    return false;
}

HLE_D3D(IDirect3D8, CheckDeviceFormat) {      // (this, adapter, devtype, adapter fmt, usage, rtype, fmt)
    bool ok = a[1] == 0 && d3d8_format_ok(a[4], a[5], a[6]);
    return ok ? D3D_OK_ : D3DERR_NOTAVAILABLE_;
}

HLE_D3D(IDirect3D8, CheckDeviceMultiSampleType) { return a[5] == 0 ? D3D_OK_ : D3DERR_NOTAVAILABLE_; }
HLE_D3D(IDirect3D8, CheckDepthStencilMatch) {
    (void)a;
    return D3D_OK_;
}

// D3DCAPS8 (212 bytes): a DX8 part with vertex shaders 1.1 and the fixed-function pixel pipeline.
void d3d8_fill_caps(uint32_t p) {
    memset(G2H(p), 0, 212);
    auto d = [&](uint32_t off, uint32_t v) { wr32(p + off, v); };
    auto f = [&](uint32_t off, float v) {
        uint32_t u;
        memcpy(&u, &v, 4);
        wr32(p + off, u);
    };
    d(0, 1);                       // DeviceType = HAL
    d(8, 0x00020000);              // Caps: D3DCAPS_READ_SCANLINE
    d(12, 0x80000000 | 0x00020000);// Caps2: NO2DDURING3DSCENE | FULLSCREENGAMMA
    d(16, 0x00000020);             // Caps3: ALPHA_FULLSCREEN_FLIP_OR_DISCARD
    d(20, 0x8000000F);             // PresentationIntervals: IMMEDIATE | ONE..FOUR
    d(28, 0x00000010 | 0x00000040 | 0x00000080 | 0x00000200 | 0x00000400 | 0x00002000 | 0x00004000 | 0x00010000 |
              0x00080000);
    d(32, 0x0002 | 0x0010 | 0x0020 | 0x0040 | 0x0080 | 0x0100 | 0x0800);   // PrimitiveMiscCaps
    d(36, 0x00000001 | 0x00000080 | 0x00000100 | 0x00010000 |
              0x00100000);  // RasterCaps: DITHER ZTEST FOGVERTEX FOGTABLE WFOG
    d(40, 0xFF);
    d(52, 0xFF);      // ZCmpCaps, AlphaCmpCaps: all
    d(44, 0x1FFF);
    d(48, 0x1FFF);  // Src/DestBlendCaps
    d(56, 0x00000008 | 0x00000200 | 0x00004000 | 0x00080000); // ShadeCaps: Gouraud colour/specular/alpha/fog
    d(60, 0x00000001 | 0x00000004 | 0x00000040 | 0x00004000 | 0x00000800 |
              0x00010000); // TextureCaps: PERSPECTIVE ALPHA MIPMAP ...
    d(64, 0x0300 | 0x030000 | 0x03000000); // TextureFilterCaps: min/mip/mag point+linear
    d(76, 0x01 | 0x02 | 0x04 | 0x10); // TextureAddressCaps: WRAP MIRROR CLAMP INDEPENDENTUV
    d(84, 0x02 | 0x04 | 0x08 | 0x10); // LineCaps
    d(88, 4096);
    d(92, 4096);
    d(96, 0);
    d(100, 8192);
    d(104, 4096);
    d(108, 16);
    f(112, 1e10f);
    f(116, -4096.f);
    f(120, -4096.f);
    f(124, 4096.f);
    f(128, 4096.f);
    d(136, 0xFF); // StencilCaps
    d(140, 8 | 0x00080000); // FVFCaps: 8 texcoords, PSIZE
    d(144, 0x00FFFFFF); // TextureOpCaps
    d(148, 8);
    d(152, 8); // MaxTextureBlendStages, MaxSimultaneousTextures
    d(156,
      0x01 | 0x08 | 0x10 | 0x20); // VertexProcessingCaps: TEXGEN MATERIALSOURCE7 DIRECTIONALLIGHTS POSITIONALLIGHTS
    d(160, 8);
    d(164, 1);
    d(168, 4);
    d(172, 0);
    f(176, 64.f);
    d(180, 0x000FFFFF);
    d(184, 0x00FFFFFF);
    d(188, 16);
    d(192, 255);
    // vs.1.1 with 96 constants (MojoShader -> GLSL), no pixel shaders: a GeForce4 MX-class DX8 part.
    // Reporting no vertex shaders sent the game down its fixed-function fallback for players.
    d(196, 0xFFFE0101);
    d(200, 96);
    d(204, 0);
    if (getenv("RT_PS_CAPS")) {
        d(204, 0xFFFF0104);
        f(208, 8.f);
    } // experiment: ps.1.4 (see CreatePixelShader)
}

HLE_D3D(IDirect3D8, GetDeviceCaps) {
    if (a[1] != 0) return D3DERR_INVALIDCALL_;
    d3d8_fill_caps(a[3]);
    return D3D_OK_;
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
