/* probes.c -- RT_PROBE, RT_PROBE_DUMP and RT_DUMP: trace arguments and object fields at function entries,
 * or guest memory at a presented frame, without a relift. */
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RT_PROBE="VA[:off,off..];VA..." (hex) with RT_PROBE_FRAMES="from-to": on every entry of VA in that
 * window, trace the frame, ecx, three stack arguments and the dwords at ecx+off (hex and as float).
 * Needs no relift: the entry hook of every lifted function calls here. */
static struct {
    uint32_t va;
    int n;
    uint32_t off[8];
} g_probe[8];
static int g_nprobe = -1;
static uint32_t g_probe_from, g_probe_to = ~0u;
static void probe_parse(void) {
    g_nprobe = 0;
    const char *e = getenv("RT_PROBE");
    if (getenv("RT_PROBE_FRAMES")) sscanf(getenv("RT_PROBE_FRAMES"), "%u-%u", &g_probe_from, &g_probe_to);
    while (e && *e && g_nprobe < 8) {
        char *end;
        uint32_t va = (uint32_t)strtoul(e, &end, 16);
        int k = g_nprobe++;
        g_probe[k].va = va;
        g_probe[k].n = 0;
        if (*end == ':') {
            e = end + 1;
            while (g_probe[k].n < 8) {
                g_probe[k].off[g_probe[k].n++] = (uint32_t)strtoul(e, &end, 16);
                e = end;
                if (*e != ',') break;
                e++;
            }
        } else e = end;
        if (*e == ';') e++;
        else break;
    }
}
/* RT_PROBE_DUMP="VA:arg:len" with RT_PROBE_FRAMES: the first entry of VA in a presented frame dumps len
 * bytes at the pointer in stack arg 0..2 (or ecx for arg 'e'): objects whose address varies per run. */
static void probe_hexdump(const char *tag, uint32_t f, uint32_t a, uint32_t len) {
    rt_trace("%s f%u at %08X", tag, f, a);
    for (uint32_t o = 0; o < len; o += 16) {
        uint32_t w[4];
        float fl[4];
        for (int i = 0; i < 4; i++) {
            w[i] = rd32(a + o + 4u * (uint32_t)i);
            memcpy(&fl[i], &w[i], 4);
        }
        rt_trace("%s +%03X: %08X %08X %08X %08X | %g %g %g %g", tag, o, w[0], w[1], w[2], w[3], fl[0], fl[1], fl[2],
                 fl[3]);
    }
}
/* RT_PROBE_DUMP="VA:arg:len[:len2:off,off,...]": also dump len2 bytes at each pointer found at obj+off. */
static void probe_dump(uint32_t va, uint32_t f) {
    static uint32_t dva, len, len2, offs[8], noff, last_f = ~0u;
    static int arg = -2;
    if (arg == -2) {
        const char *e = getenv("RT_PROBE_DUMP");
        arg = -1;
        char c = 0;
        int n = 0;
        if (e && sscanf(e, "%x:%c:%x%n", &dva, &c, &len, &n) == 3) {
            arg = c == 'e' ? 3 : c - '0';
            if (e[n] == ':') {
                char *end;
                len2 = (uint32_t)strtoul(e + n + 1, &end, 16);
                while (*end == ':' || *end == ',') { offs[noff++ % 8] = (uint32_t)strtoul(end + 1, &end, 16); }
            }
        }
    }
    if (arg < 0 || va != dva || f == last_f || f < g_probe_from || f > g_probe_to || f % 5) return;
    last_f = f;
    uint32_t a = arg == 3 ? g_ecx : rd32(g_esp + 4 + 4u * (uint32_t)arg);
    probe_hexdump("dumpobj", f, a, len);
    for (uint32_t i = 0; i < noff && i < 8; i++) {
        uint32_t q = rd32(a + offs[i]);
        if (q > 0x10000 && len2) probe_hexdump("dumpent", f, q, len2);
    }
}
void rt_probe_enter(uint32_t va) {
    if (g_nprobe < 0) probe_parse();
    uint32_t f = rt_present_frame;
    probe_dump(va, f);
    if (f < g_probe_from || f > g_probe_to) return;
    for (int i = 0; i < g_nprobe; i++)
        if (g_probe[i].va == va) {
            char buf[400];
            int o = snprintf(buf, sizeof buf, "probe %08X f%u ecx=%08X args %08X %08X %08X", va, f, g_ecx,
                             rd32(g_esp + 4), rd32(g_esp + 8), rd32(g_esp + 12));
            for (int k = 0; k < g_probe[i].n && o < (int)sizeof buf - 40; k++) {
                uint32_t v = rd32(g_ecx + g_probe[i].off[k]);
                float fl;
                memcpy(&fl, &v, 4);
                o += snprintf(buf + o, sizeof buf - (size_t)o, " [%X]=%08X(%g)", g_probe[i].off[k], v, fl);
            }
            rt_trace("%s", buf);
        }
}

/* RT_DUMP="hexaddr:hexlen@frame[,...]": at that presented frame write the guest memory range to the
 * trace as one line per 16 bytes (hex dwords and the first float), for chasing object pointers. */
void rt_dump_at(uint32_t frame) {
    const char *e = getenv("RT_DUMP");
    while (e && *e) {
        char *end;
        uint32_t a = (uint32_t)strtoul(e, &end, 16), len = 0, fr = 0;
        if (*end == ':') len = (uint32_t)strtoul(end + 1, &end, 16);
        if (*end == '@') fr = (uint32_t)strtoul(end + 1, &end, 10);
        if (fr == frame && len)
            for (uint32_t o = 0; o < len; o += 16) {
                uint32_t w[4];
                float f[4];
                for (int i = 0; i < 4; i++) {
                    w[i] = rd32(a + o + 4u * (uint32_t)i);
                    memcpy(&f[i], &w[i], 4);
                }
                rt_trace("dump f%u %08X: %08X %08X %08X %08X | %g %g %g %g", frame, a + o, w[0], w[1], w[2], w[3], f[0],
                         f[1], f[2], f[3]);
            }
        e = end;
        if (*e == ',') e++;
        else break;
    }
}
