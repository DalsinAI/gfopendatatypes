/*
 * cdxl.datatype's core: CDXL frame headers, palettes, pictures and sound,
 * with no Amiga headers, so it is tested on the host and under qemu-m68k
 * (tests/host/test_cdxl.sh).
 *
 * CDXL is the CDTV's streaming video: frame after frame, each a 32-byte
 * header, its palette (12-bit colours), its picture and its sound. Header:
 * +0 type (1 standard), +1 info (bits 0-2 the colours: 0 a palette, 1 HAM;
 * bit 4 stereo; bits 5-7 the pixels: 0 one bitplane after another, 0x20
 * chunky, 0x80 the planes of each row in turn), +2 this frame's bytes, +6
 * the last frame's, +10 the frame's number, +14 width, +16 height, +19
 * bitplanes (24 for chunky RGB), +20 the palette's bytes, +22 the sound's
 * bytes a channel (left, then right), +24 the sound's rate and +26 frames a
 * second where the writer put them (0: not given). Rows of bitplanes are
 * padded to 16 bits.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef CDXL_CORE_H
#define CDXL_CORE_H

#include <stdint.h>

#define CDXL_HEAD 32
#define CDXL_PLANAR 0x00
#define CDXL_CHUNKY 0x20
#define CDXL_LINE   0x80

struct cdxl_frame {
    uint32_t size;          /* the whole frame, header and all */
    uint16_t width, height;
    uint8_t planes;         /* 1 to 8, or 24 for chunky RGB */
    uint8_t ham;            /* 1: hold-and-modify (HAM6 with 6 planes, HAM8 with 8) */
    uint8_t layout;         /* CDXL_PLANAR, CDXL_CHUNKY or CDXL_LINE */
    uint8_t stereo;
    uint16_t colours;       /* palette entries */
    uint16_t audio;         /* sound bytes a channel */
    uint16_t rate;          /* 0: not given */
    uint8_t fps;            /* 0: not given */
};

/* Reads a header; 0 when it is one this core plays, -1 otherwise. */
int cdxl_parse(const uint8_t head[CDXL_HEAD], struct cdxl_frame *f);

/* Bytes of a row of one bitplane, and of the whole picture. */
uint32_t cdxl_rowbytes(const struct cdxl_frame *f);
uint32_t cdxl_image_bytes(const struct cdxl_frame *f);

/* Offsets in the frame of the palette, the picture and the sound. */
uint32_t cdxl_palette_at(const struct cdxl_frame *f);
uint32_t cdxl_image_at(const struct cdxl_frame *f);
uint32_t cdxl_audio_at(const struct cdxl_frame *f);

/* Where row y of bitplane p starts in the picture. */
uint32_t cdxl_plane_row(const struct cdxl_frame *f, unsigned p, unsigned y);

/* The bitplanes as one byte a pixel (each pixel's plane bits), rows packed. */
void cdxl_to_chunky(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out);

/* The palette as 8-bit R, G, B bytes (each 4-bit channel x 17). */
void cdxl_palette(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *rgb);

/* A chunky RGB picture as indices into the 6x6x6 colour cube (i = r * 36 +
 * g * 6 + b, each step 51), with a 4x4 ordered dither: pixel (r, g, b) at
 * Bayer value d is ((r * 5 + d * 16) >> 8) * 36 + ... + ((b * 5 + d * 16) >> 8),
 * from const tables (cdxl_cube_tables.h, common/mkcubedither.py); and one
 * pixel of it. */
void cdxl_rgb_to_cube(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out);
uint8_t cdxl_cube_pixel(unsigned d, unsigned r, unsigned g, unsigned b);

/* The sound as signed 8-bit mono (stereo mixed), audio bytes to out. */
void cdxl_mono(const uint8_t *frame, const struct cdxl_frame *f, int8_t *out);

/* Frames a second: the header's, else the sound's rate over its length,
 * else 12. rate is the sound's (the header's, else 11025). */
unsigned cdxl_fps(const struct cdxl_frame *f);
unsigned cdxl_rate(const struct cdxl_frame *f);

/* What the frame shows, as R, G, B bytes (for tests): the bitplanes
 * through the palette, HAM decoded, or the RGB itself. */
void cdxl_show(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *rgb);

/* ---- where the frames are ------------------------------------------------- */

/* The film, as the datatype reads it (a DOS handle) and the tests do */
struct cdxl_io {
    void *ctx;
    /* n bytes at byte at into buf: how many were read, -1 on an error */
    long (*read)(void *ctx, uint32_t at, void *buf, uint32_t n);
    /* the film's length in bytes, -1 when it can't be told */
    long (*size)(void *ctx);
    void *(*alloc)(void *ctx, uint32_t n);
    void (*free)(void *ctx, void *p);
};

/* Same picture: size, planes, layout and HAM (a frame that differs ends
 * the film, as a cut-short file does) */
int cdxl_same(const struct cdxl_frame *a, const struct cdxl_frame *b);

struct cdxl_index {
    uint32_t frames;
    uint32_t fixed;         /* every frame this many bytes, frame i at i * fixed; 0: offsets */
    uint32_t *offsets;      /* frames + 1 of them (the last: where the last frame ends) */
    uint32_t max_size;      /* the biggest frame's bytes */
    uint32_t max_audio;     /* the most sound a frame can have, a channel */
    struct cdxl_frame first;
};

/* Finds the frames. In constant time when they can be (the usual film:
 * every frame the first's size, the length a whole number of them, and
 * the last frame's header the same as the first's): the first and last
 * headers and the length, three reads; else frame by frame, a header
 * read each, until one doesn't parse or differs (cdxl_same) or the film
 * ends. 0, or -1 when there is no frame. */
int cdxl_index_open(const struct cdxl_io *io, struct cdxl_index *ix);
void cdxl_index_free(const struct cdxl_io *io, struct cdxl_index *ix);

/* Frame i, whole, into buf (ix->max_size bytes), in one read, its header
 * into *f. A frame found in constant time whose header turns out not to
 * be one like the first's (a film whose frames' sizes vary yet add up)
 * has the film indexed frame by frame and is read again. 0, or -1 (no
 * such frame, a read error, a header that doesn't parse or differs). */
int cdxl_read_frame(const struct cdxl_io *io, struct cdxl_index *ix, uint32_t i, uint8_t *buf, struct cdxl_frame *f);

#endif
