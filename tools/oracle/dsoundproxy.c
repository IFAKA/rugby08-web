/* dsoundproxy.c -- dsound.dll for the Wine reference run only (tools/run_wine.sh), built with the
 * other proxies by tools/oracle/build_proxy.sh; no CRT, no Windows SDK.
 *
 * Mutes the original game while keeping a real DirectSound under it: with Wine's audio driver
 * disabled the game had no sound device and stalled on its loading screen. Every buffer is created
 * with DSBCAPS_CTRLVOLUME, set to DSBVOLUME_MIN, and later SetVolume calls are held there. With
 * RUGBY08_ORACLE_SOUND set it forwards untouched (tools/run_wine.sh --sound / --keep). */
typedef unsigned long DWORD;
typedef void *HMODULE;
typedef long HRESULT;
#define WINAPI __stdcall

__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) unsigned WINAPI GetSystemDirectoryA(char *, unsigned);
__declspec(dllimport) int WINAPI VirtualProtect(void *, DWORD, DWORD, DWORD *);
__declspec(dllimport) DWORD WINAPI GetEnvironmentVariableA(const char *, char *, DWORD);

typedef HRESULT (WINAPI *create_buf_fn)(void *, const DWORD *, void **, void *);
typedef HRESULT (WINAPI *set_volume_fn)(void *, long);
static create_buf_fn g_create_buf;
static set_volume_fn g_set_volume[4];
static void **g_buf_vt[4];
static int g_nbuf_vt;
static HMODULE g_real;

static void patch_slot(void **vtbl, int i, void *fn, void **old) {
    DWORD prot;
    if (vtbl[i] == fn) return;
    VirtualProtect(&vtbl[i], sizeof(void *), 0x04 /* PAGE_READWRITE */, &prot);
    *old = vtbl[i];
    vtbl[i] = fn;
    VirtualProtect(&vtbl[i], sizeof(void *), prot, &prot);
}

static set_volume_fn real_set_volume(void *buf) {
    void **vt = *(void ***)buf;
    for (int i = 0; i < g_nbuf_vt; i++) if (g_buf_vt[i] == vt) return g_set_volume[i];
    return 0;
}

static HRESULT WINAPI set_volume_hook(void *buf, long vol) {
    (void)vol;
    set_volume_fn f = real_set_volume(buf);
    return f ? f(buf, -10000) : 0;                                   /* DSBVOLUME_MIN */
}

/* IDirectSound::CreateSoundBuffer(this, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) */
static HRESULT WINAPI create_buf_hook(void *ds, const DWORD *desc, void **out, void *outer) {
    DWORD d[16] = {0};
    DWORD n = desc && desc[0] <= sizeof d ? desc[0] / 4 : 0;
    for (DWORD i = 0; i < n; i++) d[i] = desc[i];
    if (n > 1) d[1] |= 0x80;                                         /* DSBCAPS_CTRLVOLUME */
    HRESULT hr = g_create_buf(ds, n > 1 ? d : desc, out, outer);
    if (hr < 0 || !out || !*out) return hr;
    void **vt = *(void ***)*out;
    int v = 0;
    while (v < g_nbuf_vt && g_buf_vt[v] != vt) v++;
    if (v == g_nbuf_vt && g_nbuf_vt < 4) {
        g_buf_vt[g_nbuf_vt] = vt;
        patch_slot(vt, 15, (void *)set_volume_hook, (void **)&g_set_volume[g_nbuf_vt]);   /* SetVolume */
        g_nbuf_vt++;
    }
    set_volume_hook(*out, 0);
    return hr;
}

static void *real(const char *name) {
    if (!g_real) {
        char path[300];
        unsigned n = GetSystemDirectoryA(path, 260);
        const char *s = "\\dsound.dll";
        while (*s) path[n++] = *s++;
        path[n] = 0;
        g_real = LoadLibraryA(path);
    }
    return g_real ? GetProcAddress(g_real, name) : 0;
}

__declspec(dllexport) HRESULT WINAPI DirectSoundCreate(const void *guid, void **out, void *outer) {
    HRESULT (WINAPI *f)(const void *, void **, void *) = (HRESULT (WINAPI *)(const void *, void **, void *))real("DirectSoundCreate");
    if (!f) return -1;
    HRESULT hr = f(guid, out, outer);
    if (hr >= 0 && out && *out && !GetEnvironmentVariableA("RUGBY08_ORACLE_SOUND", 0, 0))
        patch_slot(*(void ***)*out, 3, (void *)create_buf_hook, (void **)&g_create_buf);   /* CreateSoundBuffer */
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DirectSoundEnumerateA(void *cb, void *ctx) {
    HRESULT (WINAPI *f)(void *, void *) = (HRESULT (WINAPI *)(void *, void *))real("DirectSoundEnumerateA");
    return f ? f(cb, ctx) : -1;
}
