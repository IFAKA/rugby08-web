// d3d8_int.hpp -- internals shared by the D3D8 bridge sources (runtime/d3d8/*.cpp).
#pragma once
#include "d3d9.h"           // third_party/d3d9-webgl
#include "d3d8_bridge.h"
#include <vector>

extern "C" uint32_t gstr_put(uint32_t va, const char *s, uint32_t cap);

// Real HRESULTs for the guest (d3d9-webgl's own D3DERR_* are small negative placeholders).
enum : uint32_t {
    D3D_OK_ = 0, E_NOTIMPL_ = 0x80004001u, E_NOINTERFACE_ = 0x80004002u, E_OUTOFMEMORY_ = 0x8007000Eu,
    D3DERR_INVALIDCALL_ = 0x8876086Cu, D3DERR_NOTAVAILABLE_ = 0x8876086Au, D3DERR_DEVICELOST_ = 0x88760868u,
    D3DERR_OUTOFVIDEOMEMORY_ = 0x8876017Cu,
};
// D3D8 formats d3d9-webgl's enum does not name.
enum : uint32_t { FMT_X1R5G5B5 = 24, FMT_D16_LOCKABLE = 70, FMT_D32 = 71, FMT_D24X8 = 77 };

enum Kind { K_D3D, K_DEVICE, K_TEXTURE, K_CUBE, K_VOLTEX, K_SURFACE, K_VOLUME, K_VB, K_IB, K_SWAP };

struct Obj {
    explicit Obj(Kind k) : kind(k) {}
    Kind kind;
    uint32_t guest = 0, refs = 0;
    bool dead = false;
    IUnknown *host = nullptr;            // the d3d9-webgl object, if any
    bool owns = true;                    // false: borrowed from d3d9-webgl (its Release deletes)
    uint32_t owner = 0;                  // guest device that created it
    uint32_t w = 0, h = 0, fmt = 0, usage = 0, pool = 0, levels = 0;
    // device
    uint32_t d3d = 0, behavior = 0;
    struct DevState *dev = nullptr;      // d3d8_device.cpp
    // vertex/index buffer: guest shadow + the range written since the last Unlock
    uint32_t size = 0, fvf = 0, shadow = 0, lock_lo = 0, lock_hi = 0, locks = 0;
    // texture: one guest shadow per level (d3d9-webgl's pitch), uploaded on UnlockRect
    std::vector<uint32_t> lvl_shadow, lvl_pitch, lvl_size, lvl_surf;
    uint32_t host_fmt = 0;               // format d3d9-webgl holds (A8R8G8B8 when the bridge converts)
    // surface: a texture level (parent) or a standalone surface (host = IDirect3DSurface9)
    Obj *parent = nullptr;
    uint32_t level = 0;
};

uint32_t d3d8_new(Obj *o);              // creates the guest object, refs = 1
Obj *d3d8_obj(uint32_t guest);
extern "C" bool d3d8_format_ok(uint32_t usage, uint32_t rtype, uint32_t fmt);
extern "C" void d3d8_fill_caps(uint32_t guest_caps8);
extern "C" void d3d8_present_params(uint32_t guest_pp8, D3DPRESENT_PARAMETERS *pp);
void d3d8_device_init(Obj *dev, uint32_t focus_hwnd);
inline IDirect3DDevice9 *dev9(Obj *o) { return static_cast<IDirect3DDevice9 *>(o->host); }
IDirect3DSurface9 *d3d8_surface9(Obj *surf);    // d3d8_resources.cpp: host surface (texture level on demand)
IDirect3DTexture9 *d3d8_texture9(Obj *tex);

// d3d8_vshader.cpp: a D3D8 vertex shader -- its declaration (stream elements, constants) and, for
// a programmable one, MojoShader's GLSL ES 3.00 translation.
struct Vs8Elem { uint8_t stream, reg, type; uint16_t offset; };   // reg: D3DVSDE_*, type: D3DVSDT_*
struct Vs8Const { uint32_t reg; float v[4]; };
struct MOJOSHADER_parseData;
struct Vs8 {
    std::vector<Vs8Elem> elems;
    std::vector<Vs8Const> consts;
    uint32_t streams = 0;                // bit per stream used
    uint32_t fvf = 0;                    // fixed-function: equivalent FVF, when one exists
    bool programmable = false, fvf_ok = false;
    const MOJOSHADER_parseData *pd = nullptr;
    D3D9WProgVS *gl = nullptr;           // its GL program (d3d9-webgl), built on first use
    bool gl_tried = false;
};
bool vs8_parse_decl(uint32_t decl_va, Vs8 *vs);
bool vs8_translate(uint32_t func_va, Vs8 *vs, int id);
D3D9WProgVS *vs8_gl(Vs8 *vs, int id);

#define HLE_D3D(iface, method) uint32_t d3d8_##iface##_##method(const uint32_t *a)
extern "C" uint32_t d3d8_IUnknown_Release(const uint32_t *a);
