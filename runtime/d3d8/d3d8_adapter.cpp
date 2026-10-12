// d3d8_adapter.cpp -- IDirect3D8 adapter queries: identity, display modes, device/format checks and
// D3DCAPS8 (a DX8 part with vs.1.1; ps.1.4 with RT_PS_CAPS). Objects and CreateDevice: d3d8_core.cpp.
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

extern "C" {

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

}  // extern "C"
