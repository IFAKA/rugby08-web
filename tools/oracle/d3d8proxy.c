/* d3d8proxy.c -- d3d8.dll for the Wine reference run only (tools/check/run_wine.sh). Built by
 * tools/oracle/build_proxy.sh with Apple clang + the lld in emscripten; no CRT, no Windows SDK.
 *
 * The game asks for a 640x480 16-bit fullscreen device. macOS lists no such display mode, so
 * under Wine ChangeDisplaySettings fails, CreateDevice fails and the game dereferences the null
 * device. This proxy loads Wine's d3d8.dll and patches IDirect3D8::CreateDevice and
 * IDirect3DDevice8::Reset in their vtables to request a windowed device with the desktop's
 * back-buffer format instead. The game's code is not modified.
 * With RUGBY08_ORACLE_BACKGROUND set (tools/check/run_wine.sh without --show/--keep) the game window is
 * minimized without activation, so a reference run does not take over the user's screen.
 *
 * Input replay (RUGBY08_REPLAY = Windows path of a tests/replays/<name>.txt file, the format of
 * platform/sdl/sdl_plat.c): frames are counted at IDirect3DDevice8::Present as the runtime counts
 * them; keys and clicks are posted to the game window and held in a state that GetAsyncKeyState /
 * GetKeyState (patched in the game's import table) and the dinput.dll proxy (d3d8proxy_input) read;
 * `shot PATH` events save the back buffer as RUGBY08_SHOTDIR\<name>.bmp before that Present. */
#include "d3d8proxy.h"

/* D3DPRESENT_PARAMETERS (D3D8), DWORD indices */
enum { PP_W, PP_H, PP_FMT, PP_COUNT, PP_MS, PP_SWAP, PP_HWND, PP_WINDOWED, PP_AUTODS, PP_DSFMT,
       PP_FLAGS, PP_REFRESH, PP_INTERVAL, PP_N };
enum { D3D_GETADAPTERDISPLAYMODE = 8, D3D_CREATEDEVICE = 15, DEV_RESET = 14 };

typedef HRESULT (WINAPI *create_device_fn)(void *, unsigned, unsigned, void *, DWORD, DWORD *, void **);
typedef HRESULT (WINAPI *reset_fn)(void *, DWORD *);
typedef HRESULT (WINAPI *display_mode_fn)(void *, unsigned, DWORD *);

static void *g_d3d;                 /* the IDirect3D8 that created the device */
void *g_hwnd;                /* the game's focus window */
static create_device_fn g_create;
static reset_fn g_reset;

void patch_slot(void **vtbl, int i, void *fn, void **old) {
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
    out[PP_FLAGS] |= 1;              /* D3DPRESENTFLAG_LOCKABLE_BACKBUFFER: replay shots read it */
}

static HRESULT WINAPI reset_hook(void *dev, DWORD *pp) {
    DWORD w[PP_N];
    windowed(pp, w);
    OutputDebugStringA("d3d8proxy: Reset forced windowed\n");
    return g_reset(dev, w);
}

/* the back buffer (lockable, X8R8G8B8) as a bottom-up 32-bit BMP */
static void save_bmp(void *dev, const char *name) {
    char dir[260], path[400];
    if (!GetEnvironmentVariableA("RUGBY08_SHOTDIR", dir, sizeof dir)) return;
    int n = 0; for (const char *c = dir; *c; c++) path[n++] = *c;
    path[n++] = '\\'; for (const char *c = name; *c; c++) path[n++] = *c;
    for (const char *c = ".bmp"; *c; c++) path[n++] = *c;
    path[n] = 0;
    void *surf = 0;
    typedef HRESULT (WINAPI *getbb_fn)(void *, unsigned, unsigned, void **);
    if (((getbb_fn)(*(void ***)dev)[16])(dev, 0, 0, &surf) < 0 || !surf) return;      /* GetBackBuffer */
    void **sv = *(void ***)surf;
    DWORD desc[8] = {0}, lr[2] = {0};                    /* D3DSURFACE_DESC; D3DLOCKED_RECT { Pitch, pBits } */
    ((HRESULT (WINAPI *)(void *, DWORD *))sv[8])(surf, desc);                          /* GetDesc */
    unsigned w = desc[6], h = desc[7];
    if (((HRESULT (WINAPI *)(void *, DWORD *, void *, DWORD))sv[9])(surf, lr, 0, 0x10 /* READONLY */) >= 0) {
        void *f = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, 0, 0);
        if (f != (void *)-1) {
            unsigned char hdr[54] = { 'B', 'M' };
            DWORD size = 54 + w * h * 4, wr;
            *(DWORD *)(hdr + 2) = size; *(DWORD *)(hdr + 10) = 54; *(DWORD *)(hdr + 14) = 40;
            *(DWORD *)(hdr + 18) = w; *(DWORD *)(hdr + 22) = h; *(unsigned short *)(hdr + 26) = 1; *(unsigned short *)(hdr + 28) = 32;
            WriteFile(f, hdr, 54, &wr, 0);
            for (unsigned y = h; y-- > 0; ) WriteFile(f, (unsigned char *)lr[1] + (size_t)y * lr[0], w * 4, &wr, 0);
            CloseHandle(f);
        }
        ((HRESULT (WINAPI *)(void *))sv[10])(surf);                                    /* UnlockRect */
    }
    ((DWORD (WINAPI *)(void *))sv[2])(surf);                                           /* Release */
}
typedef HRESULT (WINAPI *present_fn)(void *, void *, void *, void *, void *);
static present_fn g_present;
static HRESULT WINAPI present_hook(void *dev, void *src, void *dst, void *wnd, void *dirty) {
    g_frame++;
    replay_tick();
    if (g_shot[0]) { save_bmp(dev, g_shot); g_shot[0] = 0; }
    return g_present(dev, src, dst, wnd, dirty);
}

static HRESULT WINAPI create_device_hook(void *d3d, unsigned adapter, unsigned type, void *hwnd,
                                         DWORD flags, DWORD *pp, void **out) {
    DWORD w[PP_N];
    HRESULT hr;
    g_d3d = d3d;
    g_hwnd = hwnd ? hwnd : (void *)pp[PP_HWND];
    windowed(pp, w);
    hr = g_create(d3d, adapter, type, hwnd, flags, w, out);
    OutputDebugStringA(hr >= 0 ? "d3d8proxy: CreateDevice forced windowed: ok\n"
                               : "d3d8proxy: CreateDevice forced windowed: FAILED\n");
    if (hr >= 0 && *out) {
        patch_slot(*(void ***)*out, DEV_RESET, (void *)reset_hook, (void **)&g_reset);
        patch_slot(*(void ***)*out, 15, (void *)present_hook, (void **)&g_present);     /* Present */
        static int imports;
        if (!imports++) patch_imports();
    }
    if (GetEnvironmentVariableA("RUGBY08_ORACLE_BACKGROUND", 0, 0)) {
        if (pp[PP_HWND]) ShowWindow((void *)pp[PP_HWND], 7 /* SW_SHOWMINNOACTIVE */);
        if (hwnd && hwnd != (void *)pp[PP_HWND]) ShowWindow(hwnd, 7);
        /* Minimized without activation, the game counted itself inactive and stalled its loading
           (as the native runtime's hidden window did): tell it it is the active app. */
        PostMessageA(g_hwnd, 0x1C /* WM_ACTIVATEAPP */, 1, 0);
        PostMessageA(g_hwnd, 0x06 /* WM_ACTIVATE */, 1 /* WA_ACTIVE */, 0);
        g_force_active = 1;
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
