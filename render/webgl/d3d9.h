#pragma once

#include <cstdint>
#include <errno.h>
#include <stdio.h>

// Basic D3D9 types
#ifndef _WINDOWS_BASE_TYPES_
#define _WINDOWS_BASE_TYPES_
typedef uint16_t WORD;
#ifndef _DWORD_DEFINED
#define _DWORD_DEFINED
typedef uint32_t DWORD;
#endif
typedef uint32_t UINT;
typedef int32_t INT;
#ifndef _BOOL_DEFINED
#define _BOOL_DEFINED
typedef int BOOL;
#endif
typedef void *HWND;
#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef long HRESULT;
#endif
typedef float FLOAT;
typedef const char *LPCSTR;
typedef void *HINSTANCE;
typedef unsigned long ULONG;
typedef uint8_t BYTE;
typedef long LONG;
#endif
#ifndef WINAPI
#define WINAPI
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef _RECT_DEFINED
typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
#define _RECT_DEFINED
#endif

#ifndef _WIN32
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include "windows_compat.h"
#endif

typedef char TCHAR;

#ifndef _D3DRECT_DEFINED
typedef struct _D3DRECT {
    LONG x1;
    LONG y1;
    LONG x2;
    LONG y2;
} D3DRECT;
#define _D3DRECT_DEFINED
#endif

#define D3DCOLORWRITEENABLE_RED   (1 << 0)
#define D3DCOLORWRITEENABLE_GREEN (1 << 1)
#define D3DCOLORWRITEENABLE_BLUE  (1 << 2)
#define D3DCOLORWRITEENABLE_ALPHA (1 << 3)

// FVF
#define D3DFVF_XYZ     0x002
#define D3DFVF_NORMAL  0x010
#define D3DFVF_DIFFUSE 0x040
#define D3DFVF_TEX1    0x100

// D3DTEXTUREOP
typedef enum _D3DTEXTUREOP {
    D3DTOP_DISABLE = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_MODULATE4X = 6,
    D3DTOP_ADD = 7,
    D3DTOP_ADDSIGNED = 8,
    D3DTOP_ADDSIGNED2X = 9,
    D3DTOP_SUBTRACT = 10,
    D3DTOP_ADDSMOOTH = 11,
    D3DTOP_BLENDDIFFUSEALPHA = 12,
    D3DTOP_BLENDTEXTUREALPHA = 13,
    D3DTOP_BLENDFACTORALPHA = 14,
    D3DTOP_BLENDTEXTUREALPHAPM = 15,
    D3DTOP_BLENDCURRENTALPHA = 16,
    D3DTOP_PREMODULATE = 17,
    D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
    D3DTOP_MODULATECOLOR_ADDALPHA = 19,
    D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
    D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
    D3DTOP_BUMPENVMAP = 22,
    D3DTOP_BUMPENVMAPLUMINANCE = 23,
    D3DTOP_DOTPRODUCT3 = 24,
    D3DTOP_MULTIPLYADD = 25,
    D3DTOP_LERP = 26,
    D3DTOP_FORCE_DWORD = 0x7fffffff,
} D3DTEXTUREOP;

// D3DTEXTUREARG
#define D3DTA_CURRENT 0x00000001
#define D3DTA_TEXTURE 0x00000002

// D3DTEXTURESTAGESTATETYPE
enum D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_BUMPENVMAT00 = 7,
    D3DTSS_BUMPENVMAT01 = 8,
    D3DTSS_BUMPENVMAT10 = 9,
    D3DTSS_BUMPENVMAT11 = 10,
    D3DTSS_TEXCOORDINDEX = 11,
    D3DTSS_BUMPENVLSCALE = 22,
    D3DTSS_BUMPENVLOFFSET = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS = 24,
    D3DTSS_COLORARG0 = 26,
    D3DTSS_ALPHAARG0 = 27,
    D3DTSS_RESULTARG = 28,
    D3DTSS_CONSTANT = 32,
};

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef S_FALSE
#define S_FALSE 1
#endif
#ifndef S_OK
#define S_OK 0
#endif
#ifndef D3D_OK
#define D3D_OK 0
#endif
#define D3D_SDK_VERSION 32
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef FAILED
#define FAILED(hr) ((HRESULT)(hr) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#endif
#define D3DERR_INVALIDCALL -1

