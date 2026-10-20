/* main.c -- boot: arena, image, imports, main thread, then the game's own CRT entry point. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char rt_game_dir[1024] = "Game Files";
char rt_exe_path[1024] = "Rugby08_unpacked.exe";
extern char rt_user_dir[1024];
int rt_frame_limit = -1;          /* --frames N: exit after N presented frames (checks); <= 0: no limit */
char rt_screenshot_path[1024];    /* --screenshot PATH: PNG of the last frame before --frames exits */
char rt_audio_capture_path[1024]; /* --audio-capture PATH: WAV of the mixed audio output */
int rt_log_input;
char rt_shot_path[1024];          /* set by the input replay's `shot` events (d3d8 Present writes it) */
char rt_replay_path[1024];        /* --replay PATH: scripted key input (tests/replays/) */
int rt_replay_live; /* --replay-live PATH: same script, but visible window and real input (play.sh --menu) */

void rt_log_open(const char *path);
void rt_mem_init(void);
uint32_t rt_load_image(const char *path, uint32_t *entry_out);
void rt_bind_imports(void);
void rt_platform_init(int argc, char **argv); /* platform/<target>/: fault handler, host setup */

static void usage(void) {
    fprintf(stderr, "rugby08 [--exe PATH] [--game-dir DIR] [--user-dir DIR] [--log FILE] [--frames N] [--screenshot "
                    "PNG] [--audio-capture WAV] [--replay FILE] [-v]\n");
    exit(2);
}

int main(int argc, char **argv) {
    const char *log_path = "logs/native_run.log";
    if (getenv("RT_USER_DIR"))
        snprintf(rt_user_dir, sizeof rt_user_dir, "%s", getenv("RT_USER_DIR")); /* tests; --user-dir wins */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--exe") && i + 1 < argc) snprintf(rt_exe_path, sizeof rt_exe_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--game-dir") && i + 1 < argc)
            snprintf(rt_game_dir, sizeof rt_game_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--user-dir") && i + 1 < argc)
            snprintf(rt_user_dir, sizeof rt_user_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) rt_frame_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            snprintf(rt_screenshot_path, sizeof rt_screenshot_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--audio-capture") && i + 1 < argc)
            snprintf(rt_audio_capture_path, sizeof rt_audio_capture_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc)
            snprintf(rt_replay_path, sizeof rt_replay_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--replay-live") && i + 1 < argc) {
            snprintf(rt_replay_path, sizeof rt_replay_path, "%s", argv[++i]);
            rt_replay_live = 1;
        } else if (!strcmp(argv[i], "--log-input")) rt_log_input = 1;
        else if (!strcmp(argv[i], "-v")) rt_verbose = 1;
        else if (!strncmp(argv[i], "--", 2)) { /* platform options are parsed by rt_platform_init */ } else
            usage();
    }
    rt_log_open(log_path);
    rt_platform_init(argc, argv);
    rt_mem_init();
    uint32_t entry;
    rt_load_image(rt_exe_path, &entry);
    rt_bind_imports();
    rt_thread_enter(RT_STACK0_LO, RT_STACK0_HI, RT_TIB0_VA);
    if (det_on()) det_init_main();
    rt_trace("boot: image loaded, entry 0x%08X, %u lifted functions", entry, recomp_dispatch_count);
    uint32_t rc = guest_call(entry, 0);
    rt_trace("exit: entry returned %u", rc);
    return (int)rc;
}
