/* dsound.c -- DirectSound (DirectSoundCreate, ordinal 1) as a software mixer on the host's audio
 * output (plat_audio_start). Semantics follow DirectSound as dsoal and Wine's dsound implement it
 * (docs/PRIOR_ART.md):
 *   - buffer memory is guest memory, so Lock hands out guest pointers and Unlock has nothing to do;
 *   - a playing buffer's play cursor advances in real time at its frequency, wrapping when looping
 *     and stopping at the end otherwise; the write cursor runs a fixed lead ahead of it;
 *   - the primary buffer is mixed only at DSSCL_WRITEPRIMARY, where the game writes it directly.
 * Rugby 08 mixes everything itself ("Main CPU software audio rendering") and streams the result
 * through one secondary buffer, so no 3D/notify interfaces are offered (QueryInterface logs them).
 * DirectSoundCaptureCreate (ordinal 6) reports no capture device. */
#include "win32.h"
#include "plat.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define DS_OK              0u
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_NOINTERFACE  0x80004002u
#define DSERR_NODRIVER     0x88780078u
#define DSERR_ALLOCATED    0x8878000Au
#define DSERR_OUTOFMEMORY  0x8007000Eu

#define DSBCAPS_PRIMARYBUFFER   0x1u
#define DSBPLAY_LOOPING         0x1u
#define DSBSTATUS_PLAYING       0x1u
#define DSBSTATUS_LOOPING       0x4u
#define DSBLOCK_FROMWRITECURSOR 0x1u
#define DSBLOCK_ENTIREBUFFER    0x2u
#define DSSCL_WRITEPRIMARY      4u
#define WRITE_LEAD_MS           15u

typedef struct dsbuf {
    uint32_t refs, flags;
    uint32_t data, size;              /* guest memory */
    uint32_t rate, channels, bits, block;
    uint32_t freq;                    /* SetFrequency (0 = the format's rate) */
    int32_t volume, pan;              /* hundredths of a dB */
    int playing, looping;
    double pos;                       /* play cursor in frames */
    int vplaying;                     /* deterministic mode: what the guest sees, from virtual time */
    uint64_t vt0, vpos0;
    struct dsbuf *next;
} dsbuf_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static dsbuf_t *g_bufs;
static dsbuf_t *g_primary;
static uint32_t g_coop;

/* ---- mixer (host audio thread) --------------------------------------------------------------- */
static float sample(const dsbuf_t *b, uint32_t frame, uint32_t ch) {
    uint32_t va = b->data + frame * b->block + ch * (b->bits / 8);
    return b->bits == 8 ? ((float)rd8(va) - 128.0f) / 128.0f : (float)(int16_t)rd16(va) / 32768.0f;
}

static void mix_buffer(dsbuf_t *b, float *out, int frames) {
    uint32_t nframes = b->size / b->block;
    if (!nframes) return;
    double step = (double)(b->freq ? b->freq : b->rate) / PLAT_AUDIO_RATE;
    float gain = b->volume <= -10000 ? 0.0f : powf(10.0f, (float)b->volume / 2000.0f);
    float gl = gain * (b->pan > 0 ? powf(10.0f, (float)-b->pan / 2000.0f) : 1.0f);
    float gr = gain * (b->pan < 0 ? powf(10.0f, (float)b->pan / 2000.0f) : 1.0f);
    for (int i = 0; i < frames; i++) {
        uint32_t f = (uint32_t)b->pos;
        float l, r;
        switch (b->channels) {
        case 1: l = r = sample(b, f, 0); break;
        case 2:
            l = sample(b, f, 0);
            r = sample(b, f, 1);
            break;
        default: {                     /* quad / 5.1 (FL FR C LFE RL RR or FL FR RL RR): fold to stereo */
            l = sample(b, f, 0);
            r = sample(b, f, 1);
            if (b->channels == 4) {
                l = 0.5f * (l + sample(b, f, 2));
                r = 0.5f * (r + sample(b, f, 3));
            } else if (b->channels >= 6) {
                float c = 0.707f * sample(b, f, 2);
                l = 0.5f * (l + c + sample(b, f, 4));
                r = 0.5f * (r + c + sample(b, f, 5));
            }
        }
        }
        out[2 * i] += l * gl;
        out[2 * i + 1] += r * gr;
        b->pos += step;
        if (b->pos >= nframes) {
            if (b->looping) b->pos -= nframes * (uint32_t)(b->pos / nframes);
            else {
                b->pos = 0;
                b->playing = 0;
                return;
            }
        }
    }
}

