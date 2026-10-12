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
#include "dsound_int.h"

pthread_mutex_t g_ds_mu = PTHREAD_MUTEX_INITIALIZER;
dsbuf_t *g_bufs;
dsbuf_t *g_primary;
uint32_t g_coop;

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
