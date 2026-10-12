/* dsound_int.h -- what the DirectSound sources share: dsound.c (IDirectSound), dsound_buffer.c
 * (IDirectSoundBuffer) and dsound_mix.c (the mixer on the host audio thread). */
#ifndef DSOUND_INT_H
#define DSOUND_INT_H
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

extern pthread_mutex_t g_ds_mu;   /* the buffer list and every buffer's cursor */
extern dsbuf_t *g_bufs;
extern dsbuf_t *g_primary;
extern uint32_t g_coop;

void mix(float *out, int frames);          /* dsound_mix.c: plat_audio_mix_fn */
int set_format(dsbuf_t *b, uint32_t wfx);   /* dsound_buffer.c */
uint32_t new_buffer(dsbuf_t *b);

#endif