static void mix(float *out, int frames) {
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    pthread_mutex_lock(&g_mu);
    for (dsbuf_t *b = g_bufs; b; b = b->next)
        if (b->playing && (b != g_primary || g_coop == DSSCL_WRITEPRIMARY)) mix_buffer(b, out, frames);
    pthread_mutex_unlock(&g_mu);
    for (int i = 0; i < 2 * frames; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
}

/* ---- buffers --------------------------------------------------------------------------------- */
static dsbuf_t *buf(const uint32_t *a) { return rt_com_host(a[0]); }

/* WAVEFORMATEX: wFormatTag nChannels nSamplesPerSec nAvgBytesPerSec nBlockAlign wBitsPerSample cbSize */
static int set_format(dsbuf_t *b, uint32_t wfx) {
    uint32_t ch = rd16(wfx + 2), rate = rd32(wfx + 4), bits = rd16(wfx + 14);
    if (!ch || ch > 8 || (bits != 8 && bits != 16) || rate < 100 || rate > 200000) return 0;
    b->channels = ch;
    b->rate = rate;
    b->bits = bits;
    b->block = ch * bits / 8;
    return 1;
}

static uint32_t sb_QueryInterface(const uint32_t *a) {
    rt_log("IDirectSoundBuffer::QueryInterface %08X-%04X: not offered", rd32(a[1]), rd16(a[1] + 4));
    if (a[2]) wr32(a[2], 0);
    return DSERR_NOINTERFACE;
}
static uint32_t sb_AddRef(const uint32_t *a) { return ++buf(a)->refs; }
static uint32_t sb_Release(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    if (b->refs > 1) return --b->refs;
    pthread_mutex_lock(&g_mu);
    for (dsbuf_t **p = &g_bufs; *p; p = &(*p)->next)
        if (*p == b) {
            *p = b->next;
            break;
        }
    if (g_primary == b) g_primary = NULL;
    pthread_mutex_unlock(&g_mu);
    gfree(b->data);
    free(b);
    return 0;
}

static uint32_t
sb_GetCaps(const uint32_t *a) {   /* DSBCAPS: dwSize dwFlags dwBufferBytes dwUnlockTransferRate dwPlayCpuOverhead */
    dsbuf_t *b = buf(a);
    wr32(a[1] + 4, b->flags);
    wr32(a[1] + 8, b->size);
    wr32(a[1] + 12, 0);
    wr32(a[1] + 16, 0);
    return DS_OK;
}

/* Deterministic mode: the play cursor the guest sees is vpos0 + elapsed virtual time * frequency
 * (never the mixer's real-time progress); a one-shot buffer stops at its end. */
static uint32_t nframes_of(const dsbuf_t *b) { return b->block ? b->size / b->block : 0; }
static uint64_t vpos(dsbuf_t *b) {
    uint64_t p = b->vpos0, now = det_clock_read();   /* polling the cursor is polling the clock */
    if (b->vplaying) p += (now - b->vt0) * (uint64_t)(b->freq ? b->freq : b->rate) / 1000000000ull;
    uint32_t n = nframes_of(b);
    if (!n) return 0;
    if (p >= n) {
        if (b->looping) p %= n;
        else {
            b->vplaying = 0;
            b->vpos0 = 0;
            p = 0;
        }
    }
    return p;
}
static void vrebase(dsbuf_t *b) {
    b->vpos0 = vpos(b);
    b->vt0 = det_now_ns();
}

static void cursors(dsbuf_t *b, uint32_t *play, uint32_t *write) {
    *play = (uint32_t)b->pos * b->block;
    if (det_on()) {
        *play = (uint32_t)vpos(b) * b->block;
        *write = b->size
                     ? (*play + (b->vplaying ? ((b->freq ? b->freq : b->rate) * WRITE_LEAD_MS / 1000) * b->block : 0)) %
                           b->size
                     : 0;
        return;
    }
    uint32_t lead = b->playing ? ((b->freq ? b->freq : b->rate) * WRITE_LEAD_MS / 1000) * b->block : 0;
    *write = b->size ? (*play + lead) % b->size : 0;
}

static uint32_t sb_GetCurrentPosition(const uint32_t *a) {   /* (this, pdwPlay, pdwWrite) */
    dsbuf_t *b = buf(a);
    uint32_t play, write;
    pthread_mutex_lock(&g_mu);
    cursors(b, &play, &write);
    pthread_mutex_unlock(&g_mu);
    if (a[1]) wr32(a[1], play);
    if (a[2]) wr32(a[2], write);
    return DS_OK;
}

static uint32_t sb_GetFormat(const uint32_t *a) {   /* (this, pwfx, cbSize, pcbWritten) */
    dsbuf_t *b = buf(a);
    if (a[1] && a[2] >= 16) {
        wr16(a[1], 1);
        wr16(a[1] + 2, (uint16_t)b->channels);
        wr32(a[1] + 4, b->rate);
        wr32(a[1] + 8, b->rate * b->block);
        wr16(a[1] + 12, (uint16_t)b->block);
        wr16(a[1] + 14, (uint16_t)b->bits);
        if (a[2] >= 18) wr16(a[1] + 16, 0);
    }
    if (a[3]) wr32(a[3], 18);
    return DS_OK;
}

static uint32_t sb_GetVolume(const uint32_t *a) {
    wr32(a[1], (uint32_t)buf(a)->volume);
    return DS_OK;
}
static uint32_t sb_GetPan(const uint32_t *a) {
    wr32(a[1], (uint32_t)buf(a)->pan);
    return DS_OK;
}
static uint32_t sb_GetFrequency(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    wr32(a[1], b->freq ? b->freq : b->rate);
    return DS_OK;
}
static uint32_t sb_GetStatus(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    int playing = b->playing;
    if (det_on()) {
        vpos(b);
        playing = b->vplaying;
    }
    wr32(a[1], (playing ? DSBSTATUS_PLAYING : 0) | (playing && b->looping ? DSBSTATUS_LOOPING : 0));
    return DS_OK;
}
static uint32_t sb_Initialize(const uint32_t *a) {
    (void)a;
    return DSERR_ALLOCATED;
}

static uint32_t sb_Lock(const uint32_t *a) {   /* (this, offset, bytes, ppv1, pcb1, ppv2, pcb2, flags) */
    dsbuf_t *b = buf(a);
    uint32_t off = a[1], n = a[2], play, write;
    if (a[7] & DSBLOCK_FROMWRITECURSOR) {
        cursors(b, &play, &write);
        off = write;
    }
    if (a[7] & DSBLOCK_ENTIREBUFFER) n = b->size;
    if (!b->size || off >= b->size || n > b->size) return DSERR_INVALIDPARAM;
    uint32_t n1 = off + n <= b->size ? n : b->size - off;
    wr32(a[3], b->data + off);
    wr32(a[4], n1);
    if (a[5]) wr32(a[5], n1 < n ? b->data : 0);
    if (a[6]) wr32(a[6], n - n1);
    return DS_OK;
}

static uint32_t sb_Play(const uint32_t *a) {   /* (this, reserved, priority, flags) */
    dsbuf_t *b = buf(a);
    pthread_mutex_lock(&g_mu);
    if (det_on()) vrebase(b);
    b->looping = (a[3] & DSBPLAY_LOOPING) != 0 || b == g_primary;
    b->playing = 1;
    if (det_on()) {
        b->vplaying = 1;
        b->vt0 = det_now_ns();
    }
    pthread_mutex_unlock(&g_mu);
    return DS_OK;
}

static uint32_t sb_SetCurrentPosition(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    pthread_mutex_lock(&g_mu);
    if (b->block && a[1] < b->size) b->pos = a[1] / b->block;
    if (det_on() && b->block && a[1] < b->size) {
        b->vpos0 = a[1] / b->block;
        b->vt0 = det_now_ns();
    }
    pthread_mutex_unlock(&g_mu);
    return DS_OK;
}

static uint32_t sb_SetFormat(const uint32_t *a) {   /* primary only: the device's output format */
    dsbuf_t *b = buf(a);
    if (b != g_primary) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&g_mu);
    int ok = set_format(b, a[1]);
    pthread_mutex_unlock(&g_mu);
    rt_log("DirectSound primary SetFormat: %u Hz, %u ch, %u bit%s", rd32(a[1] + 4), rd16(a[1] + 2), rd16(a[1] + 14),
           ok ? "" : " (rejected)");
    return ok ? DS_OK : DSERR_INVALIDPARAM;
}

