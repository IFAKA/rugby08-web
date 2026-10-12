/* k32_sys.c -- version/system info, modules, command line & environment, std handles, console,
 * process, errors and debugging. Code pages and locale: k32_locale.c; time: k32_time.c. */
#include "win32.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ---- version / system ------------------------------------------------------------------------ */
HLE(GetVersionExA) {   /* Windows XP SP2 */
    uint32_t p = a[0], sz = rd32(p);
    wr32(p + 4, 5);
    wr32(p + 8, 1);
    wr32(p + 12, 2600);
    wr32(p + 16, 2);
    gstr_put(p + 20, "Service Pack 2", 128);
    if (sz >= 156) {
        wr16(p + 148, 2);
        wr16(p + 150, 0);
        wr16(p + 152, 0x100);
        wr8(p + 154, 1);
        wr8(p + 155, 0);
    }
    return TRUE_;
}

HLE(GetSystemInfo) {   /* SYSTEM_INFO, 36 bytes: one Pentium Pro class CPU (runtime/DESIGN.md) */
    uint32_t p = a[0];
    memset(G2H(p), 0, 36);
    wr16(p + 0, 0);             /* PROCESSOR_ARCHITECTURE_INTEL */
    wr32(p + 4, 4096);          /* page size */
    wr32(p + 8, 0x10000);
    wr32(p + 12, 0x7FFEFFFF);
    wr32(p + 16, 1);            /* active processor mask */
    wr32(p + 20, 1);            /* number of processors */
    wr32(p + 24, 586);          /* PROCESSOR_INTEL_PENTIUM */
    wr32(p + 28, 0x10000);      /* allocation granularity */
    wr16(p + 32, 6);
    wr16(p + 34, 0x0109);
    return 0;
}

/* PF_RDTSC_INSTRUCTION_AVAILABLE (8) and PF_COMPARE_EXCHANGE_DOUBLE (2) only: no MMX/SSE/3DNow! */
HLE(IsProcessorFeaturePresent) { return (a[0] == 8 || a[0] == 2) ? TRUE_ : FALSE_; }

/* ---- modules ------------------------------------------------------------------------------------ */
typedef struct {
    const char *name;
    uint32_t base;
} module_t;
static module_t g_mods[64] = {
    { "kernel32.dll", 0x7C800000u }, { "user32.dll", 0x77D40000u }, { "gdi32.dll", 0x77F10000u },
    { "advapi32.dll", 0x77DD0000u }, { "winmm.dll", 0x76B40000u },  { "d3d8.dll", 0x6D9A0000u },
    { "dsound.dll", 0x73F10000u },   { "dinput.dll", 0x5F580000u }, { "ddraw.dll", 0x73760000u },
    { "ntdll.dll", 0x7C900000u },
};
static int g_nmods = 10;
static pthread_mutex_t g_modmu = PTHREAD_MUTEX_INITIALIZER;

static const char *base_name(const char *p) {
    const char *b = p;
    for (const char *s = p; *s; s++)
        if (*s == '\\' || *s == '/') b = s + 1;
    return b;
}

static uint32_t module_lookup(const char *name, int add) {
    char n[260];
    snprintf(n, sizeof n, "%s", base_name(name));
    if (!strchr(n, '.')) strncat(n, ".dll", sizeof n - strlen(n) - 1);
    pthread_mutex_lock(&g_modmu);
    for (int i = 0; i < g_nmods; i++)
        if (!strcasecmp(g_mods[i].name, n)) {
            uint32_t b = g_mods[i].base;
            pthread_mutex_unlock(&g_modmu);
            return b;
        }
    uint32_t b = 0;
    if (add && g_nmods < 64) {
        b = 0x10000000u + 0x100000u * (uint32_t)g_nmods;
        g_mods[g_nmods++] = (module_t){ strdup(n), b };
    }
    pthread_mutex_unlock(&g_modmu);
    return b;
}

HLE(GetModuleHandleA) {
    if (!a[0]) return RT_IMAGE_BASE;
    uint32_t b = module_lookup(gstr(a[0]), 0);
    if (!b) set_err(126 /* ERROR_MOD_NOT_FOUND */);
    return b;
}

/* Only DLLs whose functions we can serve "load"; others (DRM leftovers, d3d9, ...) fail so the
 * game takes its fallback. Every attempt is logged. */
HLE(LoadLibraryA) {
    const char *n = gstr(a[0]);
    uint32_t b = module_lookup(n, 0);
    rt_log("LoadLibraryA(\"%s\") -> 0x%08X", n, b);
    if (!b) set_err(126);
    return b;
}
HLE(FreeLibrary) {
    (void)a;
    return TRUE_;
}

HLE(GetProcAddress) {
    if (a[1] < 0x10000u) {
        rt_log("GetProcAddress(0x%08X, #%u) -> 0", a[0], a[1]);
        return 0;
    }
    const char *n = gstr(a[1]);
    for (uint32_t i = 0; i < rt_import_count; i++)
        if (!strcmp(rt_imports[i].name, n)) return RT_BRIDGE_IMPORT + 16 * i;
    rt_log("GetProcAddress(0x%08X, \"%s\") -> 0 (not provided)", a[0], n);
    return set_err(127 /* ERROR_PROC_NOT_FOUND */);
}

HLE(GetModuleFileNameA) {
    const char *p = GUEST_GAME_ROOT "\\Rugby08.exe";
    if (a[0] && a[0] != RT_IMAGE_BASE) p = "C:\\WINDOWS\\system32\\unknown.dll";
    uint32_t n = gstr_put(a[1], p, a[2]);
    return n;
}

