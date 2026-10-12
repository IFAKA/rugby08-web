/* d3d8proxy.h -- what the d3d8.dll proxy's sources share (d3d8proxy.c: device hooks; d3d8proxy_replay.c:
 * input replay). No CRT, no Windows SDK: the few declarations needed are here. */
#ifndef D3D8PROXY_H
#define D3D8PROXY_H
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

extern void *g_hwnd;          /* the game's focus window */
extern int g_force_active;     /* background run: the game is told it is the active window */
extern unsigned long g_frame;  /* frames presented (the runtime's count) */
extern char g_shot[64];        /* replay `shot`: save the back buffer as this name before Present */
void patch_slot(void **vtbl, int i, void *fn, void **old);
void replay_tick(void);
void patch_imports(void);

#endif