static uint32_t sb_SetVolume(const uint32_t *a) {
    buf(a)->volume = (int32_t)a[1];
    return DS_OK;
}
static uint32_t sb_SetPan(const uint32_t *a) {
    buf(a)->pan = (int32_t)a[1];
    return DS_OK;
}
static uint32_t sb_SetFrequency(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    if (det_on()) vrebase(b);
    b->freq = a[1];
    return DS_OK;
}
static uint32_t sb_Stop(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    if (det_on()) {
        vrebase(b);
        b->vplaying = 0;
    }
    b->playing = 0;
    return DS_OK;
}
static uint32_t sb_Unlock(const uint32_t *a) {
    (void)a;
    return DS_OK;
}
static uint32_t sb_Restore(const uint32_t *a) {
    (void)a;
    return DS_OK;
}

static const rt_method_t sb_methods[] = {
    { "IDirectSoundBuffer::QueryInterface", sb_QueryInterface, 12 },
    { "AddRef", sb_AddRef, 4 },
    { "Release", sb_Release, 4 },
    { "GetCaps", sb_GetCaps, 8 },
    { "GetCurrentPosition", sb_GetCurrentPosition, 12 },
    { "GetFormat", sb_GetFormat, 16 },
    { "GetVolume", sb_GetVolume, 8 },
    { "GetPan", sb_GetPan, 8 },
    { "GetFrequency", sb_GetFrequency, 8 },
    { "GetStatus", sb_GetStatus, 8 },
    { "Initialize", sb_Initialize, 12 },
    { "Lock", sb_Lock, 32 },
    { "Play", sb_Play, 16 },
    { "SetCurrentPosition", sb_SetCurrentPosition, 8 },
    { "SetFormat", sb_SetFormat, 8 },
    { "SetVolume", sb_SetVolume, 8 },
    { "SetPan", sb_SetPan, 8 },
    { "SetFrequency", sb_SetFrequency, 8 },
    { "Stop", sb_Stop, 4 },
    { "Unlock", sb_Unlock, 20 },
    { "Restore", sb_Restore, 4 },
};

