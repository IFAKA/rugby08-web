/* k32_dir.c -- FindFirstFile/FindNextFile, directories, delete and move (the user overlay only),
 * current directory, drives (one fixed disk C: holding the install). Files: k32_file.c. */
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

/* ---- find ---------------------------------------------------------------------------------------- */
typedef struct {
    char names[512][260];
    int is_dir[512];
    uint64_t size[512];
    int n, i;
} find_t;

static void add_dir(find_t *fs, const char *hostdir, const char *pat) {
    DIR *d = opendir(hostdir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && fs->n < 512) {
        if (fnmatch(pat, e->d_name, FNM_CASEFOLD)) continue;
        int dup = 0;
        for (int k = 0; k < fs->n; k++)
            if (!strcasecmp(fs->names[k], e->d_name)) dup = 1;
        if (dup) continue;
        char p[2300];
        snprintf(p, sizeof p, "%s/%s", hostdir, e->d_name);
        struct stat st;
        stat(p, &st);
        snprintf(fs->names[fs->n], 260, "%s", e->d_name);
        fs->is_dir[fs->n] = S_ISDIR(st.st_mode);
        fs->size[fs->n] = (uint64_t)st.st_size;
        fs->n++;
    }
    closedir(d);
}

static void fill_find_data(uint32_t out, find_t *fs) {   /* WIN32_FIND_DATAA, 320 bytes */
    int i = fs->i++;
    memset(G2H(out), 0, 320);
    wr32(out, fs->is_dir[i] ? 0x10 : 0x80);
    wr32(out + 28, (uint32_t)(fs->size[i] >> 32));
    wr32(out + 32, (uint32_t)fs->size[i]);
    gstr_put(out + 44, fs->names[i], 260);
}

HLE(FindFirstFileA) {
    char full[1024];
    path_full_guest(gstr(a[0]), full, sizeof full);
    char *slash = strrchr(full, '\\');
    char pat[260], dir[1024];
    snprintf(pat, sizeof pat, "%s", slash + 1);
    *slash = 0;
    snprintf(dir, sizeof dir, "%s%s", full, strlen(full) == 2 ? "\\" : "");
    find_t *fs = calloc(1, sizeof *fs);
    char host[2048];
    path_to_host(dir, host, sizeof host, 0);         /* overlay if present, else game dir */
    add_dir(fs, host, pat);
    char over[2048];
    path_to_host(dir, over, sizeof over, 1);
    if (strcmp(over, host) != 0) add_dir(fs, over, pat);
    if (!fs->n) {
        free(fs);
        set_err(ERROR_FILE_NOT_FOUND_);
        return INVALID_HANDLE;
    }
    uint32_t h = h_new(H_FIND);
    h_get(h, H_FIND)->p = fs;
    fill_find_data(a[1], fs);
    return h;
}
HLE(FindNextFileA) {
    hobj_t *o = h_get(a[0], H_FIND);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    find_t *fs = o->p;
    if (fs->i >= fs->n) return set_err(ERROR_NO_MORE_FILES_);
    fill_find_data(a[1], fs);
    return TRUE_;
}
HLE(FindClose) {
    hobj_t *o = h_get(a[0], H_FIND);
    if (o) {
        free(o->p);
        h_close(a[0]);
    }
    return TRUE_;
}

