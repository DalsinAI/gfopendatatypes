/*
 * smacker.datatype's core: Smacker films (RAD Game Tools' SMK2 and SMK4),
 * decoded with no Amiga headers, so it is tested on the host
 * (tests/test-smacker.sh) against FFmpeg's decoder.
 *
 * A Smacker film is a 104-byte header, a table of frame sizes, a table of
 * frame types, four Huffman trees, then the frames. Every number is
 * little-endian and every bit stream is read from the low bit of each byte
 * up. A frame is an optional palette change, up to seven sound tracks'
 * packets and the picture: 8-bit pixels in 4x4 blocks, each block drawn
 * from the trees or left as the frame before had it, so frames are decoded
 * in order. Written from the format as the MultimediaWiki describes it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef SMK_CORE_H
#define SMK_CORE_H

#include <stdint.h>

#define SMK_HEAD 104
#define SMK_TRACKS 7

/* A track's AudioRate bits; the low 24 bits are the rate in Hz */
#define SMK_AUD_PACKED  0x80000000UL    /* Smacker's own DPCM through Huffman trees */
#define SMK_AUD_DATA    0x40000000UL    /* the track has sound */
#define SMK_AUD_16BITS  0x20000000UL
#define SMK_AUD_STEREO  0x10000000UL
#define SMK_AUD_BINK    0x0C000000UL    /* Bink audio (SMK4): not decoded here */

/* Header flags */
#define SMK_FLAG_RING      1            /* one more frame, for going round to frame 1 */
#define SMK_FLAG_INTERLACE 2            /* rows to be shown with a blank row after each */
#define SMK_FLAG_DOUBLE    4            /* rows to be shown twice */

/* The film, as the datatype reads it (a DOS handle, or memory) and the
 * tests do */
struct smk_io {
    void *ctx;
    /* n bytes at byte at into buf: how many were read, -1 on an error */
    long (*read)(void *ctx, uint32_t at, void *buf, uint32_t n);
    void *(*alloc)(void *ctx, uint32_t n);
    void (*free)(void *ctx, void *p);
};

struct smk_track {
    uint32_t rate;          /* Hz; 0: no track */
    uint8_t channels;       /* 1 or 2 */
    uint8_t bits;           /* 8 (unsigned) or 16 (signed) */
    uint8_t packed;         /* 1: Smacker DPCM; 0: PCM as it is */
    uint8_t playable;       /* 1: this core decodes it (not Bink audio) */
    uint32_t max_bytes;     /* the most a frame's packet unpacks to */
};

/* A big tree: 16-bit values from two byte trees, three of them cached
 * (smk_core.c) */
struct smk_bigtree {
    uint32_t *t;            /* entries, depth first; NULL: no tree, every value 0 */
    uint32_t n;
    uint32_t last[3];       /* the three cached values' entries */
};

struct smk {
    const struct smk_io *io;
    uint8_t version;        /* 2 or 4 */
    uint32_t width, height;
    uint32_t frames;        /* the film's, without the ring frame */
    uint32_t stored;        /* frames in the file: frames, plus 1 with SMK_FLAG_RING */
    uint32_t flags;
    uint32_t fps_den;       /* frames a second = 100000 / fps_den */
    struct smk_track track[SMK_TRACKS];
    uint32_t *offset;       /* stored + 1: where each frame starts, the last where the film ends */
    uint8_t *type;          /* stored: bit 0 a palette, bit 1 + i track i */
    uint8_t *key;           /* stored: 1 for a key frame */
    uint32_t max_frame;     /* the biggest frame's bytes */
    struct smk_bigtree mmap, mclr, full, types;
    uint8_t *buf;           /* max_frame bytes: the frame read */
    uint8_t *pixels;        /* width x height: the picture */
    uint8_t palette[256 * 3];
    uint32_t next;          /* the frame smk_next decodes */
    uint8_t palette_changed;
};

/* Reads the header, the tables and the trees. 0, or -1 when it is not a
 * Smacker film this core plays (smk_close is still called). */
int smk_open(struct smk *s, const struct smk_io *io);
void smk_close(struct smk *s);

/* Decodes the next frame (s->next) into s->pixels and s->palette, and
 * track's sound (-1: none) into pcm, as the track's own samples (8-bit
 * unsigned or 16-bit signed, native order, channels interleaved), at most
 * the track's max_bytes. *pcm_bytes is how many (0: none in this frame).
 * After the last stored frame it starts again at frame 0. 0, or -1 when
 * the frame can't be read or decoded (the pixels may be half drawn). */
int smk_next(struct smk *s, int track, void *pcm, uint32_t *pcm_bytes);

/* Makes frame i the next one: from frame 0 again when it is behind, then
 * decoding the frames between without their sound. 0 or -1. */
int smk_seek(struct smk *s, uint32_t i);

/* Frames a second, rounded, at least 1 */
unsigned smk_fps(const struct smk *s);

#endif
