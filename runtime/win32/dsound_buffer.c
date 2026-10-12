/* dsound_buffer.c -- IDirectSoundBuffer: guest-memory buffers, play/write cursors (virtual in
 * deterministic mode), Lock/Play/Stop and format, volume, pan, frequency. */
#include "dsound_int.h"

/* ---- buffers --------------------------------------------------------------------------------- */
static dsbuf_t *buf(const uint32_t *a) { return rt_com_host(a[0]); }

/* WAVEFORMATEX: wFormatTag nChannels nSamplesPerSec nAvgBytesPerSec nBlockAlign wBitsPerSample cbSize */
int set_format(dsbuf_t *b, uint32_t wfx) {
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
    pthread_mutex_lock(&g_ds_mu);
    for (dsbuf_t **p = &g_bufs; *p; p = &(*p)->next)
        if (*p == b) {
            *p = b->next;
            break;
        }
    if (g_primary == b) g_primary = NULL;
    pthread_mutex_unlock(&g_ds_mu);
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
    pthread_mutex_lock(&g_ds_mu);
    cursors(b, &play, &write);
    pthread_mutex_unlock(&g_ds_mu);
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
    pthread_mutex_lock(&g_ds_mu);
    if (det_on()) vrebase(b);
    b->looping = (a[3] & DSBPLAY_LOOPING) != 0 || b == g_primary;
    b->playing = 1;
    if (det_on()) {
        b->vplaying = 1;
        b->vt0 = det_now_ns();
    }
    pthread_mutex_unlock(&g_ds_mu);
    return DS_OK;
}

static uint32_t sb_SetCurrentPosition(const uint32_t *a) {
    dsbuf_t *b = buf(a);
    pthread_mutex_lock(&g_ds_mu);
    if (b->block && a[1] < b->size) b->pos = a[1] / b->block;
    if (det_on() && b->block && a[1] < b->size) {
        b->vpos0 = a[1] / b->block;
        b->vt0 = det_now_ns();
    }
    pthread_mutex_unlock(&g_ds_mu);
    return DS_OK;
}

static uint32_t sb_SetFormat(const uint32_t *a) {   /* primary only: the device's output format */
    dsbuf_t *b = buf(a);
    if (b != g_primary) return DSERR_INVALIDPARAM;
    pthread_mutex_lock(&g_ds_mu);
    int ok = set_format(b, a[1]);
    pthread_mutex_unlock(&g_ds_mu);
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

uint32_t new_buffer(dsbuf_t *b) {
    static uint32_t vt;
    if (!vt) vt = rt_com_vtable("IDirectSoundBuffer", sb_methods, (int)(sizeof sb_methods / sizeof sb_methods[0]));
    b->refs = 1;
    pthread_mutex_lock(&g_ds_mu);
    b->next = g_bufs;
    g_bufs = b;
    pthread_mutex_unlock(&g_ds_mu);
    return rt_com_new(vt, b);
}
