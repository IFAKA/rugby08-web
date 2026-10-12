/* k32_file.c -- files, file mappings, CloseHandle (find, directories, drives: k32_dir.c). Guest paths go through
 * paths.c; every write/delete/rename lands in the userdata overlay -- "Game Files" is never
 * modified (a write to a game file first copies it into the overlay). */
#include "win32.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    char host[2048];
    char guest[1024];
} file_t;

static void note_miss(const char *guest, const char *host) {
    const char *dot = strrchr(guest, '.');
    const char *base = strrchr(guest, '/') ? strrchr(guest, '/') + 1 : guest;
    /* <32 hex digits>.<ext> names are the game's override probes for files whose canonical copy
     * is inside data/data.gob (its index uses the same names); a miss just means "use the pack" */
    int hashed = dot && dot - base == 32 && strspn(base, "0123456789abcdefABCDEF") == 32;
    int archive =
        !hashed && dot &&
        (!strcasecmp(dot, ".big") || !strcasecmp(dot, ".gob") || !strcasecmp(dot, ".viv") || !strcasecmp(dot, ".vp6"));
    /* archives are required content (checked by native.fs); other misses are ordinary probes */
    rt_log("%s: %s (host %s)", archive ? "FILE_NOT_FOUND" : "file probe miss", guest, host);
}

static int copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in < 0 || out < 0) {
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        return -1;
    }
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        if (write(out, buf, (size_t)n) != n) break;
    close(in);
    close(out);
    return 0;
}

#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u

HLE(CreateFileA) {   /* (name, access, share, sa, disposition, flags, template) */
    const char *g = gstr(a[0]);
    uint32_t access = a[1], disp = a[4];
    if (!g) return set_err(ERROR_INVALID_PARAMETER_) | INVALID_HANDLE;
    char rhost[2048], whost[2048];
    path_to_host(g, rhost, sizeof rhost, 0);
    struct stat st;
    int exists = !stat(rhost, &st);
    if (exists && S_ISDIR(st.st_mode)) {
        set_err(ERROR_ACCESS_DENIED_);
        return INVALID_HANDLE;
    }
    int writing = (access & GENERIC_WRITE) || disp == 1 || disp == 2 || disp == 5;
    int flags = 0;
    const char *host = rhost;
    if (writing) {
        path_to_host(g, whost, sizeof whost, 1);
        if (exists && strcmp(rhost, whost) != 0 && disp != 2 && disp != 5) copy_file(rhost, whost);
        host = whost;
        flags = (access & GENERIC_READ) ? O_RDWR : O_WRONLY;
    } else flags = O_RDONLY;
    switch (disp) {
    case 1:
        if (exists) {
            set_err(ERROR_FILE_EXISTS_);
            return INVALID_HANDLE;
        }
        flags |= O_CREAT;
        break;
    case 2: flags |= O_CREAT | O_TRUNC; break;
    case 3:
        if (!exists) {
            note_miss(g, rhost);
            set_err(ERROR_FILE_NOT_FOUND_);
            return INVALID_HANDLE;
        }
        break;
    case 4: flags |= O_CREAT; break;
    case 5:
        if (!exists) {
            set_err(ERROR_FILE_NOT_FOUND_);
            return INVALID_HANDLE;
        }
        flags |= O_TRUNC;
        break;
    }
    int fd = open(host, flags, 0644);
    if (fd < 0) {
        note_miss(g, host);
        set_err(errno == ENOENT ? ERROR_PATH_NOT_FOUND_ : ERROR_ACCESS_DENIED_);
        return INVALID_HANDLE;
    }
    uint32_t h = h_new(H_FILE);
    file_t *f = calloc(1, sizeof *f);
    f->fd = fd;
    snprintf(f->host, sizeof f->host, "%s", host);
    snprintf(f->guest, sizeof f->guest, "%s", g);
    h_get(h, H_FILE)->p = f;
    rt_last_error = (exists && (disp == 2 || disp == 4)) ? ERROR_ALREADY_EXISTS_ : 0;
    rt_log("CreateFileA(\"%s\", %s) -> 0x%08X", g, writing ? "w" : "r", h);
    return h;
}

static file_t *fget(uint32_t h) {
    hobj_t *o = h_get(h, H_FILE);
    return o ? o->p : NULL;
}

