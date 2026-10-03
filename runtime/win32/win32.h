/* win32.h -- shared helpers for the Win32 HLE (kernel32/user32/gdi32/advapi32/winmm). */
#ifndef RT_WIN32_H
#define RT_WIN32_H
#include "rt.h"
#include <pthread.h>
#include <stdio.h>

#define TRUE_ 1u
#define FALSE_ 0u
#define INVALID_HANDLE 0xFFFFFFFFu

/* Win32 error codes we report */
#define ERROR_SUCCESS_ 0
#define ERROR_FILE_NOT_FOUND_ 2
#define ERROR_PATH_NOT_FOUND_ 3
#define ERROR_ACCESS_DENIED_ 5
#define ERROR_INVALID_HANDLE_ 6
#define ERROR_NOT_ENOUGH_MEMORY_ 8
#define ERROR_NO_MORE_FILES_ 18
#define ERROR_HANDLE_EOF_ 38
#define ERROR_FILE_EXISTS_ 80
#define ERROR_INVALID_PARAMETER_ 87
#define ERROR_INSUFFICIENT_BUFFER_ 122
#define ERROR_ALREADY_EXISTS_ 183
#define ERROR_NO_MORE_ITEMS_ 259
#define WAIT_TIMEOUT_ 258
#define ERROR_CALL_NOT_IMPLEMENTED_ 120

static inline uint32_t set_err(uint32_t e) { rt_last_error = e; return 0; }

/* ---- kernel object handles ------------------------------------------------------------- */
typedef enum { H_FREE = 0, H_FILE, H_FIND, H_EVENT, H_MUTEX, H_SEMAPHORE, H_THREAD, H_MAPPING,
               H_STD, H_PROCESS, H_HEAP, H_MODULE } htype_t;
typedef struct hobj hobj_t;
struct hobj {
    htype_t type;
    int refs;
    pthread_mutex_t mu;       /* waitable objects: state below is guarded by mu */
    pthread_cond_t cv;
    int signaled;             /* event/thread/mutex(free)/semaphore(count>0) */
    int manual_reset;         /* event */
    int count, max_count;     /* semaphore; mutex recursion count */
    uint64_t owner;           /* mutex owner (host thread id) */
    uint32_t exit_code;       /* thread */
    void *p;                  /* type-specific (FILE*, find state, thread info...) */
    char name[260];
};
uint32_t h_new(htype_t type);                 /* returns the handle value */
hobj_t  *h_get(uint32_t h, htype_t type);     /* NULL if not that type (H_FREE = any) */
void     h_close(uint32_t h);
uint64_t rt_host_tid(void);

/* user32.c: back hwnd with the host window (D3D CreateDevice binds its device window). */
int user32_bind_host(uint32_t hwnd);

/* Block until the object is signaled or timeout (ms, 0xFFFFFFFF = infinite). 0 = acquired,
 * WAIT_TIMEOUT_ otherwise. Consumes auto-reset/semaphore/mutex state. */
uint32_t h_wait(hobj_t *o, uint32_t timeout_ms);
void     h_signal(hobj_t *o);                 /* set signaled + broadcast */

/* ---- paths --------------------------------------------------------------------------------- */
#define GUEST_GAME_ROOT "C:\\Program Files\\EA SPORTS\\Rugby 08"
extern char rt_user_dir[1024];                 /* writable overlay (saves, options, logs) */
/* Map a guest path (any case, '\\' or '/', relative to the guest cwd) to a host path.
 * for_write: resolve to the writable overlay (creating parent dirs). Returns 0 on success. */
int  path_to_host(const char *guest, char *host, size_t n, int for_write);
void path_guest_cwd(char *out, size_t n);
int  path_set_guest_cwd(const char *guest);
void path_full_guest(const char *guest, char *out, size_t n);   /* absolute, normalized */

/* ---- strings --------------------------------------------------------------------------------- */
uint32_t gstr_put(uint32_t va, const char *s, uint32_t cap);   /* copies with NUL; returns len */
uint32_t gstr_len(uint32_t va);
uint16_t cp1252_to_u16(uint8_t c);
int      u16_to_cp1252(uint16_t w);                            /* -1 if unmappable */

/* ---- time ------------------------------------------------------------------------------------ */
uint64_t rt_now_ns(void);
uint32_t rt_ms(void);   /* ms since boot (GetTickCount / timeGetTime) */

#endif
