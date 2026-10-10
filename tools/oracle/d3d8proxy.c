/* d3d8proxy.c -- d3d8.dll for the Wine reference run only (tools/run_wine.sh). Built by
 * tools/oracle/build_proxy.sh with Apple clang + the lld in emscripten; no CRT, no Windows SDK.
 *
 * The game asks for a 640x480 16-bit fullscreen device. macOS lists no such display mode, so
 * under Wine ChangeDisplaySettings fails, CreateDevice fails and the game dereferences the null
 * device. This proxy loads Wine's d3d8.dll and patches IDirect3D8::CreateDevice and
 * IDirect3DDevice8::Reset in their vtables to request a windowed device with the desktop's
 * back-buffer format instead. The game's code is not modified.
 * With RUGBY08_ORACLE_BACKGROUND set (tools/run_wine.sh without --show/--keep) the game window is
 * minimized without activation, so a reference run does not take over the user's screen. */
typedef unsigned long DWORD;
typedef void *HMODULE;
typedef long HRESULT;
#define WINAPI __stdcall

__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) unsigned WINAPI GetSystemDirectoryA(char *, unsigned);
__declspec(dllimport) int WINAPI VirtualProtect(void *, DWORD, DWORD, DWORD *);
__declspec(dllimport) void WINAPI OutputDebugStringA(const char *);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableA(const char *, char *, DWORD);
__declspec(dllimport) int WINAPI ShowWindow(void *, int);

/* D3DPRESENT_PARAMETERS (D3D8), DWORD indices */
enum { PP_W, PP_H, PP_FMT, PP_COUNT, PP_MS, PP_SWAP, PP_HWND, PP_WINDOWED, PP_AUTODS, PP_DSFMT,
       PP_FLAGS, PP_REFRESH, PP_INTERVAL, PP_N };
enum { D3D_GETADAPTERDISPLAYMODE = 8, D3D_CREATEDEVICE = 15, DEV_RESET = 14 };

typedef HRESULT (WINAPI *create_device_fn)(void *, unsigned, unsigned, void *, DWORD, DWORD *, void **);
typedef HRESULT (WINAPI *reset_fn)(void *, DWORD *);
typedef HRESULT (WINAPI *display_mode_fn)(void *, unsigned, DWORD *);

static void *g_d3d;                 /* the IDirect3D8 that created the device */
static create_device_fn g_create;
static reset_fn g_reset;

static void patch_slot(void **vtbl, int i, void *fn, void **old) {
    DWORD prot;
    if (vtbl[i] == fn) return;
    VirtualProtect(&vtbl[i], sizeof(void *), 0x04 /* PAGE_READWRITE */, &prot);
    *old = vtbl[i];
    vtbl[i] = fn;
    VirtualProtect(&vtbl[i], sizeof(void *), prot, &prot);
}

/* Copy of the game's parameters with the fullscreen fields made windowed. */
static void windowed(const DWORD *in, DWORD *out) {
    DWORD mode[4] = {0};             /* D3DDISPLAYMODE: Width Height RefreshRate Format */
    for (int i = 0; i < PP_N; i++) out[i] = in[i];
    if (g_d3d) ((display_mode_fn)(*(void ***)g_d3d)[D3D_GETADAPTERDISPLAYMODE])(g_d3d, 0, mode);
    out[PP_WINDOWED] = 1;
    if (mode[3]) out[PP_FMT] = mode[3];
    out[PP_REFRESH] = 0;
    out[PP_INTERVAL] = 0;            /* D3DPRESENT_INTERVAL_DEFAULT: required when windowed */
}

static HRESULT WINAPI reset_hook(void *dev, DWORD *pp) {
    DWORD w[PP_N];
    windowed(pp, w);
    OutputDebugStringA("d3d8proxy: Reset forced windowed\n");
    return g_reset(dev, w);
}

static HRESULT WINAPI create_device_hook(void *d3d, unsigned adapter, unsigned type, void *hwnd,
                                         DWORD flags, DWORD *pp, void **out) {
    DWORD w[PP_N];
    HRESULT hr;
    g_d3d = d3d;
    windowed(pp, w);
    hr = g_create(d3d, adapter, type, hwnd, flags, w, out);
    OutputDebugStringA(hr >= 0 ? "d3d8proxy: CreateDevice forced windowed: ok\n"
                               : "d3d8proxy: CreateDevice forced windowed: FAILED\n");
    if (hr >= 0 && *out) patch_slot(*(void ***)*out, DEV_RESET, (void *)reset_hook, (void **)&g_reset);
    if (GetEnvironmentVariableA("RUGBY08_ORACLE_BACKGROUND", 0, 0)) {
        if (pp[PP_HWND]) ShowWindow((void *)pp[PP_HWND], 7 /* SW_SHOWMINNOACTIVE */);
        if (hwnd && hwnd != (void *)pp[PP_HWND]) ShowWindow(hwnd, 7);
    }
    return hr;
}

__declspec(dllexport) void *WINAPI Direct3DCreate8(unsigned sdk) {
    static void *(WINAPI *real)(unsigned);
    void *d3d;
    if (!real) {
        char path[300];
        unsigned n = GetSystemDirectoryA(path, 260);
        const char *s = "\\d3d8.dll";
        while (*s) path[n++] = *s++;
        path[n] = 0;
        real = (void *(WINAPI *)(unsigned))GetProcAddress(LoadLibraryA(path), "Direct3DCreate8");
        if (!real) { OutputDebugStringA("d3d8proxy: system d3d8.dll not found\n"); return 0; }
    }
    d3d = real(sdk);
    if (d3d) patch_slot(*(void ***)d3d, D3D_CREATEDEVICE, (void *)create_device_hook, (void **)&g_create);
    return d3d;
}
