/*
 * gfopenaiff and gfopenwave.datatype: AIFF, AIFC and WAV in plain C
 * (pcmcore.h). MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>

#include "pcmcore.h"

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}
static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 | (unsigned long)p[1] << 8 | p[0];
}
static unsigned le16(const unsigned char *p) { return (unsigned)p[1] << 8 | p[0]; }

static void put_be32(unsigned char *p, unsigned long v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put_be16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static void put_le32(unsigned char *p, unsigned long v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_le16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

/* COMM's 80-bit extended rate, as a whole number of Hz */
static unsigned long ext80_rate(const unsigned char *p)
{
    int e = (int)(be16(p) & 0x7fff) - 16383;
    unsigned long hi = be32(p + 2);
    if (be16(p) & 0x8000 || e < 0 || e > 31) return 0;
    return hi >> (31 - e);
}

static void put_ext80(unsigned char *p, unsigned long rate)
{
    int msb = 31;
    memset(p, 0, 10);
    if (!rate) return;
    while (!(rate & (1UL << msb))) msb--;
    put_be16(p, 16383 + msb);
    put_be32(p + 2, (rate << (31 - msb)) & 0xffffffffUL);
}

static int finish(pcm_info *pi)
{
    if (pi->channels < 1 || pi->channels > 8 || !pi->rate || !pi->enc || !pi->data)
        return -1;
    if (pi->bits != 8 && pi->bits != 16 && pi->bits != 24 && pi->bits != 32)
        return -1;
    if ((pi->enc == PCM_F32_BE || pi->enc == PCM_F32_LE) && pi->bits != 32)
        return -1;
    pi->frame_bytes = pi->channels * (pi->bits / 8);
    return 0;
}

static int parse_aiff(pcm_read_fn rd, void *h, unsigned long size, pcm_info *pi, int aifc)
{
    unsigned char c[26];
    unsigned long off = 12, end = size, ssnd_len = 0;
    int have_comm = 0;
    pi->container = PCM_AIFF;
    while (off + 8 <= end) {
        unsigned long len;
        if (rd(h, off, c, 8) != 8) break;
        len = be32(c + 4);
        if (!memcmp(c, "COMM", 4) && len >= 18) {
            unsigned long n = len < 26 ? len : 22;
            if (rd(h, off + 8, c, n) != n) return -1;
            pi->channels = (int)be16(c);
            pi->frames = be32(c + 2);
            pi->bits = (int)((be16(c + 6) + 7) & ~7u);
            pi->rate = ext80_rate(c + 8);
            pi->enc = PCM_S_BE;
            if (aifc && n >= 22) {
                if (!memcmp(c + 18, "NONE", 4) || !memcmp(c + 18, "twos", 4)) pi->enc = PCM_S_BE;
                else if (!memcmp(c + 18, "sowt", 4)) pi->enc = PCM_S_LE;
                else if (!memcmp(c + 18, "fl32", 4) || !memcmp(c + 18, "FL32", 4)) { pi->enc = PCM_F32_BE; pi->bits = 32; }
                else return -1;                 /* a compressed AIFC */
            }
            if (pi->enc == PCM_S_LE && pi->bits == 8) pi->enc = PCM_S_BE;
            have_comm = 1;
        } else if (!memcmp(c, "SSND", 4) && len >= 8) {
            if (rd(h, off + 8, c, 8) != 8) return -1;
            pi->data = off + 16 + be32(c);
            ssnd_len = len - 8 - be32(c);
        }
        if (!size && have_comm && pi->data) break;
        off += 8 + len + (len & 1);
        if (!size) end = off + 8;               /* length not known: walk on */
    }
    if (!have_comm || finish(pi)) return -1;
    if (ssnd_len && pi->frames > ssnd_len / pi->frame_bytes)
        pi->frames = ssnd_len / pi->frame_bytes; /* the SSND holds less */
    return 0;
}

static int parse_wave(pcm_read_fn rd, void *h, unsigned long size, pcm_info *pi)
{
    unsigned char c[40];
    unsigned long off = 12, end = size, data_len = 0;
    int have_fmt = 0;
    unsigned tag = 0;
    pi->container = PCM_WAVE;
    while (off + 8 <= end) {
        unsigned long len;
        if (rd(h, off, c, 8) != 8) break;
        len = le32(c + 4);
        if (!memcmp(c, "fmt ", 4) && len >= 16) {
            unsigned long n = len < 40 ? len : 40;
            if (rd(h, off + 8, c, n) != n) return -1;
            tag = le16(c);
            pi->channels = (int)le16(c + 2);
            pi->rate = le32(c + 4);
            pi->bits = (int)((le16(c + 14) + 7) & ~7u);
            if (tag == 0xfffe && n >= 26) tag = le16(c + 24);   /* extensible: the subformat */
            if (tag == 1) pi->enc = pi->bits == 8 ? PCM_U8 : PCM_S_LE;
            else if (tag == 3) pi->enc = PCM_F32_LE;
            else return -1;                     /* compressed */
            have_fmt = 1;
        } else if (!memcmp(c, "data", 4)) {
            pi->data = off + 8;
            data_len = len;
            if (size && data_len > size - pi->data) data_len = size - pi->data;
            if (have_fmt) break;
        }
        off += 8 + len + (len & 1);
        if (!size) end = off + 8;
    }
    if (!have_fmt || finish(pi)) return -1;
    pi->frames = data_len / pi->frame_bytes;
    return 0;
}

