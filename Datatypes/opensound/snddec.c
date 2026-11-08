/*
 * gfopensound.datatype's decoders on this CPU (snddec.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdlib.h>
#include <string.h>

#include "snddec.h"

/* No stdio in a datatype: everything comes from memory. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

enum { K_OGG = 1, K_MP3, K_FLAC };

struct snddec {
    int kind, channels;
    stb_vorbis *ogg;
    char *ogg_mem;                 /* all of stb_vorbis's memory: no alloca */
    drmp3 *mp3;
    drflac *flac;
};

static int kind_of(const unsigned char *m, unsigned long n)
{
    if (n >= 4 && !memcmp(m, "OggS", 4)) return K_OGG;
    if (n >= 4 && !memcmp(m, "fLaC", 4)) return K_FLAC;
    if (n >= 3 && !memcmp(m, "ID3", 3)) return K_MP3;
    if (n >= 2 && m[0] == 0xff && (m[1] & 0xe0) == 0xe0 && (m[1] & 0x06)) return K_MP3;
    return 0;
}

const char *snddec_kind(const unsigned char *data, unsigned long size)
{
    static const char *const names[] = { NULL, "Ogg Vorbis", "MP3", "FLAC" };
    return names[kind_of(data, size)];
}

snddec *snddec_open(const unsigned char *data, unsigned long size,
                    int *channels, unsigned long *rate, unsigned long *frames)
{
    snddec *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->kind = kind_of(data, size);
    *frames = 0;
    switch (d->kind) {
    case K_OGG: {
        int err = 0, n;
        stb_vorbis_alloc al;
        stb_vorbis_info vi;
        for (n = 256 * 1024; n <= 4 * 1024 * 1024 && !d->ogg; n *= 2) {
            if (!(d->ogg_mem = malloc(n))) break;
            al.alloc_buffer = d->ogg_mem;
            al.alloc_buffer_length_in_bytes = n;
            d->ogg = stb_vorbis_open_memory(data, (int)size, &err, &al);
            if (!d->ogg) {
                free(d->ogg_mem);
                d->ogg_mem = NULL;
                if (err != VORBIS_outofmem) break;
            }
        }
        if (!d->ogg) break;
        vi = stb_vorbis_get_info(d->ogg);
        d->channels = vi.channels;
        *rate = vi.sample_rate;
        *frames = stb_vorbis_stream_length_in_samples(d->ogg);
        break;
    }
    case K_MP3:
        if (!(d->mp3 = calloc(1, sizeof *d->mp3))) break;   /* big: never on a stack */
        if (!drmp3_init_memory(d->mp3, data, size, NULL)) {
            free(d->mp3);
            d->mp3 = NULL;
            break;
        }
        d->channels = (int)d->mp3->channels;
        *rate = d->mp3->sampleRate;
        *frames = (unsigned long)drmp3_get_pcm_frame_count(d->mp3);   /* reads headers only */
        drmp3_seek_to_pcm_frame(d->mp3, 0);
        break;
    case K_FLAC:
        if (!(d->flac = drflac_open_memory(data, size, NULL))) break;
        d->channels = (int)d->flac->channels;
        *rate = d->flac->sampleRate;
        *frames = (unsigned long)d->flac->totalPCMFrameCount;
        break;
    }
    if (!d->ogg && !d->mp3 && !d->flac) {
        free(d);
        return NULL;
    }
    *channels = d->channels;
    return d;
}

unsigned long snddec_read(snddec *d, short *out, unsigned long n)
{
    switch (d->kind) {
    case K_OGG: {
        int got = stb_vorbis_get_samples_short_interleaved(d->ogg, d->channels, out, (int)(n * d->channels));
        return got > 0 ? (unsigned long)got : 0;
    }
    case K_MP3: return (unsigned long)drmp3_read_pcm_frames_s16(d->mp3, n, out);
    case K_FLAC: return (unsigned long)drflac_read_pcm_frames_s16(d->flac, n, out);
    }
    return 0;
}

int snddec_rewind(snddec *d)
{
    switch (d->kind) {
    case K_OGG: return stb_vorbis_seek_start(d->ogg) ? 0 : -1;
    case K_MP3: return drmp3_seek_to_pcm_frame(d->mp3, 0) ? 0 : -1;
    case K_FLAC: return drflac_seek_to_pcm_frame(d->flac, 0) ? 0 : -1;
    }
    return -1;
}

void snddec_close(snddec *d)
{
    if (!d) return;
    if (d->ogg) stb_vorbis_close(d->ogg);
    free(d->ogg_mem);
    if (d->mp3) { drmp3_uninit(d->mp3); free(d->mp3); }
    if (d->flac) drflac_close(d->flac);
    free(d);
}
