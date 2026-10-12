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
__declspec(dllimport) void *WINAPI CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, void *);
__declspec(dllimport) int WINAPI ReadFile(void *, void *, DWORD, DWORD *, void *);
__declspec(dllimport) int WINAPI WriteFile(void *, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) int WINAPI CloseHandle(void *);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char *);
__declspec(dllimport) int WINAPI PostMessageA(void *, unsigned, unsigned, long);

/* D3DPRESENT_PARAMETERS (D3D8), DWORD indices */
enum { PP_W, PP_H, PP_FMT, PP_COUNT, PP_MS, PP_SWAP, PP_HWND, PP_WINDOWED, PP_AUTODS, PP_DSFMT,
       PP_FLAGS, PP_REFRESH, PP_INTERVAL, PP_N };
enum { D3D_GETADAPTERDISPLAYMODE = 8, D3D_CREATEDEVICE = 15, DEV_RESET = 14 };

typedef HRESULT (WINAPI *create_device_fn)(void *, unsigned, unsigned, void *, DWORD, DWORD *, void **);
typedef HRESULT (WINAPI *reset_fn)(void *, DWORD *);
typedef HRESULT (WINAPI *display_mode_fn)(void *, unsigned, DWORD *);

static void *g_d3d;                 /* the IDirect3D8 that created the device */
static void *g_hwnd;                /* the game's focus window */
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
    out[PP_FLAGS] |= 1;              /* D3DPRESENTFLAG_LOCKABLE_BACKBUFFER: replay shots read it */
}

static HRESULT WINAPI reset_hook(void *dev, DWORD *pp) {
    DWORD w[PP_N];
    windowed(pp, w);
    OutputDebugStringA("d3d8proxy: Reset forced windowed\n");
    return g_reset(dev, w);
}

/* ---- input replay ------------------------------------------------------------------------- */
enum { EV_KEY, EV_PRESS, EV_MOVE, EV_SHOT };
typedef struct { DWORD frame; int kind, down, x, y; unsigned char vk, scan, ext; char path[64]; } ev_t;
static ev_t g_ev[512];
static int g_nev, g_iev, g_loaded;
static DWORD g_frame;
static unsigned char g_keys[256];   /* DIK state (scan | 0x80 when extended) */
static unsigned char g_vkdown[256];
static int g_mdx, g_mdy, g_mbuttons, g_cx = 320, g_cy = 240;
static char g_shot[64];
static int g_force_active;           /* background run: GetForegroundWindow/GetActiveWindow -> the game */

