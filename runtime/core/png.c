/* png.c -- minimal PNG writer for check screenshots: RGBA8, stored (uncompressed) deflate blocks. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc(crc(0, (const uint8_t *)type, 4), data, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

int rt_png_write(const char *path, int w, int h, const uint8_t *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    size_t row = (size_t)w * 4 + 1, raw = row * (size_t)h;
    size_t blocks = (raw + 65534) / 65535;
    uint8_t *z = malloc(2 + raw + blocks * 5 + 4), *q = z;
    *q++ = 0x78;
    *q++ = 0x01;
    uint32_t s1 = 1, s2 = 0;
    uint8_t *flat = malloc(raw);
    for (int y = 0; y < h; y++) {
        flat[row * (size_t)y] = 0;
        memcpy(flat + row * (size_t)y + 1, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
    }
    for (size_t i = 0; i < raw; i++) {
        s1 = (s1 + flat[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    for (size_t off = 0; off < raw; off += 65535) {
        uint16_t n = (uint16_t)(raw - off < 65535 ? raw - off : 65535);
        *q++ = off + n >= raw ? 1 : 0;
        *q++ = (uint8_t)n;
        *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n;
        *q++ = (uint8_t)(~n >> 8);
        memcpy(q, flat + off, n);
        q += n;
    }
    be32(q, (s2 << 16) | s1);
    q += 4;
    chunk(f, "IDAT", z, (uint32_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(z);
    free(flat);
    return fclose(f);
}
