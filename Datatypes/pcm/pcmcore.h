/*
 * gfopenaiff and gfopenwave.datatype: AIFF, AIFC and WAV, read and written,
 * in plain C with no Amiga headers (tests/test-pcm.sh runs it on x86 or
 * ARM64 cores). Written from Apple's AIFF 1.3 and AIFF-C specifications and
 * Microsoft's RIFF WAVE (with WAVE_FORMAT_EXTENSIBLE).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef PCMCORE_H
#define PCMCORE_H

enum { PCM_AIFF = 1, PCM_WAVE = 2 };
/* how the file keeps a sample */
enum { PCM_S_BE = 1, PCM_S_LE, PCM_U8, PCM_F32_BE, PCM_F32_LE };

typedef struct {
    int container;              /* PCM_AIFF or PCM_WAVE */
    int channels;               /* 1 to 8 */
    unsigned long rate;
    unsigned long frames;
    int bits;                   /* 8, 16, 24 or 32 */
    int enc;                    /* PCM_S_BE ... */
    unsigned long data;         /* where the samples start in the file */
    int frame_bytes;            /* bytes per frame in the file */
} pcm_info;

/* Reads n bytes at offset off; returns how many it read. */
typedef unsigned long (*pcm_read_fn)(void *h, unsigned long off, void *buf, unsigned long n);

/* Fills pi from the file's header; 0, or -1 when it isn't an AIFF, AIFC or
 * WAV this reads (compressed AIFC and WAV, other than sowt and fl32 and
 * WAV's float, are not read). size: the file's length, 0 if not known. */
int pcm_parse(pcm_read_fn rd, void *h, unsigned long size, pcm_info *pi);

/* n frames from the file's bytes to 16-bit signed samples, interleaved,
 * in the host's order (24 and 32 bits: their top 16; float: clamped). */
void pcm_to16(const pcm_info *pi, const unsigned char *src, short *dst, unsigned long n);

/* The header written before n frames: its length (at most PCM_HEADER_MAX).
 * bits 8 or 16. */
#define PCM_HEADER_MAX 64
int pcm_header(int container, int channels, unsigned long rate, unsigned long frames, int bits,
               unsigned char *out);

/* n frames of 16-bit samples as the file keeps them: AIFF big-endian
 * (8-bit signed), WAV little-endian (8-bit unsigned); bits 8 takes the
 * high byte (the caller dithers). Returns the bytes written. */
unsigned long pcm_encode(int container, int channels, int bits, const short *src, unsigned long n,
                         unsigned char *out);

/* A pad byte after the samples (IFF and RIFF chunks are even): 1 or 0. */
int pcm_pad(int channels, unsigned long frames, int bits);

#endif