static int str_eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }
static const char *word(const char *p, char *out, int cap) {
    int n = 0; p = skip_ws(p);
    while (*p && *p != '\n' && *p != '\r' && (cap > 60 || *p != ' ') && n < cap - 1) out[n++] = *p++;
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0; return p;
}
static const char *num(const char *p, int *v) {
    int neg = 0, x = 0; p = skip_ws(p);
    if (*p == '-') neg = 1, p++;
    while (*p >= '0' && *p <= '9') x = x * 10 + (*p++ - '0');
    *v = neg ? -x : x; return p;
}
/* SDL key names the replays use -> (VK, DIK scan, extended) */
static int key_of(const char *k, unsigned char *vk, unsigned char *scan, unsigned char *ext) {
    static const char row1[] = "QWERTYUIOP", row2[] = "ASDFGHJKL", row3[] = "ZXCVBNM";
    *ext = 0;
    if (k[0] >= 'A' && k[0] <= 'Z' && !k[1]) {
        const char *r; int i;
        *vk = (unsigned char)k[0];
        for (r = row1, i = 0; r[i]; i++) if (r[i] == k[0]) { *scan = (unsigned char)(0x10 + i); return 1; }
        for (r = row2, i = 0; r[i]; i++) if (r[i] == k[0]) { *scan = (unsigned char)(0x1E + i); return 1; }
        for (r = row3, i = 0; r[i]; i++) if (r[i] == k[0]) { *scan = (unsigned char)(0x2C + i); return 1; }
    }
    static const struct { const char *n; unsigned char vk, scan, ext; } t[] = {
        { "Return", 0x0D, 0x1C, 0 }, { "Escape", 0x1B, 0x01, 0 }, { "Space", 0x20, 0x39, 0 }, { "Tab", 0x09, 0x0F, 0 },
        { "Up", 0x26, 0x48, 1 }, { "Down", 0x28, 0x50, 1 }, { "Left", 0x25, 0x4B, 1 }, { "Right", 0x27, 0x4D, 1 },
        { "Keypad Enter", 0x0D, 0x1C, 1 }, { "Keypad 6", 0x66, 0x4D, 0 }, { "Backspace", 0x08, 0x0E, 0 },
        { "1", 0x31, 0x02, 0 }, { "2", 0x32, 0x03, 0 }, { "P", 0x50, 0x19, 0 } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (str_eq(k, t[i].n)) { *vk = t[i].vk; *scan = t[i].scan; *ext = t[i].ext; return 1; }
    return 0;
}
static void add(DWORD f, int kind, int down, int x, int y) {
    if (g_nev >= 512) return;
    ev_t *e = &g_ev[g_nev++];
    e->frame = f; e->kind = kind; e->down = down; e->x = x; e->y = y; e->path[0] = 0;
}
static void replay_load(void) {
    static char buf[16384], path[300], act[16], arg[64];
    DWORD n = 0;
    g_loaded = 1;
    if (!GetEnvironmentVariableA("RUGBY08_REPLAY", path, sizeof path)) return;
    void *h = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0, 0);
    if (h == (void *)-1) { OutputDebugStringA("d3d8proxy: replay not found\n"); return; }
    ReadFile(h, buf, sizeof buf - 1, &n, 0);
    CloseHandle(h);
    buf[n] = 0;
    for (const char *p = buf; *p; ) {
        int f, x, y;
        const char *q = skip_ws(p);
        if (*q >= '0' && *q <= '9') {
            q = num(q, &f);
            q = word(q, act, sizeof act);
            if (str_eq(act, "tap") || str_eq(act, "down") || str_eq(act, "up")) {
                unsigned char vk, sc, ex;
                word(q, arg, 61);
                if (key_of(arg, &vk, &sc, &ex)) {
                    int both = str_eq(act, "tap");
                    if (both || str_eq(act, "down")) { add((DWORD)f, EV_KEY, 1, 0, 0); g_ev[g_nev - 1].vk = vk; g_ev[g_nev - 1].scan = sc; g_ev[g_nev - 1].ext = ex; }
                    if (both || str_eq(act, "up")) { add((DWORD)(both ? f + 3 : f), EV_KEY, 0, 0, 0); g_ev[g_nev - 1].vk = vk; g_ev[g_nev - 1].scan = sc; g_ev[g_nev - 1].ext = ex; }
                }
            } else if (str_eq(act, "press")) {
                int hold = 3; num(q, &hold);
                add((DWORD)f, EV_PRESS, 1, -1, -1); add((DWORD)(f + (hold > 0 ? hold : 3)), EV_PRESS, 0, -1, -1);
            } else if (str_eq(act, "click")) {
                q = num(q, &x); num(q, &y);
                add((DWORD)f, EV_PRESS, 1, x, y); add((DWORD)(f + 3), EV_PRESS, 0, x, y);
            } else if (str_eq(act, "move")) {
                q = num(q, &x); num(q, &y);
                add((DWORD)f, EV_MOVE, 0, x, y);
            } else if (str_eq(act, "shot")) {
                word(q, arg, 63);
                add((DWORD)f, EV_SHOT, 0, 0, 0);
                const char *b = arg; for (const char *c = arg; *c; c++) if (*c == '/' || *c == '\\') b = c + 1;
                int i = 0; while (b[i] && b[i] != '.' && i < 59) { g_ev[g_nev - 1].path[i] = b[i]; i++; }
                g_ev[g_nev - 1].path[i] = 0;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    for (int i = 1; i < g_nev; i++)                                    /* stable sort by frame */
        for (int j = i; j > 0 && g_ev[j - 1].frame > g_ev[j].frame; j--) { ev_t t = g_ev[j]; g_ev[j] = g_ev[j - 1]; g_ev[j - 1] = t; }
    OutputDebugStringA("d3d8proxy: replay loaded\n");
}
static void replay_tick(void) {
    if (!g_loaded) replay_load();
    for (; g_iev < g_nev && g_ev[g_iev].frame <= g_frame; g_iev++) {
        ev_t *e = &g_ev[g_iev];
        if (e->kind == EV_KEY) {
            unsigned char dik = (unsigned char)(e->scan | (e->ext ? 0x80 : 0));
            g_keys[dik] = e->down ? 0x80 : 0;
            g_vkdown[e->vk] = (unsigned char)e->down;
            long lp = 1 | ((long)e->scan << 16) | ((long)e->ext << 24) | (e->down ? 0 : (long)0xC0000000);
            if (g_hwnd) PostMessageA(g_hwnd, e->down ? 0x100 : 0x101, e->vk, lp);   /* WM_KEYDOWN / WM_KEYUP */
        } else if (e->kind == EV_PRESS) {
            if (e->x >= 0) { g_cx = e->x; g_cy = e->y; }
            g_mbuttons = e->down ? 1 : 0;
            g_vkdown[1] = (unsigned char)e->down;                                     /* VK_LBUTTON */
            if (g_hwnd) PostMessageA(g_hwnd, e->down ? 0x201 : 0x202, e->down ? 1 : 0, (long)((g_cy << 16) | (g_cx & 0xFFFF)));
        } else if (e->kind == EV_MOVE) {
            g_mdx += e->x; g_mdy += e->y;
            g_cx += e->x; g_cy += e->y;
            if (g_cx < 0) g_cx = 0; if (g_cy < 0) g_cy = 0; if (g_cx > 639) g_cx = 639; if (g_cy > 479) g_cy = 479;
            if (g_hwnd) PostMessageA(g_hwnd, 0x200, g_mbuttons, (long)((g_cy << 16) | (g_cx & 0xFFFF)));   /* WM_MOUSEMOVE */
        } else if (e->kind == EV_SHOT) {
            for (int i = 0; i < 64; i++) g_shot[i] = e->path[i];
        }
    }
}
/* the dinput.dll proxy merges this into GetDeviceState: DIK keys ORed in, and the replay's total mouse
 * motion (each device reports its delta against the total it saw last, as DirectInput does) */
__declspec(dllexport) void d3d8proxy_input(unsigned char *keys, int *tx, int *ty, int *buttons) {
    if (!g_loaded) replay_load();
    if (keys) for (int i = 0; i < 256; i++) keys[i] |= g_keys[i];
    if (tx) *tx = g_mdx;
    if (ty) *ty = g_mdy;
    if (buttons) *buttons = g_mbuttons;
}
typedef void *(WINAPI *wnd_fn)(void);
static wnd_fn g_fg, g_active, g_focus;
static void *WINAPI fg_hook(void) { return g_force_active && g_hwnd ? g_hwnd : g_fg(); }
static void *WINAPI active_hook(void) { return g_force_active && g_hwnd ? g_hwnd : g_active(); }
static void *WINAPI focus_hook(void) { return g_force_active && g_hwnd ? g_hwnd : g_focus(); }
typedef short (WINAPI *keystate_fn)(int);
static keystate_fn g_async, g_keystate;
static short WINAPI async_hook(int vk) { return (short)((vk >= 0 && vk < 256 && g_vkdown[vk]) ? 0x8000 : 0) | g_async(vk); }
static short WINAPI keystate_hook(int vk) { return (short)((vk >= 0 && vk < 256 && g_vkdown[vk]) ? 0x8000 : 0) | g_keystate(vk); }
/* the game's import table entries for USER32 GetAsyncKeyState/GetKeyState -> the hooks above */
static void patch_imports(void) {
    unsigned char *b = (unsigned char *)GetModuleHandleA(0);
    unsigned char *nt = b + *(DWORD *)(b + 0x3C);
    DWORD rva = *(DWORD *)(nt + 0x80);                                 /* import directory */
    for (DWORD *d = (DWORD *)(b + rva); d[3]; d += 5) {
        const char *dll = (const char *)(b + d[3]);
        if (!((dll[0] | 32) == 'u' && (dll[1] | 32) == 's' && (dll[2] | 32) == 'e' && (dll[3] | 32) == 'r')) continue;
        DWORD *names = (DWORD *)(b + (d[0] ? d[0] : d[4])), *iat = (DWORD *)(b + d[4]);
        for (int i = 0; names[i]; i++) {
            if (names[i] & 0x80000000u) continue;
            const char *fn = (const char *)(b + names[i] + 2);
            if (str_eq(fn, "GetAsyncKeyState")) patch_slot((void **)iat, i, (void *)async_hook, (void **)&g_async);
            else if (str_eq(fn, "GetKeyState")) patch_slot((void **)iat, i, (void *)keystate_hook, (void **)&g_keystate);
            else if (str_eq(fn, "GetForegroundWindow")) patch_slot((void **)iat, i, (void *)fg_hook, (void **)&g_fg);
            else if (str_eq(fn, "GetActiveWindow")) patch_slot((void **)iat, i, (void *)active_hook, (void **)&g_active);
            else if (str_eq(fn, "GetFocus")) patch_slot((void **)iat, i, (void *)focus_hook, (void **)&g_focus);
        }
    }
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