// D3D9 Color type
typedef DWORD D3DCOLOR;

// D3D9 types
typedef struct _D3DMATRIX {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D3DMATRIX;

typedef struct _D3DVECTOR {
    float x;
    float y;
    float z;
} D3DVECTOR;

// Primitive types
enum D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
};

enum D3DFILLMODE {
    D3DFILL_POINT = 1,
    D3DFILL_WIREFRAME = 2,
    D3DFILL_SOLID = 3,
};

// Fog modes
enum D3DFOGMODE {
    D3DFOG_NONE = 0,
    D3DFOG_EXP = 1,
    D3DFOG_EXP2 = 2,
    D3DFOG_LINEAR = 3,
    D3DFOG_FORCE_DWORD = 0x7fffffff,
};

enum D3DCULL {
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
};

enum D3DCMPFUNC {
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
};

// Flexible Vertex Format flags
#define D3DFVF_XYZ     0x002
#define D3DFVF_DIFFUSE 0x040
#define D3DFVF_XYZRHW  0x004
#define D3DFVF_NORMAL  0x010
#define D3DFVF_TEX1    0x100

// Clear flags
#define D3DCLEAR_TARGET  0x00000001L
#define D3DCLEAR_ZBUFFER 0x00000002L
#define D3DCLEAR_STENCIL 0x00000004L

// Format types
typedef DWORD D3DFORMAT;
enum {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_A8B8G8R8 = 32,
    D3DFMT_X8B8G8R8 = 33,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_D24S8 = 75,
    D3DFMT_D16 = 80,
    D3DFMT_DXT1 = 0x31545844, // 'DXT1'
    D3DFMT_DXT2 = 0x32545844, // 'DXT2'
    D3DFMT_DXT3 = 0x33545844, // 'DXT3'
    D3DFMT_DXT4 = 0x34545844, // 'DXT4'
    D3DFMT_DXT5 = 0x35545844, // 'DXT5'
};

// Device types
enum D3DDEVTYPE {
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
};

// Transform states
enum D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0 = 16,
    D3DTS_TEXTURE1 = 17,
    D3DTS_TEXTURE2 = 18,
    D3DTS_TEXTURE3 = 19,
    D3DTS_TEXTURE4 = 20,
    D3DTS_TEXTURE5 = 21,
    D3DTS_TEXTURE6 = 22,
    D3DTS_TEXTURE7 = 23,
    D3DTS_WORLD = 256,
    D3DTS_WORLDMATRIX = 256,
};

// Render states
enum D3DRENDERSTATETYPE {
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_FOGCOLOR = 34,
    D3DRS_FOGTABLEMODE = 35,
    D3DRS_FOGSTART = 36,
    D3DRS_FOGEND = 37,
    D3DRS_FOGDENSITY = 38,
    D3DRS_RANGEFOGENABLE = 48,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_TEXTUREFACTOR = 60,
    D3DRS_LIGHTING = 137,
    D3DRS_AMBIENT = 139,
    D3DRS_FOGVERTEXMODE = 140,
    D3DRS_ZENABLE = 7,
    D3DRS_NORMALIZENORMALS = 43,

    // Point Sprites
    D3DRS_POINTSPRITEENABLE = 154,
    D3DRS_POINTSCALEENABLE = 155,
    D3DRS_POINTSCALE_A = 156,
    D3DRS_POINTSCALE_B = 157,
    D3DRS_POINTSCALE_C = 158,
    D3DRS_POINTSIZE = 159,
    D3DRS_POINTSIZE_MIN = 160,
    D3DRS_POINTSIZE_MAX = 161,

