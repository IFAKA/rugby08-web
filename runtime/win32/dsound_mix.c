/* dsound_mix.c -- the mixer: every playing buffer (and the primary at DSSCL_WRITEPRIMARY) mixed into
 * the host output, on the host's audio thread. */
#include "dsound_int.h"

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

/* Deterministic mode: the speakers follow the cursor the guest sees (virtual time, vpos). A buffer more
 * than 50 ms ahead of it waits in silence (virtual time stalls while netplay waits for a player); one
 * more than 50 ms behind jumps forward. Read-only: the host audio thread never changes guest state. */
static int det_follow(dsbuf_t *b) {
    if (!b->vplaying) return 0;
    uint32_t n = b->block ? b->size / b->block : 0, rate = b->freq ? b->freq : b->rate;
    if (!n) return 0;
    uint64_t p = b->vpos0 + (det_now_ns() - b->vt0) * (uint64_t)rate / 1000000000ull;
    if (p >= n) {
        if (!b->looping) return 0;
        p %= n;
    }
    double d = b->pos - (double)p, lim = rate * 0.05;
    if (b->looping) {
        if (d > n / 2.0) d -= n;
        else if (d < -(n / 2.0)) d += n;
    }
    if (d > lim) return 0;
    if (d < -lim) b->pos = (double)p;
    return 1;
}

void mix(float *out, int frames) {
    memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    if (rt_ff) return; /* catching up (netplay): silent; det_follow re-aligns the cursors afterwards */
    pthread_mutex_lock(&g_ds_mu);
    for (dsbuf_t *b = g_bufs; b; b = b->next)
        if (b->playing && (b != g_primary || g_coop == DSSCL_WRITEPRIMARY) && (rt_det != 1 || det_follow(b)))
            mix_buffer(b, out, frames);
    pthread_mutex_unlock(&g_ds_mu);
    for (int i = 0; i < 2 * frames; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
}
