/* paths.c -- guest (Windows) paths -> host paths.
 *
 * The guest sees the game installed at GUEST_GAME_ROOT and starts with that as its cwd.
 *   under GUEST_GAME_ROOT  -> read:  <userdata>/game/<rest> if it exists, else "Game Files"/<rest>
 *                             write: <userdata>/game/<rest>       ("Game Files" is never modified)
 *   any other absolute path -> <userdata>/<drive>/<rest>          (profiles, My Documents, temp)
 * Every component is matched case-insensitively against the host directory (cached). */
#include "win32.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char rt_user_dir[1024] = "userdata";
static char g_cwd[1024] = GUEST_GAME_ROOT;
static pthread_mutex_t g_pmu = PTHREAD_MUTEX_INITIALIZER;

void path_guest_cwd(char *out, size_t n) {
    pthread_mutex_lock(&g_pmu);
    snprintf(out, n, "%s", g_cwd);
    pthread_mutex_unlock(&g_pmu);
}

/* Absolute, backslashes, "." and ".." resolved, no trailing backslash (except "C:\"). */
void path_full_guest(const char *guest, char *out, size_t n) {
    char buf[2048];
    if (guest[0] && guest[1] == ':') snprintf(buf, sizeof buf, "%s", guest);
    else if (guest[0] == '\\' || guest[0] == '/') {
        char c[1024];
        path_guest_cwd(c, sizeof c);
        snprintf(buf, sizeof buf, "%c:%s", c[0], guest);
    } else {
        char c[1024];
        path_guest_cwd(c, sizeof c);
        snprintf(buf, sizeof buf, "%s\\%s", c, guest);
    }
    for (char *p = buf; *p; p++)
        if (*p == '/') *p = '\\';
    /* split and rebuild */
    char *parts[256];
    int np = 0;
    char drive[3] = { (char)toupper((unsigned char)buf[0]), ':', 0 };
    char *s = buf + 2, *tok;
    while ((tok = strsep(&s, "\\"))) {
        if (!*tok || !strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) {
            if (np) np--;
            continue;
        }
        if (np < 256) parts[np++] = tok;
    }
    size_t o = (size_t)snprintf(out, n, "%s", drive);
    if (!np && o + 1 < n) {
        out[o++] = '\\';
        out[o] = 0;
    }
    for (int i = 0; i < np && o < n; i++) o += (size_t)snprintf(out + o, n - o, "\\%s", parts[i]);
}

int path_set_guest_cwd(const char *guest) {
    char full[1024];
    path_full_guest(guest, full, sizeof full);
    char host[2048];
    if (path_to_host(full, host, sizeof host, 0)) return -1;
    struct stat st;
    if (stat(host, &st) || !S_ISDIR(st.st_mode)) return -1;
    pthread_mutex_lock(&g_pmu);
    snprintf(g_cwd, sizeof g_cwd, "%s", full);
    pthread_mutex_unlock(&g_pmu);
    return 0;
}

/* Case-insensitive resolution of `rel` (components separated by '\\') under host dir `base`.
 * Missing components are appended as given. */
static void resolve_ci(const char *base, const char *rel, char *out, size_t n) {
    snprintf(out, n, "%s", base);
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", rel);
    char *s = tmp, *tok;
    int missing = 0;
    while ((tok = strsep(&s, "\\"))) {
        if (!*tok) continue;
        size_t len = strlen(out);
        char cand[2048];
        snprintf(cand, sizeof cand, "%s/%s", out, tok);
        struct stat st;
        if (missing || !stat(cand, &st)) {
            snprintf(out + len, n - len, "/%s", tok);
            continue;
        }
        DIR *d = opendir(out);
        int found = 0;
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcasecmp(e->d_name, tok)) {
                    snprintf(out + len, n - len, "/%s", e->d_name);
                    found = 1;
                    break;
                }
            }
            closedir(d);
        }
        if (!found) {
            missing = 1;
            snprintf(out + len, n - len, "/%s", tok);
        }
    }
}

static void mkdirs_for(const char *file) {
    char d[2048];
    snprintf(d, sizeof d, "%s", file);
    for (char *p = d + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(d, 0755);
            *p = '/';
        }
    }
}

int path_to_host(const char *guest, char *host, size_t n, int for_write) {
    char full[1024];
    path_full_guest(guest, full, sizeof full);
    size_t rl = strlen(GUEST_GAME_ROOT);
    if (!strncasecmp(full, GUEST_GAME_ROOT, rl) && (full[rl] == 0 || full[rl] == '\\')) {
        const char *rest = full + rl;
        char over[1024];
        snprintf(over, sizeof over, "%s/game", rt_user_dir);
        char ov[2048];
        resolve_ci(over, rest, ov, sizeof ov);
        struct stat st;
        if (for_write) {
            mkdirs_for(ov);
            snprintf(host, n, "%s", ov);
            return 0;
        }
        if (!stat(ov, &st)) {
            snprintf(host, n, "%s", ov);
            return 0;
        }
        resolve_ci(rt_game_dir, rest, host, n);
        return 0;
    }
    char base[1024];
    snprintf(base, sizeof base, "%s/%c", rt_user_dir, full[0]);
    resolve_ci(base, full + 2, host, n);
    if (for_write) mkdirs_for(host);
    return 0;
}