    D3DRS_VERTEXBLEND = 151,
    D3DRS_CLIPPLANEENABLE = 152,
    D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
    D3DRS_COLORWRITEENABLE = 168,
    D3DRS_CLIPPING = 136,

    // Stencil states
    D3DRS_STENCILENABLE = 52,
    D3DRS_STENCILFAIL = 53,
    D3DRS_STENCILZFAIL = 54,
    D3DRS_STENCILPASS = 55,
    D3DRS_STENCILFUNC = 56,
    D3DRS_STENCILREF = 57,
    D3DRS_STENCILMASK = 58,
    D3DRS_STENCILWRITEMASK = 59,
    D3DRS_TWOSIDEDSTENCILMODE = 185,
    D3DRS_CCW_STENCILFAIL = 186,
    D3DRS_CCW_STENCILZFAIL = 187,
    D3DRS_CCW_STENCILPASS = 188,
    D3DRS_CCW_STENCILFUNC = 189,
    D3DRS_SCISSORTESTENABLE = 174,
};

enum D3DSTENCILOP {
    D3DSTENCILOP_KEEP = 1,
    D3DSTENCILOP_ZERO = 2,
    D3DSTENCILOP_REPLACE = 3,
    D3DSTENCILOP_INCRSAT = 4,
    D3DSTENCILOP_DECRSAT = 5,
    D3DSTENCILOP_INVERT = 6,
    D3DSTENCILOP_INCR = 7,
    D3DSTENCILOP_DECR = 8,
};

#define D3DLOCK_NOOVERWRITE 0x00001000L

#define D3DLOCK_DISCARD 0x00002000L

// Blend modes
enum D3DBLEND {
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
};

// Swap effects
typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD = 1,
    D3DSWAPEFFECT_FLIP = 2,
    D3DSWAPEFFECT_COPY = 3,
} D3DSWAPEFFECT;

// MultiSample types
typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE = 0,
    D3DMULTISAMPLE_NONMASKABLE = 1,
    D3DMULTISAMPLE_2_SAMPLES = 2,
    D3DMULTISAMPLE_3_SAMPLES = 3,
    D3DMULTISAMPLE_4_SAMPLES = 4,
    D3DMULTISAMPLE_5_SAMPLES = 5,
    D3DMULTISAMPLE_6_SAMPLES = 6,
    D3DMULTISAMPLE_7_SAMPLES = 7,
    D3DMULTISAMPLE_8_SAMPLES = 8,
    D3DMULTISAMPLE_9_SAMPLES = 9,
    D3DMULTISAMPLE_10_SAMPLES = 10,
    D3DMULTISAMPLE_11_SAMPLES = 11,
    D3DMULTISAMPLE_12_SAMPLES = 12,
    D3DMULTISAMPLE_13_SAMPLES = 13,
    D3DMULTISAMPLE_14_SAMPLES = 14,
    D3DMULTISAMPLE_15_SAMPLES = 15,
    D3DMULTISAMPLE_16_SAMPLES = 16,
} D3DMULTISAMPLE_TYPE;

// Present parameters
struct D3DPRESENT_PARAMETERS {
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    DWORD MultiSampleQuality;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT PresentationInterval;
};

#ifndef _GUID_DEFINED_
#define _GUID_DEFINED_
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif

// Pool types
enum D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3,
};

// Usage flags
#define D3DUSAGE_RENDERTARGET 0x00000001L
#define D3DUSAGE_DEPTHSTENCIL 0x00000002L

// Locked rect structure
struct D3DLOCKED_RECT {
    INT Pitch;
    void *pBits;
};

// Color value structure
struct _D3DCOLORVALUE {
    float r;
    float g;
    float b;
    float a;
};
typedef struct _D3DCOLORVALUE D3DCOLORVALUE;

#define D3DZB_FALSE 0
#define D3DZB_TRUE  1

// Light types
enum D3DLIGHTTYPE {
    D3DLIGHT_POINT = 1,
    D3DLIGHT_SPOT = 2,
    D3DLIGHT_DIRECTIONAL = 3,
};