HLE(ReadFile) {   /* (h, buf, n, &read, overlapped) */
    if (a[4]) rt_log("ReadFile: OVERLAPPED ignored (synchronous read)");
    hobj_t *o = h_get(a[0], H_FREE);
    if (o && o->type == H_STD) {
        if (a[3]) wr32(a[3], 0);
        return TRUE_;
    }
    file_t *f = fget(a[0]);
    if (!f) {
        if (a[3]) wr32(a[3], 0);
        return set_err(ERROR_INVALID_HANDLE_);
    }
    uint32_t got = 0;
    while (got < a[2]) {
        ssize_t n = read(f->fd, G2H(a[1] + got), a[2] - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    if (a[3]) wr32(a[3], got);
    if (rt_verbose) {   /* -v: every read with its position and first bytes (pack/loader debugging) */
        const uint8_t *d = G2H(a[1]);
        rt_log("ReadFile h=0x%X pos=0x%llX n=%u got=%u [%02X %02X %02X %02X %02X %02X %02X %02X]", a[0],
               (unsigned long long)lseek(f->fd, 0, SEEK_CUR) - got, a[2], got, got > 0 ? d[0] : 0, got > 1 ? d[1] : 0,
               got > 2 ? d[2] : 0, got > 3 ? d[3] : 0, got > 4 ? d[4] : 0, got > 5 ? d[5] : 0, got > 6 ? d[6] : 0,
               got > 7 ? d[7] : 0);
    }
    return TRUE_;
}

HLE(WriteFile) {
    hobj_t *o = h_get(a[0], H_FREE);
    if (o && o->type == H_STD) {
        rt_log("stdio(%d): %.*s", o->count, (int)a[2], (const char *)G2H(a[1]));
        if (a[3]) wr32(a[3], a[2]);
        return TRUE_;
    }
    file_t *f = fget(a[0]);
    if (!f) return set_err(ERROR_INVALID_HANDLE_);
    ssize_t n = write(f->fd, G2H(a[1]), a[2]);
    if (a[3]) wr32(a[3], n > 0 ? (uint32_t)n : 0);
    return n >= 0 ? TRUE_ : set_err(ERROR_ACCESS_DENIED_);
}

HLE(SetFilePointer) {   /* (h, lo, &hi, method) */
    file_t *f = fget(a[0]);
    if (!f) {
        set_err(ERROR_INVALID_HANDLE_);
        return 0xFFFFFFFFu;
    }
    int64_t off = (int32_t)a[1];
    if (a[2]) off = (int64_t)(((uint64_t)rd32(a[2]) << 32) | a[1]);
    off_t r = lseek(f->fd, (off_t)off, a[3] == 0 ? SEEK_SET : a[3] == 1 ? SEEK_CUR : SEEK_END);
    if (r < 0) {
        set_err(131 /* ERROR_NEGATIVE_SEEK */);
        return 0xFFFFFFFFu;
    }
    if (a[2]) wr32(a[2], (uint32_t)((uint64_t)r >> 32));
    rt_last_error = 0;
    return (uint32_t)r;
}

HLE(GetFileSize) {
    file_t *f = fget(a[0]);
    struct stat st;
    if (!f || fstat(f->fd, &st)) {
        set_err(ERROR_INVALID_HANDLE_);
        return 0xFFFFFFFFu;
    }
    if (a[1]) wr32(a[1], (uint32_t)((uint64_t)st.st_size >> 32));
    return (uint32_t)st.st_size;
}

HLE(SetEndOfFile) {
    file_t *f = fget(a[0]);
    if (!f) return set_err(ERROR_INVALID_HANDLE_);
    return ftruncate(f->fd, lseek(f->fd, 0, SEEK_CUR)) ? FALSE_ : TRUE_;
}
HLE(FlushFileBuffers) {
    (void)a;
    return TRUE_;
}

HLE(GetFileType) {
    hobj_t *o = h_get(a[0], H_FREE);
    if (!o) return 0;
    return o->type == H_STD ? 2 /* FILE_TYPE_CHAR */ : o->type == H_FILE ? 1 /* FILE_TYPE_DISK */ : 0;
}

/* ---- file mappings: a view is a guest-heap copy of the file range (read-only use) ---------------- */
typedef struct {
    uint32_t file;
    uint32_t size;
} mapping_t;
HLE(CreateFileMappingA) {   /* (hfile, sa, protect, maxhi, maxlo, name) */
    file_t *f = fget(a[0]);
    if (!f) return set_err(ERROR_INVALID_HANDLE_);
    uint32_t h = h_new(H_MAPPING);
    mapping_t *m = calloc(1, sizeof *m);
    m->file = a[0];
    struct stat st;
    fstat(f->fd, &st);
    m->size = a[4] ? a[4] : (uint32_t)st.st_size;
    h_get(h, H_MAPPING)->p = m;
    rt_log("CreateFileMappingA(file 0x%X, size 0x%X) -> 0x%X", a[0], m->size, h);
    return h;
}
HLE(MapViewOfFile) {   /* (hmap, access, offhi, offlo, size) */
    hobj_t *o = h_get(a[0], H_MAPPING);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    mapping_t *m = o->p;
    file_t *f = fget(m->file);
    uint32_t size = a[4] ? a[4] : m->size - a[3];
    uint32_t va = gcalloc(size);
    rt_log("MapViewOfFile(map 0x%X, off 0x%X, size 0x%X) -> 0x%08X", a[0], a[3], size, va);
    if (!va) return set_err(ERROR_NOT_ENOUGH_MEMORY_);
    pread(f->fd, G2H(va), size, (off_t)a[3]);
    return va;
}
HLE(UnmapViewOfFile) {
    gfree(a[0]);
    return TRUE_;
}

/* ---- CloseHandle: every handle type ---------------------------------------------------------------- */
HLE(CloseHandle) {
    hobj_t *o = h_get(a[0], H_FREE);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    if (o->type == H_STD || o->type == H_THREAD) return TRUE_;   /* threads keep their object */
    int left = h_close(a[0]);
    if (o->type == H_FILE && o->p && left == 0) {
        close(((file_t *)o->p)->fd);
        free(o->p);
        o->p = NULL;
    }   /* the last handle */
    return TRUE_;
}
