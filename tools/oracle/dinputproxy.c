/* dinputproxy.c -- dinput.dll for the Wine reference run only (tools/run_wine.sh), built with
 * d3d8proxy.c by tools/oracle/build_proxy.sh; no CRT, no Windows SDK.
 *
 * Loads Wine's dinput.dll and patches IDirectInput::CreateDevice and the devices' GetDeviceState
 * so the input replay held by the d3d8.dll proxy (d3d8proxy_input) reaches the game the way the
 * runtime's DirectInput (runtime/win32/dinput.c) delivers it: replayed keys ORed into the keyboard
 * state, replayed mouse motion and buttons added to the mouse state. A device that is not acquired
 * (the window is minimized in the background) reports the replay alone instead of failing. */
typedef unsigned long DWORD;
typedef void *HMODULE;
typedef long HRESULT;
#define WINAPI __stdcall

__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) unsigned WINAPI GetSystemDirectoryA(char *, unsigned);
__declspec(dllimport) int WINAPI VirtualProtect(void *, DWORD, DWORD, DWORD *);
__declspec(dllimport) void WINAPI OutputDebugStringA(const char *);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char *);

enum { KIND_KEYBOARD = 1, KIND_MOUSE = 2 };
typedef HRESULT (WINAPI *create_fn)(void *, const DWORD *, void **, void *);
typedef HRESULT (WINAPI *state_fn)(void *, DWORD, void *);
typedef void (*input_fn)(unsigned char *, int *, int *, int *);

static create_fn g_create;
static state_fn g_state[4];          /* the real GetDeviceState of each patched vtable */
static void **g_vt[4];
static int g_nvt;
static struct { void *dev; int kind, tx, ty; } g_dev[8];
static int g_ndev;

static input_fn replay(void) {
    static input_fn f;
    if (!f) f = (input_fn)GetProcAddress(GetModuleHandleA("d3d8.dll"), "d3d8proxy_input");
    return f;
}

static HRESULT WINAPI state_hook(void *dev, DWORD cb, void *data) {
    void **vt = *(void ***)dev;
    int v = 0;
    while (v < g_nvt && g_vt[v] != vt) v++;
    HRESULT hr = v < g_nvt ? g_state[v](dev, cb, data) : -1;
    int d = 0;
    while (d < g_ndev && g_dev[d].dev != dev) d++;
    if (d == g_ndev || !replay()) return hr;
    unsigned char *b = (unsigned char *)data;
    if (hr < 0) { for (DWORD i = 0; i < cb; i++) b[i] = 0; hr = 0; }
    if (g_dev[d].kind == KIND_KEYBOARD && cb >= 256) {
        replay()(b, 0, 0, 0);
    } else if (g_dev[d].kind == KIND_MOUSE && cb >= 16) {        /* DIMOUSESTATE: lX lY lZ rgbButtons[4] */
        int tx = 0, ty = 0, btn = 0;
        replay()(0, &tx, &ty, &btn);
        ((long *)b)[0] += tx - g_dev[d].tx;
        ((long *)b)[1] += ty - g_dev[d].ty;
        g_dev[d].tx = tx; g_dev[d].ty = ty;
        if (btn & 1) b[12] = 0x80;
    }
    return hr;
}

static void patch_slot(void **vtbl, int i, void *fn, void **old) {
    DWORD prot;
    if (vtbl[i] == fn) return;
    VirtualProtect(&vtbl[i], sizeof(void *), 0x04 /* PAGE_READWRITE */, &prot);
    *old = vtbl[i];
    vtbl[i] = fn;
    VirtualProtect(&vtbl[i], sizeof(void *), prot, &prot);
}

/* GUID_SysKeyboard {6F1D2B61-...}, GUID_SysMouse {6F1D2B60-...} */
static HRESULT WINAPI create_hook(void *di, const DWORD *guid, void **out, void *outer) {
    HRESULT hr = g_create(di, guid, out, outer);
    if (hr < 0 || !out || !*out) return hr;
    int kind = guid[0] == 0x6F1D2B61u ? KIND_KEYBOARD : guid[0] == 0x6F1D2B60u ? KIND_MOUSE : 0;
    if (!kind || g_ndev >= 8) return hr;
    g_dev[g_ndev].dev = *out; g_dev[g_ndev].kind = kind;
    if (replay()) replay()(0, &g_dev[g_ndev].tx, &g_dev[g_ndev].ty, 0);   /* no motion from before creation */
    g_ndev++;
    void **vt = *(void ***)*out;
    int v = 0;
    while (v < g_nvt && g_vt[v] != vt) v++;
    if (v == g_nvt && g_nvt < 4) {
        g_vt[g_nvt] = vt;
        patch_slot(vt, 9, (void *)state_hook, (void **)&g_state[g_nvt]);   /* GetDeviceState */
        g_nvt++;
    }
    OutputDebugStringA(kind == KIND_KEYBOARD ? "dinputproxy: keyboard\n" : "dinputproxy: mouse\n");
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DirectInputCreateA(void *inst, DWORD version, void **out, void *outer) {
    static HRESULT (WINAPI *real)(void *, DWORD, void **, void *);
    if (!real) {
        char path[300];
        unsigned n = GetSystemDirectoryA(path, 260);
        const char *s = "\\dinput.dll";
        while (*s) path[n++] = *s++;
        path[n] = 0;
        real = (HRESULT (WINAPI *)(void *, DWORD, void **, void *))GetProcAddress(LoadLibraryA(path), "DirectInputCreateA");
        if (!real) { OutputDebugStringA("dinputproxy: system dinput.dll not found\n"); return -1; }
    }
    HRESULT hr = real(inst, version, out, outer);
    if (hr >= 0 && out && *out) patch_slot(*(void ***)*out, 3, (void *)create_hook, (void **)&g_create);   /* CreateDevice */
    return hr;
}