// Light structure
struct D3DLIGHT9 {
    D3DLIGHTTYPE Type;
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Ambient;
    D3DVECTOR Position;
    D3DVECTOR Direction;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
};

// Material structure
struct D3DMATERIAL9 {
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Ambient;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Emissive;
    float Power;
};

// The interfaces hold only what runtime/d3d8 calls: the D3D8 layer owns the object lifetimes and
// the rest of the D3D8 API (caps, adapters, state blocks, shaders) is answered there.
struct IUnknown {
    virtual ~IUnknown() {}
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};

struct IDirect3DSurface9 : public IUnknown {};

struct IDirect3DTexture9 : public IUnknown {
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) = 0;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const void *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(UINT Level) = 0;
};

// Viewport structure
struct D3DVIEWPORT9 {
    DWORD X;
    DWORD Y;
    DWORD Width;
    DWORD Height;
    float MinZ;
    float MaxZ;
};

// Sampler state types
enum D3DSAMPLERSTATETYPE {
    D3DSAMP_ADDRESSU = 1,
    D3DSAMP_ADDRESSV = 2,
    D3DSAMP_ADDRESSW = 3,
    D3DSAMP_MINFILTER = 5,
    D3DSAMP_MAGFILTER = 6,
    D3DSAMP_MIPFILTER = 7,
    D3DSAMP_MIPMAPLODBIAS = 8,
};

// Texture filter types
enum D3DTEXTUREFILTERTYPE {
    D3DTEXF_NONE = 0,
    D3DTEXF_POINT = 1,
    D3DTEXF_LINEAR = 2,
};

enum D3DDECLUSAGE {
    D3DDECLUSAGE_POSITION = 0,
    D3DDECLUSAGE_BLENDWEIGHT = 1,
    D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL = 3,
    D3DDECLUSAGE_PSIZE = 4,
    D3DDECLUSAGE_TEXCOORD = 5,
    D3DDECLUSAGE_TANGENT = 6,
    D3DDECLUSAGE_BINORMAL = 7,
};

// Texture address modes
enum D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_CLAMP = 3,
};

class IDirect3DIndexBuffer9 : public IUnknown {
  public:
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
};

class IDirect3DVertexBuffer9 : public IUnknown {
  public:
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
};

class IDirect3DDevice9 : public IUnknown {
  public:
    virtual HRESULT Clear(DWORD Count, const void *pRects, DWORD Flags, D3DCOLOR Color, FLOAT Z, DWORD Stencil) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Present(const void *pSourceRect, const void *pDestRect, HWND hDestWindowOverride,
                            const void *pDirtyRegion) = 0;
    virtual HRESULT SetFVF(DWORD FVF) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                    const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                           UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                           const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9 **ppTexture, void *pSharedHandle) = 0;
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                              D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard,
                                              IDirect3DSurface9 **ppSurface, void *pSharedHandle) = 0;
    virtual HRESULT SetClipPlane(DWORD Index, const float *pPlane) = 0;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DTexture9 *pTexture) = 0;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                      IDirect3DIndexBuffer9 **ppIndexBuffer, void *pSharedHandle) = 0;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                       IDirect3DVertexBuffer9 **ppVertexBuffer, void *pSharedHandle) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexData) = 0;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes,
                                    UINT Stride) = 0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) = 0;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9 *pMaterial) = 0;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) = 0;
    virtual HRESULT GetRenderTarget(DWORD Index, IDirect3DSurface9 **ppRenderTarget) = 0;
    virtual HRESULT SetRenderTarget(DWORD Index, IDirect3DSurface9 *pRenderTarget) = 0;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pZStencilSurface) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex,
                                         UINT NumVertices, UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
};

class IDirect3D9 : public IUnknown {
  public:
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                 D3DPRESENT_PARAMETERS *pPresentationParameters,
                                 IDirect3DDevice9 **ppReturnedDeviceInterface) = 0;
};

extern "C" IDirect3D9 *Direct3DCreate9(UINT SDKVersion);