int pcm_parse(pcm_read_fn rd, void *h, unsigned long size, pcm_info *pi)
{
    unsigned char m[12];
    memset(pi, 0, sizeof *pi);
    if (rd(h, 0, m, 12) != 12) return -1;
    if (size && size < 12) return -1;
    if (!memcmp(m, "FORM", 4) && !memcmp(m + 8, "AIFF", 4)) {
        if (!size) size = 8 + be32(m + 4);
        return parse_aiff(rd, h, size, pi, 0);
    }
    if (!memcmp(m, "FORM", 4) && !memcmp(m + 8, "AIFC", 4)) {
        if (!size) size = 8 + be32(m + 4);
        return parse_aiff(rd, h, size, pi, 1);
    }
    if (!memcmp(m, "RIFF", 4) && !memcmp(m + 8, "WAVE", 4)) {
        if (!size) size = 8 + le32(m + 4);
        return parse_wave(rd, h, size, pi);
    }
    return -1;
}

/* An IEEE single (its bits) to 16 bits, rounded and clamped, without
 * floating point: x * 32768 is the mantissa shifted by exponent - 8. */
static short f32to16(unsigned long v)
{
    int e = (int)((v >> 23) & 0xff) - 127, neg = (v >> 31) & 1, sh;
    unsigned long m = (v & 0x7fffffUL) | 0x800000UL, r;
    if (e >= 0) return neg ? -32768 : 32767;    /* |x| >= 1 (and Inf, NaN) */
    sh = 8 - e;
    r = sh >= 32 ? 0 : (m + (1UL << (sh - 1))) >> sh;
    if (neg) return r >= 32768 ? -32768 : (short)-(long)r;
    return r >= 32767 ? 32767 : (short)r;
}

void pcm_to16(const pcm_info *pi, const unsigned char *s, short *d, unsigned long n)
{
    unsigned long i, k = n * pi->channels;
    int b = pi->bits / 8;
    for (i = 0; i < k; i++, s += b) {
        switch (pi->enc) {
        case PCM_U8: d[i] = (short)(((int)s[0] - 128) * 256); break;
        case PCM_S_BE: d[i] = (short)(b == 1 ? (signed char)s[0] * 256 : (signed char)s[0] * 256 + s[1]); break;
        case PCM_S_LE: d[i] = (short)((signed char)s[b - 1] * 256 + s[b - 2]); break;
        case PCM_F32_BE:
        case PCM_F32_LE: d[i] = f32to16(pi->enc == PCM_F32_BE ? be32(s) : le32(s)); break;
        }
    }
}

int pcm_pad(int channels, unsigned long frames, int bits)
{
    return (int)((frames * channels * (bits / 8)) & 1);
}

int pcm_header(int container, int channels, unsigned long rate, unsigned long frames, int bits,
               unsigned char *h)
{
    unsigned long data = frames * channels * (bits / 8);
    unsigned long pad = data & 1;
    memset(h, 0, PCM_HEADER_MAX);
    if (container == PCM_WAVE) {
        memcpy(h, "RIFF", 4); put_le32(h + 4, 36 + data + pad);
        memcpy(h + 8, "WAVEfmt ", 8); put_le32(h + 16, 16);
        put_le16(h + 20, 1); put_le16(h + 22, channels);
        put_le32(h + 24, rate); put_le32(h + 28, rate * channels * (bits / 8));
        put_le16(h + 32, channels * (bits / 8)); put_le16(h + 34, bits);
        memcpy(h + 36, "data", 4); put_le32(h + 40, data);
        return 44;
    }
    memcpy(h, "FORM", 4); put_be32(h + 4, 4 + 26 + 16 + data + pad);
    memcpy(h + 8, "AIFFCOMM", 8); put_be32(h + 16, 18);
    put_be16(h + 20, channels); put_be32(h + 22, frames);
    put_be16(h + 26, bits); put_ext80(h + 28, rate);
    memcpy(h + 38, "SSND", 4); put_be32(h + 42, 8 + data);
    return 54;                                  /* offset and block size: 0 */
}

unsigned long pcm_encode(int container, int channels, int bits, const short *s, unsigned long n,
                         unsigned char *o)
{
    unsigned long i, k = n * channels;
    for (i = 0; i < k; i++) {
        unsigned v = (unsigned short)s[i];
        if (bits == 8) *o++ = container == PCM_WAVE ? (unsigned char)((v >> 8) ^ 0x80) : (unsigned char)(v >> 8);
        else if (container == PCM_WAVE) { *o++ = v; *o++ = v >> 8; }
        else { *o++ = v >> 8; *o++ = v; }
    }
    return k * (bits / 8);
}
