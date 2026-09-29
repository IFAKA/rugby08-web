/* k32_sys.c -- version/system info, modules, command line & environment, std handles,
 * code pages / locale / string typing, time, errors, debugging, exceptions, console. */
#include "win32.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* ---- version / system ------------------------------------------------------------------------ */
HLE(GetVersionExA) {   /* Windows XP SP2 */
    uint32_t p = a[0], sz = rd32(p);
    wr32(p + 4, 5); wr32(p + 8, 1); wr32(p + 12, 2600); wr32(p + 16, 2);
    gstr_put(p + 20, "Service Pack 2", 128);
    if (sz >= 156) { wr16(p + 148, 2); wr16(p + 150, 0); wr16(p + 152, 0x100); wr8(p + 154, 1); wr8(p + 155, 0); }
    return TRUE_;
}

HLE(GetSystemInfo) {   /* SYSTEM_INFO, 36 bytes: one Pentium Pro class CPU (runtime/DESIGN.md) */
    uint32_t p = a[0];
    memset(G2H(p), 0, 36);
    wr16(p + 0, 0);             /* PROCESSOR_ARCHITECTURE_INTEL */
    wr32(p + 4, 4096);          /* page size */
    wr32(p + 8, 0x10000);  wr32(p + 12, 0x7FFEFFFF);
    wr32(p + 16, 1);            /* active processor mask */
    wr32(p + 20, 1);            /* number of processors */
    wr32(p + 24, 586);          /* PROCESSOR_INTEL_PENTIUM */
    wr32(p + 28, 0x10000);      /* allocation granularity */
    wr16(p + 32, 6); wr16(p + 34, 0x0109);
    return 0;
}

/* PF_RDTSC_INSTRUCTION_AVAILABLE (8) and PF_COMPARE_EXCHANGE_DOUBLE (2) only: no MMX/SSE/3DNow! */
HLE(IsProcessorFeaturePresent) { return (a[0] == 8 || a[0] == 2) ? TRUE_ : FALSE_; }

/* ---- modules ------------------------------------------------------------------------------------ */
typedef struct { const char *name; uint32_t base; } module_t;
static module_t g_mods[64] = {
    { "kernel32.dll", 0x7C800000u }, { "user32.dll", 0x77D40000u }, { "gdi32.dll", 0x77F10000u },
    { "advapi32.dll", 0x77DD0000u }, { "winmm.dll", 0x76B40000u }, { "d3d8.dll", 0x6D9A0000u },
    { "dsound.dll", 0x73F10000u }, { "dinput.dll", 0x5F580000u }, { "ddraw.dll", 0x73760000u },
    { "ntdll.dll", 0x7C900000u },
};
static int g_nmods = 10;
static pthread_mutex_t g_modmu = PTHREAD_MUTEX_INITIALIZER;

static const char *base_name(const char *p) {
    const char *b = p;
    for (const char *s = p; *s; s++) if (*s == '\\' || *s == '/') b = s + 1;
    return b;
}

static uint32_t module_lookup(const char *name, int add) {
    char n[260];
    snprintf(n, sizeof n, "%s", base_name(name));
    if (!strchr(n, '.')) strncat(n, ".dll", sizeof n - strlen(n) - 1);
    pthread_mutex_lock(&g_modmu);
    for (int i = 0; i < g_nmods; i++)
        if (!strcasecmp(g_mods[i].name, n)) { uint32_t b = g_mods[i].base; pthread_mutex_unlock(&g_modmu); return b; }
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
HLE(FreeLibrary) { (void)a; return TRUE_; }

HLE(GetProcAddress) {
    if (a[1] < 0x10000u) { rt_log("GetProcAddress(0x%08X, #%u) -> 0", a[0], a[1]); return 0; }
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
    "APPDATA=C:\\Documents and Settings\\Player\\Application Data", "COMPUTERNAME=RUGBY08",
    "HOMEDRIVE=C:", "HOMEPATH=\\Documents and Settings\\Player", "NUMBER_OF_PROCESSORS=1",
    "OS=Windows_NT", "PATH=C:\\WINDOWS\\system32;C:\\WINDOWS", "SystemDrive=C:",
    "SystemRoot=C:\\WINDOWS", "TEMP=C:\\WINDOWS\\Temp", "TMP=C:\\WINDOWS\\Temp",
    "USERNAME=Player", "USERPROFILE=C:\\Documents and Settings\\Player", "windir=C:\\WINDOWS", NULL };

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
HLE(FreeEnvironmentStringsA) { gfree(a[0]); return TRUE_; }
HLE(FreeEnvironmentStringsW) { gfree(a[0]); return TRUE_; }
HLE(SetEnvironmentVariableA) { (void)a; return TRUE_; }

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
    if (!g_std[i]) { g_std[i] = h_new(H_STD); h_get(g_std[i], H_STD)->count = i; }
    return g_std[i];
}
HLE(SetStdHandle) { (void)a; return TRUE_; }
HLE(SetHandleCount) { return a[0]; }
HLE(GetConsoleMode) { (void)a; return FALSE_; }
HLE(SetConsoleMode) { (void)a; return FALSE_; }
HLE(GetNumberOfConsoleInputEvents) { if (a[1]) wr32(a[1], 0); return FALSE_; }
HLE(PeekConsoleInputA) { if (a[3]) wr32(a[3], 0); return FALSE_; }
HLE(ReadConsoleInputA) { if (a[3]) wr32(a[3], 0); return FALSE_; }