// D3D8 programmable vertex shaders (rugby08 runtime/d3d8/d3d8_vshader.cpp translates vs.1.x to
// GLSL ES 3.00 with MojoShader). Each one is linked with the fixed-function pixel stage; while one
// is in use, draws run it with the D3D8 declaration's stream-0 elements as inputs and the live
// constant registers as vs_uniforms_vec4.
struct D3D9WProgVS;
struct D3D9WElem {
    unsigned char stream, reg, type;
    unsigned short offset;
};   // D3DVSDE_*, D3DVSDT_*
// regs: the constant register of each vs_uniforms_vec4 slot, in order. nullptr if it does not link.
extern "C" D3D9WProgVS *d3d9w_progvs_create(const char *glsl, const unsigned char *regs, int nregs,
                                            const D3D9WElem *elems, int nelems);
// p = nullptr: back to fixed function. consts: the device's 96 constant registers (read at each draw).
extern "C" void d3d9w_progvs_use(D3D9WProgVS *p, const float (*consts)[4]);
// D3D8 pixel shaders (ps.1.x -> GLSL ES 3.00 by MojoShader). While one is in use it replaces the
// fixed-function texture stages: linked with the programmable vertex shader in use, or with the
// fixed-function vertex stage, matching varyings by MojoShader's io_<usage>_<index> names.
// regs: the constant register of each ps_uniforms_vec4 slot. nullptr if it does not compile.
struct D3D9WProgPS;
extern "C" D3D9WProgPS *d3d9w_progps_create(const char *glsl, const unsigned char *regs, int nregs);
// p = nullptr: fixed-function texture stages. consts: the device's 8 pixel constant registers.
extern "C" void d3d9w_progps_use(D3D9WProgPS *p, const float (*consts)[4]);
// Print every draw's state while on (rugby08: RT_TRACE_DRAWS_FRAME=N traces frame N).
extern "C" void d3d9w_trace_draws(int on);
extern "C" void d3d9w_interp_mode(int on);    // fps.interp: record each step; frames come from d3d9w_interp_draw
extern "C" int d3d9w_interp_draw(float t);    // the last step, blended from the one before by t (0..1)
extern "C" int d3d9w_interp_rewrites(int *copies);
extern "C" void d3d9w_interp_flush(void);   // before a GPU readback: the recorded calls run
extern "C" int d3d9w_interp_eye(
    float *
        e); // the camera position of the last replay (checks)   // steps that rewrote a buffer their own earlier draws read
// GL texture name of a texture (0 if none): lets the D3D8 bridge read a render-target texture back.
extern "C" unsigned d3d9w_texture_gl(IDirect3DTexture9 *t);
// overlay.cpp: an RGBA image (w x h, the back buffer's size) over the finished frame; changed: re-upload
extern "C" void d3d9w_overlay_draw(const unsigned char *rgba, int w, int h, int changed);
extern "C" void d3d9w_overlay_undo(void); // after the swap: the game's frame back under the overlay
// overlay.cpp: the finished frame before the overlay, RGBA top row first (w x h); 1 if read
extern "C" int d3d9w_frame_read(unsigned char *rgba, int w, int h);
// The render thread (render/webgl/queue.cpp; fps.interp on fast displays): the platform starts it and
// runs batches; runtime/ code that reads the GPU runs there (d3d9w_on_render), after what was drawn.
extern "C" void d3d9w_rq_start(void); // on the render thread, before its first batch
extern "C" void d3d9w_rq_hand(void); // at Present: the step's calls to the render thread
extern "C" int d3d9w_rq_syncs_take(void); // the game's waits for the render thread since last asked
extern "C" double d3d9w_rq_run(double ms); // render thread: the next batch, waiting up to ms (ms it ran, < 0: none)
extern "C" void d3d9w_rq_post(void (*f)(void *), void *arg);
extern "C" void d3d9w_rq_call(void (*f)(void *), void *arg);
template <class F> void d3d9w_on_render(F &&f) {
    d3d9w_rq_call([](void *p) { (*(F *)p)(); }, &f);
}