static uint32_t new_buffer(dsbuf_t *b) {
    static uint32_t vt;
    if (!vt) vt = rt_com_vtable("IDirectSoundBuffer", sb_methods, (int)(sizeof sb_methods / sizeof sb_methods[0]));
    b->refs = 1;
    pthread_mutex_lock(&g_mu);
    b->next = g_bufs;
    g_bufs = b;
    pthread_mutex_unlock(&g_mu);
    return rt_com_new(vt, b);
}

/* ---- IDirectSound ------------------------------------------------------------------------------ */
static uint32_t ds_QueryInterface(const uint32_t *a) {
    wr32(a[2], a[0]);
    return DS_OK;
}
static uint32_t ds_AddRef(const uint32_t *a) {
    (void)a;
    return 1;
}
static uint32_t ds_Release(const uint32_t *a) {
    (void)a;
    return 0;
}

/* DSBUFFERDESC: dwSize dwFlags dwBufferBytes dwReserved lpwfxFormat [guid3DAlgorithm] */
static uint32_t ds_CreateSoundBuffer(const uint32_t *a) {   /* (this, pcDSBufferDesc, ppDSBuffer, pUnkOuter) */
    uint32_t d = a[1], flags = rd32(d + 4), bytes = rd32(d + 8), wfx = rd32(d + 16);
    dsbuf_t *b = calloc(1, sizeof *b);
    b->flags = flags;
    if (flags & DSBCAPS_PRIMARYBUFFER) {
        if (g_primary) {
            wr32(a[2], 0);
            free(b);
            return DSERR_ALLOCATED;
        }
        b->rate = 22050;
        b->channels = 2;
        b->bits = 8;
        b->block = 2;     /* DirectSound's default primary format */
        bytes = 44100 * 4 / 5;                                            /* 200 ms at up to 44.1 kHz 16-bit stereo */
        g_primary = b;
    } else if (!wfx || !set_format(b, wfx) || bytes < 4 || bytes > 64u << 20) {
        free(b);
        wr32(a[2], 0);
        return DSERR_INVALIDPARAM;
    }
    b->size = bytes - bytes % b->block;
    b->data = gcalloc(b->size);
    if (!b->data) {
        free(b);
        wr32(a[2], 0);
        return DSERR_OUTOFMEMORY;
    }
    if (b->bits == 8) memset(G2H(b->data), 0x80, b->size);
    wr32(a[2], new_buffer(b));
    rt_log("DirectSound CreateSoundBuffer: %s flags 0x%X, %u bytes, %u Hz %u ch %u bit",
           b == g_primary ? "primary" : "secondary", flags, b->size, b->rate, b->channels, b->bits);
    return DS_OK;
}