/* ---- command line, environment, startup ---------------------------------------------------------- */
HLE(GetCommandLineA) {
    (void)a;
    static uint32_t s;
    if (!s) s = gstrdup("\"" GUEST_GAME_ROOT "\\Rugby08.exe\"");
    return s;
}

static const char *g_env[] = { "ALLUSERSPROFILE=C:\\Documents and Settings\\All Users",
                               "APPDATA=C:\\Documents and Settings\\Player\\Application Data",
                               "COMPUTERNAME=RUGBY08",
                               "HOMEDRIVE=C:",
                               "HOMEPATH=\\Documents and Settings\\Player",
                               "NUMBER_OF_PROCESSORS=1",
                               "OS=Windows_NT",
                               "PATH=C:\\WINDOWS\\system32;C:\\WINDOWS",
                               "SystemDrive=C:",
                               "SystemRoot=C:\\WINDOWS",
                               "TEMP=C:\\WINDOWS\\Temp",
                               "TMP=C:\\WINDOWS\\Temp",
                               "USERNAME=Player",
                               "USERPROFILE=C:\\Documents and Settings\\Player",
                               "windir=C:\\WINDOWS",
                               NULL };

HLE(GetEnvironmentStrings) {
    (void)a;
    uint32_t n = 1;
    for (int i = 0; g_env[i]; i++) n += (uint32_t)strlen(g_env[i]) + 1;
    uint32_t p = gcalloc(n), o = p;
    for (int i = 0; g_env[i]; i++) { o += gstr_put(o, g_env[i], 0xFFFF) + 1; }
    return p;
}
HLE(GetEnvironmentStringsW) {
    (void)a;
    uint32_t n = 1;
    for (int i = 0; g_env[i]; i++) n += (uint32_t)strlen(g_env[i]) + 1;
    uint32_t p = gcalloc(2 * n), o = p;
    for (int i = 0; g_env[i]; i++) {
        for (const char *s = g_env[i]; *s; s++, o += 2) wr16(o, (uint8_t)*s);
        o += 2;
    }
    return p;
}
HLE(FreeEnvironmentStringsA) {
    gfree(a[0]);
    return TRUE_;
}
HLE(FreeEnvironmentStringsW) {
    gfree(a[0]);
    return TRUE_;
}
HLE(SetEnvironmentVariableA) {
    (void)a;
    return TRUE_;
}

HLE(GetStartupInfoA) {   /* STARTUPINFOA, 68 bytes */
    memset(G2H(a[0]), 0, 68);
    wr32(a[0], 68);
    return 0;
}

/* ---- std handles / console -------------------------------------------------------------------- */
static uint32_t g_std[3];
HLE(GetStdHandle) {
    int i = (int)(0xFFFFFFF6u - a[0]);   /* -10, -11, -12 */
    if (i < 0 || i > 2) return INVALID_HANDLE;
    if (!g_std[i]) {
        g_std[i] = h_new(H_STD);
        h_get(g_std[i], H_STD)->count = i;
    }
    return g_std[i];
}
HLE(SetStdHandle) {
    (void)a;
    return TRUE_;
}
HLE(SetHandleCount) { return a[0]; }
HLE(GetConsoleMode) {
    (void)a;
    return FALSE_;
}
HLE(SetConsoleMode) {
    (void)a;
    return FALSE_;
}
HLE(GetNumberOfConsoleInputEvents) {
    if (a[1]) wr32(a[1], 0);
    return FALSE_;
}
HLE(PeekConsoleInputA) {
    if (a[3]) wr32(a[3], 0);
    return FALSE_;
}
HLE(ReadConsoleInputA) {
    if (a[3]) wr32(a[3], 0);
    return FALSE_;
}

/* ---- processes / errors / debugging -------------------------------------------------------------- */
HLE(GetCurrentProcess) {
    (void)a;
    return 0xFFFFFFFFu;
}
HLE(GetCurrentProcessId) {
    (void)a;
    return 0x1000;
}
HLE(ExitProcess) {
    rt_trace("exit: ExitProcess(%u)", a[0]);
    exit((int)a[0]);
}
HLE(TerminateProcess) {
    rt_trace("exit: TerminateProcess(%u)", a[1]);
    exit((int)a[1]);
}
HLE(GetLastError) {
    (void)a;
    return rt_last_error;
}
HLE(SetLastError) {
    rt_last_error = a[0];
    return 0;
}
HLE(IsDebuggerPresent) {
    (void)a;
    return FALSE_;
}
HLE(DebugBreak) {   /* the CRT's /RTC failure path ends here: the trace ring shows the failing call */
    static int n;
    (void)a;
    rt_log("DebugBreak() in sub_%08X", g_cur_func);
    if (n++ < 4) recomp_dump_trace("DebugBreak");
    return 0;
}
HLE(OutputDebugStringA) {
    const char *s = gstr(a[0]);
    size_t n = s ? strlen(s) : 0;
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
    rt_log("ODS: %.*s", (int)n, s ? s : "");
    return 0;
}
HLE(FormatMessageA) {
    char msg[64];
    snprintf(msg, sizeof msg, "Error %u.\r\n", a[2]);
    if (a[0] & 0x100 /* FORMAT_MESSAGE_ALLOCATE_BUFFER */) {
        uint32_t p = gstrdup(msg);
        wr32(a[4], p);
        return (uint32_t)strlen(msg);
    }
    return gstr_put(a[4], msg, a[5]);
}

static uint32_t g_unhandled_filter;
HLE(SetUnhandledExceptionFilter) {
    uint32_t o = g_unhandled_filter;
    g_unhandled_filter = a[0];
    return o;
}
HLE(UnhandledExceptionFilter) {
    (void)a;
    rt_log("UnhandledExceptionFilter called");
    return 1;
}

/* RaiseException / RtlUnwind: k32_eh.c */