/* ---- processes / errors / debugging -------------------------------------------------------------- */
HLE(GetCurrentProcess) { (void)a; return 0xFFFFFFFFu; }
HLE(GetCurrentProcessId) { (void)a; return 0x1000; }
HLE(ExitProcess) { rt_trace("exit: ExitProcess(%u)", a[0]); exit((int)a[0]); }
HLE(TerminateProcess) { rt_trace("exit: TerminateProcess(%u)", a[1]); exit((int)a[1]); }
HLE(GetLastError) { (void)a; return rt_last_error; }
HLE(SetLastError) { rt_last_error = a[0]; return 0; }
HLE(IsDebuggerPresent) { (void)a; return FALSE_; }
HLE(DebugBreak) { (void)a; rt_log("DebugBreak() in sub_%08X", g_cur_func); return 0; }
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
HLE(SetUnhandledExceptionFilter) { uint32_t o = g_unhandled_filter; g_unhandled_filter = a[0]; return o; }
HLE(UnhandledExceptionFilter) { (void)a; rt_log("UnhandledExceptionFilter called"); return 1; }

/* C++ exceptions and SEH unwinding: not emulated yet. Fail loudly with the exception so the
 * first real use is diagnosed (0xE06D7363 = MSVC C++ throw; a[3] points at its arguments). */
HLE(RaiseException) {
    uint32_t code = a[0], nargs = a[2], args = a[3];
    if (code == 0xE06D7363u && nargs >= 3) {
        uint32_t tinfo = rd32(args + 8), ctarr = tinfo ? rd32(tinfo + 12) : 0;
        uint32_t ct = ctarr ? rd32(ctarr + 4) : 0, td = ct ? rd32(ct + 4) : 0;
        rt_fatal("C++ exception thrown (type %s) from sub_%08X -- SEH not emulated yet",
                 td ? gstr(td + 8) : "?", g_cur_func);
    }
    rt_fatal("RaiseException(0x%08X) from sub_%08X -- SEH not emulated yet", code, g_cur_func);
}
HLE(RtlUnwind) { (void)a; rt_fatal("RtlUnwind from sub_%08X -- SEH not emulated yet", g_cur_func); }

/* ---- code pages ---------------------------------------------------------------------------------- */
HLE(GetACP) { (void)a; return 1252; }
HLE(GetOEMCP) { (void)a; return 437; }
HLE(IsValidCodePage) { return (a[0] == 1252 || a[0] == 437 || a[0] == 65001) ? TRUE_ : FALSE_; }
HLE(GetCPInfo) {   /* CPINFO: MaxCharSize(4) DefaultChar[2] LeadByte[12] */
    memset(G2H(a[1]), 0, 18);
    wr32(a[1], 1); wr8(a[1] + 4, '?');
    return TRUE_;
}

static uint32_t mb_len(uint32_t s, int32_t n) { return n >= 0 ? (uint32_t)n : gstr_len(s) + 1; }

HLE(MultiByteToWideChar) {   /* (cp, flags, src, srclen, dst, dstlen) */
    uint32_t src = a[2], dst = a[4], cap = a[5];
    uint32_t n = mb_len(src, (int32_t)a[3]);
    if (!cap) return n;
    if (cap < n) { set_err(ERROR_INSUFFICIENT_BUFFER_); n = cap; }
    for (uint32_t i = 0; i < n; i++) wr16(dst + 2 * i, cp1252_to_u16(rd8(src + i)));
    return n;
}

HLE(WideCharToMultiByte) {   /* (cp, flags, src, srclen, dst, dstlen, defchar, useddef) */
    uint32_t src = a[2], dst = a[4], cap = a[5];
    int32_t sl = (int32_t)a[3];
    uint32_t n = 0;
    if (sl < 0) { while (rd16(src + 2 * n)) n++; n++; } else n = (uint32_t)sl;
    if (a[7]) wr32(a[7], 0);
    if (!cap) return n;
    if (cap < n) { set_err(ERROR_INSUFFICIENT_BUFFER_); n = cap; }
    for (uint32_t i = 0; i < n; i++) {
        int c = u16_to_cp1252(rd16(src + 2 * i));
        if (c < 0) { c = a[6] ? rd8(a[6]) : '?'; if (a[7]) wr32(a[7], 1); }
        wr8(dst + i, (uint8_t)c);
    }
    return n;
}