/* ---- directories, delete, move: overlay only ---------------------------------------------------- */
static int only_in_game_files(const char *g) {
    char r[2048], w[2048];
    path_to_host(g, r, sizeof r, 0);
    path_to_host(g, w, sizeof w, 1);
    return strcmp(r, w) != 0;   /* read path is not the overlay: it lives in "Game Files" */
}
HLE(DeleteFileA) {
    const char *g = gstr(a[0]);
    if (only_in_game_files(g)) {
        rt_log("DeleteFileA(\"%s\") refused: game content is read-only", g);
        return set_err(ERROR_ACCESS_DENIED_);
    }
    char w[2048];
    path_to_host(g, w, sizeof w, 1);
    return unlink(w) ? set_err(ERROR_FILE_NOT_FOUND_) : TRUE_;
}
HLE(MoveFileA) {
    const char *from = gstr(a[0]), *to = gstr(a[1]);
    if (only_in_game_files(from)) {
        rt_log("MoveFileA(\"%s\") refused: game content is read-only", from);
        return set_err(ERROR_ACCESS_DENIED_);
    }
    char f[2048], t[2048];
    path_to_host(from, f, sizeof f, 1);
    path_to_host(to, t, sizeof t, 1);
    return rename(f, t) ? set_err(ERROR_FILE_NOT_FOUND_) : TRUE_;
}
HLE(CreateDirectoryA) {
    char w[2048];
    path_to_host(gstr(a[0]), w, sizeof w, 1);
    if (!mkdir(w, 0755)) return TRUE_;
    return set_err(errno == EEXIST ? ERROR_ALREADY_EXISTS_ : ERROR_PATH_NOT_FOUND_);
}
HLE(RemoveDirectoryA) {
    char w[2048];
    path_to_host(gstr(a[0]), w, sizeof w, 1);
    return rmdir(w) ? set_err(ERROR_ACCESS_DENIED_) : TRUE_;
}

HLE(GetFullPathNameA) {   /* (name, buflen, buf, &filepart) */
    char full[1024];
    path_full_guest(gstr(a[0]), full, sizeof full);
    uint32_t n = (uint32_t)strlen(full);
    if (a[1] <= n) return n + 1;
    gstr_put(a[2], full, a[1]);
    if (a[3]) {
        char *s = strrchr(full, '\\');
        wr32(a[3], a[2] + (uint32_t)(s ? s + 1 - full : 0));
    }
    return n;
}
HLE(GetCurrentDirectoryA) {
    char c[1024];
    path_guest_cwd(c, sizeof c);
    uint32_t n = (uint32_t)strlen(c);
    if (a[0] <= n) return n + 1;
    gstr_put(a[1], c, a[0]);
    return n;
}
HLE(SetCurrentDirectoryA) {
    if (path_set_guest_cwd(gstr(a[0]))) {
        rt_log("SetCurrentDirectoryA(\"%s\") failed", gstr(a[0]));
        return set_err(ERROR_PATH_NOT_FOUND_);
    }
    return TRUE_;
}

/* ---- drives: one fixed disk C: holding the install ------------------------------------------------ */
HLE(GetDriveTypeA) {
    const char *r = gstr(a[0]);
    if (!r || toupper((unsigned char)r[0]) == 'C') return 3;   /* DRIVE_FIXED */
    return 1;                                                  /* DRIVE_NO_ROOT_DIR */
}
HLE(GetLogicalDrives) {
    (void)a;
    return 1u << 2;
}
HLE(GetLogicalDriveStringsA) {
    if (a[0] < 5) return 5;
    uint32_t p = a[1];
    wr8(p, 'C');
    wr8(p + 1, ':');
    wr8(p + 2, '\\');
    wr8(p + 3, 0);
    wr8(p + 4, 0);
    return 4;
}
HLE(GetVolumeInformationA) {   /* (root, vol, volsz, &serial, &maxcomp, &flags, fs, fssz) */
    if (a[1]) gstr_put(a[1], "RUGBY08", a[2]);
    if (a[3]) wr32(a[3], 0x20070831u);
    if (a[4]) wr32(a[4], 255);
    if (a[5]) wr32(a[5], 0x3);
    if (a[6]) gstr_put(a[6], "NTFS", a[7]);
    return TRUE_;
}
HLE(GetDiskFreeSpaceA) {   /* (root, &spc, &bps, &free, &total) */
    if (a[1]) wr32(a[1], 8);
    if (a[2]) wr32(a[2], 512);
    if (a[3]) wr32(a[3], 0x00400000u);
    if (a[4]) wr32(a[4], 0x00800000u);
    return TRUE_;
}
