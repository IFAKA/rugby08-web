/* sdl_audio.c -- plat_audio_start on SDL2: one 44.1 kHz stereo float device whose callback pulls
 * from the runtime's mixer (runtime/win32/dsound.c). Without a device (headless runs) a thread
 * paces the mixer in real time instead, so DirectSound play cursors still advance.
 * --audio-capture PATH: every mixed block is also appended to a 16-bit WAV (header kept current). */
#include "plat.h"
#include "rt.h"
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { BLOCK = 512 };                 /* frames per callback: ~11.6 ms, close to DirectSound's cursor step */
static plat_audio_mix_fn g_mix;
static FILE *g_wav;
static uint32_t g_wav_bytes;

static void wav_header(void) {
    uint8_t h[44];
    uint32_t rate = PLAT_AUDIO_RATE, byterate = rate * 4;
    memcpy(h, "RIFF", 4);
    uint32_t riff = 36 + g_wav_bytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmtlen = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    memcpy(h + 16, &fmtlen, 4);
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byterate, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &g_wav_bytes, 4);
    fseek(g_wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, g_wav);
    fseek(g_wav, 0, SEEK_END);
}

static void pull(float *out, int frames) {
    g_mix(out, frames);
    if (!g_wav) return;
    int16_t s[BLOCK * 2];
    for (int done = 0; done < frames;) {
        int n = frames - done < BLOCK ? frames - done : BLOCK;
        for (int i = 0; i < 2 * n; i++) {
            float v = out[2 * done + i] * 32767.0f;
            s[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
        }
        fwrite(s, 4, (size_t)n, g_wav);
        g_wav_bytes += 4u * (uint32_t)n;
        done += n;
    }
    wav_header();
    fflush(g_wav);
}

static void callback(void *user, Uint8 *stream, int len) {
    (void)user;
    pull((float *)stream, len / 8);
}

static void *pacer(void *arg) {
    (void)arg;
    float buf[BLOCK * 2];
    uint64_t due = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
    for (;;) {
        pull(buf, BLOCK);
        due += hz * BLOCK / PLAT_AUDIO_RATE;
        uint64_t now = SDL_GetPerformanceCounter();
        if (due > now) {
            uint64_t ns = (due - now) * 1000000000ull / hz;
            struct timespec t = { (time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull) };
            nanosleep(&t, NULL);
        }
    }
    return NULL;
}

#ifdef __EMSCRIPTEN__
/* Browser output: the mixer runs on the pacer thread (never on the busy main thread) and writes
 * into a ring in shared memory; an AudioWorklet drains it. ctl: [0] frames written, [1] frames
 * read, [2] 1 once the AudioContext is running (browsers keep it suspended until a user gesture;
 * until then the pacer runs on the wall clock, as the headless pacer does; also while a running
 * context drains nothing). */
#include <emscripten.h>
enum { RING_FRAMES = 16384, RING_TARGET = 3072 };
static float g_ring[RING_FRAMES * 2];
static int32_t g_ctl[4];

static void web_audio_init(void) {
    MAIN_THREAD_ASYNC_EM_ASM(Module.rtAudioInit($0, $1, $2, $3), g_ring, RING_FRAMES, g_ctl, PLAT_AUDIO_RATE);
}

static void web_pacer(void) {
    float buf[BLOCK * 2];
    web_audio_init();
    uint64_t due = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
    int32_t last_rd = -1;
    uint64_t rd_moved = due;
    for (;;) {
        int running = __atomic_load_n(&g_ctl[2], __ATOMIC_ACQUIRE);
        int32_t wr = __atomic_load_n(&g_ctl[0], __ATOMIC_ACQUIRE), rd = __atomic_load_n(&g_ctl[1], __ATOMIC_ACQUIRE);
        uint64_t t_now = SDL_GetPerformanceCounter();
        if (rd != last_rd) last_rd = rd, rd_moved = t_now;
        /* a context that says it runs but drains nothing (muted headless pages, a busy machine, an audio
           device going away) must not stop the game's sound clock: DirectSound's play cursor would
           freeze and the game would restore its device over and over. Then the wall clock paces, and the
           pacer drains the ring itself until the worklet reads again. */
        if (running && t_now - rd_moved > hz / 4) running = 0;
        if (running && wr - rd >= RING_TARGET) {      /* the device clock paces the mixer */
            struct timespec t = { 0, 2000000 };
            nanosleep(&t, NULL);
            due = SDL_GetPerformanceCounter();
            continue;
        }
        pull(buf, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            int32_t j = (int32_t)((uint32_t)(wr + i) % RING_FRAMES) * 2;
            g_ring[j] = buf[2 * i];
            g_ring[j + 1] = buf[2 * i + 1];
        }
        __atomic_store_n(&g_ctl[0], wr + BLOCK, __ATOMIC_RELEASE);
        if (running) continue;
        if (__atomic_load_n(&g_ctl[2], __ATOMIC_ACQUIRE) && wr + BLOCK - rd > RING_TARGET)   /* stalled sink */
            __atomic_store_n(&g_ctl[1], wr + BLOCK - RING_TARGET, __ATOMIC_RELEASE);
        due += hz * BLOCK / PLAT_AUDIO_RATE;
        uint64_t now = SDL_GetPerformanceCounter();
        if (due > now) {
            uint64_t ns = (due - now) * 1000000000ull / hz;
            struct timespec t = { (time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull) };
            nanosleep(&t, NULL);
        } else if (now - due > hz / 4) due = now;
    }
}
static void *web_pacer_thread(void *arg) {
    (void)arg;
    web_pacer();
    return NULL;
}
#endif

int plat_audio_start(plat_audio_mix_fn mix) {
    static int started, result;
    if (started) return result;
    started = 1;
    g_mix = mix;
    if (rt_audio_capture_path[0]) {
        g_wav = fopen(rt_audio_capture_path, "wb");
        if (g_wav) wav_header();
        else rt_log("audio: cannot write %s", rt_audio_capture_path);
    }
    SDL_AudioSpec want = { 0 }, have;
    want.freq = PLAT_AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = BLOCK;
    want.callback = callback;
    SDL_AudioDeviceID dev = 0;
#ifndef __EMSCRIPTEN__ /* the browser keeps audio suspended until a user gesture and runs SDL's callback on the busy main thread: clock the mixer by the pacer instead */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#endif
    if (dev) {
        SDL_PauseAudioDevice(dev, 0);
        rt_log("audio: SDL device '%s', %d Hz, %d-frame blocks", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
        return result = 0;
    }
#ifdef __EMSCRIPTEN__
    rt_log("audio: AudioWorklet fed by a mixer thread");
    pthread_t th;
    pthread_create(&th, NULL, web_pacer_thread, NULL);
    return result = 0;
#else
    rt_log("audio: no output device (%s); pacing the mixer without one", SDL_GetError());
    pthread_t th;
    pthread_create(&th, NULL, pacer, NULL);
#endif
    return result = 1;
}