static uint32_t ds_GetCaps(const uint32_t *a) {   /* DSCAPS (96 bytes) */
    uint32_t p = a[1];
    memset(G2H(p + 4), 0, 92);
    wr32(p + 4, 0x0F | 0x400 | 0xF00);   /* PRIMARY mono/stereo/8/16, CONTINUOUSRATE, SECONDARY mono/stereo/8/16 */
    wr32(p + 8, 100);
    wr32(p + 12, 200000);   /* min/max secondary sample rate */
    wr32(p + 16, 1);                          /* primary buffers */
    return DS_OK;
}

static uint32_t ds_DuplicateSoundBuffer(const uint32_t *a) {
    if (a[2]) wr32(a[2], 0);
    return DSERR_INVALIDPARAM;
}
static uint32_t ds_SetCooperativeLevel(const uint32_t *a) {
    g_coop = a[2];
    return DS_OK;
}
static uint32_t ds_ok(const uint32_t *a) {
    (void)a;
    return DS_OK;
}
static uint32_t ds_GetSpeakerConfig(const uint32_t *a) {
    wr32(a[1], 4);
    return DS_OK;
}   /* DSSPEAKER_STEREO */

static const rt_method_t ds_methods[] = {
    { "IDirectSound::QueryInterface", ds_QueryInterface, 12 },
    { "AddRef", ds_AddRef, 4 },
    { "Release", ds_Release, 4 },
    { "CreateSoundBuffer", ds_CreateSoundBuffer, 16 },
    { "GetCaps", ds_GetCaps, 8 },
    { "DuplicateSoundBuffer", ds_DuplicateSoundBuffer, 12 },
    { "SetCooperativeLevel", ds_SetCooperativeLevel, 12 },
    { "Compact", ds_ok, 4 },
    { "GetSpeakerConfig", ds_GetSpeakerConfig, 8 },
    { "SetSpeakerConfig", ds_ok, 8 },
    { "Initialize", ds_ok, 8 },
};

HLE(DirectSoundCreate) {   /* (lpGuid, ppDS, pUnkOuter) */
    static uint32_t ds;
    if (!ds)
        ds = rt_com_new(rt_com_vtable("IDirectSound", ds_methods, (int)(sizeof ds_methods / sizeof ds_methods[0])),
                        NULL);
    plat_audio_start(mix);
    rt_log("DirectSoundCreate -> software mixer");
    wr32(a[1], ds);
    return DS_OK;
}

HLE(DirectSoundCaptureCreate) {   /* (lpGuid, ppDSC, pUnkOuter): no microphone */
    if (a[1]) wr32(a[1], 0);
    return DSERR_NODRIVER;
}