/* ---- locale ---------------------------------------------------------------------------------------- */
HLE(GetUserDefaultLCID) { (void)a; return 0x0409; }
HLE(IsValidLocale) { (void)a; return TRUE_; }
HLE(EnumSystemLocalesA) { if (a[0]) guest_call(a[0], 1, gstrdup("00000409")); return TRUE_; }

static const char *locale_str(uint32_t type) {
    switch (type & 0xFFFF) {
    case 0x01: return "0409";           /* LOCALE_ILANGUAGE */
    case 0x03: return "ENU";            /* LOCALE_SABBREVLANGNAME */
    case 0x07: return "USA";            /* LOCALE_SABBREVCTRYNAME */
    case 0x0B: return "437";            /* LOCALE_IDEFAULTCODEPAGE */
    case 0x0E: return ".";              /* LOCALE_SDECIMAL */
    case 0x0F: return ",";              /* LOCALE_STHOUSAND */
    case 0x1004: return "1252";         /* LOCALE_IDEFAULTANSICODEPAGE */
    case 0x1001: return "English";      /* LOCALE_SENGLANGUAGE */
    case 0x1002: return "United States";
    case 0x14: return "$";
    case 0x1E: return ":";
    case 0x1D: return "/";
    default: return "";
    }
}
HLE(GetLocaleInfoA) {
    const char *s = locale_str(a[1]);
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (!a[3]) return n;
    if (a[3] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    gstr_put(a[2], s, a[3]);
    return n;
}
HLE(GetLocaleInfoW) {
    const char *s = locale_str(a[1]);
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (!a[3]) return n;
    if (a[3] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (uint32_t i = 0; i < n; i++) wr16(a[2] + 2 * i, (uint8_t)s[i]);
    return n;
}

/* C1 character types (GetStringType CT_CTYPE1) for code page 1252 / Latin-1 */
static uint16_t ctype1(uint16_t c) {
    uint16_t t = 0;
    if (c < 128) {
        if (isupper(c)) t |= 0x001;
        if (islower(c)) t |= 0x002;
        if (isdigit(c)) t |= 0x004;
        if (isspace(c)) t |= 0x008;
        if (ispunct(c)) t |= 0x010;
        if (iscntrl(c)) t |= 0x020;
        if (c == ' ') t |= 0x040;
        if (isxdigit(c)) t |= 0x080;
        if (isalpha(c)) t |= 0x100;
    } else if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7) {
        t = 0x100 | (c < 0xDF ? 0x001 : 0x002);
    } else if (c >= 0xA0) t = 0x010;
    return t;
}
HLE(GetStringTypeW) {   /* (type, src, len, out) */
    int32_t n = (int32_t)a[2];
    if (n < 0) { n = 0; while (rd16(a[1] + 2 * (uint32_t)n)) n++; n++; }
    for (int32_t i = 0; i < n; i++) wr16(a[3] + 2 * (uint32_t)i, ctype1(rd16(a[1] + 2 * (uint32_t)i)));
    return TRUE_;
}
HLE(GetStringTypeA) {   /* (lcid, type, src, len, out) */
    int32_t n = (int32_t)a[3];
    if (n < 0) n = (int32_t)gstr_len(a[2]) + 1;
    for (int32_t i = 0; i < n; i++) wr16(a[4] + 2 * (uint32_t)i, ctype1(cp1252_to_u16(rd8(a[2] + (uint32_t)i))));
    return TRUE_;
}

static uint16_t map_case(uint16_t c, uint32_t flags) {
    if (flags & 0x100) { if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 32; }   /* LCMAP_LOWERCASE */
    if (flags & 0x200) { if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 32; }   /* LCMAP_UPPERCASE */
    return c;
}
HLE(LCMapStringW) {   /* (lcid, flags, src, srclen, dst, dstlen) */
    int32_t n = (int32_t)a[3];
    if (n < 0) { n = 0; while (rd16(a[2] + 2 * (uint32_t)n)) n++; n++; }
    if (!a[5]) return (uint32_t)n;
    if ((int32_t)a[5] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (int32_t i = 0; i < n; i++) wr16(a[4] + 2 * (uint32_t)i, map_case(rd16(a[2] + 2 * (uint32_t)i), a[1]));
    return (uint32_t)n;
}
HLE(LCMapStringA) {
    int32_t n = (int32_t)a[3];
    if (n < 0) n = (int32_t)gstr_len(a[2]) + 1;
    if (!a[5]) return (uint32_t)n;
    if ((int32_t)a[5] < n) return set_err(ERROR_INSUFFICIENT_BUFFER_);
    for (int32_t i = 0; i < n; i++) {
        int c = u16_to_cp1252(map_case(cp1252_to_u16(rd8(a[2] + (uint32_t)i)), a[1]));
        wr8(a[4] + (uint32_t)i, (uint8_t)(c < 0 ? '?' : c));
    }
    return (uint32_t)n;
}

static int cmp_fold(uint16_t x, uint16_t y, uint32_t flags) {
    if (flags & 1 /* NORM_IGNORECASE */) { x = map_case(x, 0x100); y = map_case(y, 0x100); }
    return (int)x - (int)y;
}
HLE(CompareStringA) {   /* (lcid, flags, s1, n1, s2, n2) -> 1 <, 2 =, 3 > */
    int32_t n1 = (int32_t)a[3], n2 = (int32_t)a[5];
    if (n1 < 0) n1 = (int32_t)gstr_len(a[2]);
    if (n2 < 0) n2 = (int32_t)gstr_len(a[4]);
    for (int32_t i = 0; i < n1 && i < n2; i++) {
        int d = cmp_fold(cp1252_to_u16(rd8(a[2] + (uint32_t)i)), cp1252_to_u16(rd8(a[4] + (uint32_t)i)), a[1]);
        if (d) return d < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}
HLE(CompareStringW) {
    int32_t n1 = (int32_t)a[3], n2 = (int32_t)a[5];
    if (n1 < 0) { n1 = 0; while (rd16(a[2] + 2 * (uint32_t)n1)) n1++; }
    if (n2 < 0) { n2 = 0; while (rd16(a[4] + 2 * (uint32_t)n2)) n2++; }
    for (int32_t i = 0; i < n1 && i < n2; i++) {
        int d = cmp_fold(rd16(a[2] + 2 * (uint32_t)i), rd16(a[4] + 2 * (uint32_t)i), a[1]);
        if (d) return d < 0 ? 1 : 3;
    }
    return n1 < n2 ? 1 : n1 > n2 ? 3 : 2;
}

/* ---- time ------------------------------------------------------------------------------------------ */
uint64_t rt_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t g_boot_ns;
uint32_t rt_ms(void) {
    if (!g_boot_ns) g_boot_ns = rt_now_ns();
    return (uint32_t)((rt_now_ns() - g_boot_ns) / 1000000ull);
}
HLE(GetTickCount) { (void)a; return rt_ms(); }
HLE(QueryPerformanceFrequency) { wr32(a[0], 1000000u); wr32(a[0] + 4, 0); return TRUE_; }
HLE(QueryPerformanceCounter) {
    uint64_t us = rt_now_ns() / 1000ull;
    wr32(a[0], (uint32_t)us); wr32(a[0] + 4, (uint32_t)(us >> 32));
    return TRUE_;
}

/* FILETIME: 100 ns since 1601-01-01 */
static uint64_t unix_to_filetime(uint64_t sec, uint64_t ns) { return (sec + 11644473600ull) * 10000000ull + ns / 100; }
HLE(GetSystemTimeAsFileTime) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ft = unix_to_filetime((uint64_t)tv.tv_sec, (uint64_t)tv.tv_usec * 1000);
    wr32(a[0], (uint32_t)ft); wr32(a[0] + 4, (uint32_t)(ft >> 32));
    return 0;
}
HLE(FileTimeToLocalFileTime) { wr32(a[1], rd32(a[0])); wr32(a[1] + 4, rd32(a[0] + 4)); return TRUE_; }   /* UTC */
HLE(FileTimeToSystemTime) {   /* SYSTEMTIME: 8 WORDs */
    uint64_t ft = rd32(a[0]) | ((uint64_t)rd32(a[0] + 4) << 32);
    time_t t = (time_t)(ft / 10000000ull - 11644473600ull);
    struct tm tm;
    gmtime_r(&t, &tm);
    wr16(a[1] + 0, (uint16_t)(tm.tm_year + 1900)); wr16(a[1] + 2, (uint16_t)(tm.tm_mon + 1));
    wr16(a[1] + 4, (uint16_t)tm.tm_wday); wr16(a[1] + 6, (uint16_t)tm.tm_mday);
    wr16(a[1] + 8, (uint16_t)tm.tm_hour); wr16(a[1] + 10, (uint16_t)tm.tm_min);
    wr16(a[1] + 12, (uint16_t)tm.tm_sec); wr16(a[1] + 14, (uint16_t)((ft / 10000) % 1000));
    return TRUE_;
}
HLE(GetTimeZoneInformation) { memset(G2H(a[0]), 0, 172); return 0; /* TIME_ZONE_ID_UNKNOWN: UTC */ }
