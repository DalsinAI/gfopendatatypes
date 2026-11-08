/*
 * gfopenpng.datatype's decoder: PNG pictures read on the Amiga itself, written
 * from the PNG specification (ISO/IEC 15948, W3C PNG 3rd edition) and RFC
 * 1950/1951 (zlib's wrapper and inflate). No other library.
 *
 * Every colour type and bit depth the specification allows (greyscale 1, 2,
 * 4, 8, 16; RGB 8, 16; palette 1, 2, 4, 8; grey with alpha 8, 16; RGBA 8,
 * 16), transparency (tRNS: a palette's alphas, or the one transparent grey
 * or RGB colour) and Adam7 interlacing. The picture comes out as 0xAARRGGBB
 * words, one row after the other; 16-bit samples are taken to 8.
 *
 * Built to run as a job on another core later (gfopenmulticore.library, the
 * cores board): png_decode() makes no OS calls, writes no static data and
 * touches only the memory it is handed, which the caller allocates from the
 * sizes png_head() works out.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef PNGDECODE_H
#define PNGDECODE_H

#include <exec/types.h>

#define PNG_MAXSIDE   32768UL
#define PNG_MAXPIXELS (64UL << 20)       /* 256 MB of ARGB */

struct png_head {
    ULONG width, height;
    UBYTE depth;                         /* bits per sample */
    UBYTE ctype;                         /* 0 grey, 2 RGB, 3 palette, 4 grey+alpha, 6 RGBA */
    UBYTE interlace;                     /* 0 none, 1 Adam7 */
    UBYTE alpha;                         /* 1: colour type 4 or 6, or a tRNS chunk */
    UBYTE has_key;                       /* a tRNS colour for grey or RGB */
    UBYTE pad[3];
    UWORD key[3];                        /* that colour: grey, or R, G, B (at the picture's depth) */
    ULONG idat_len;                      /* all the IDAT chunks' bytes together */
    ULONG raw_len;                       /* the inflated rows of every pass, with their filter bytes */
    ULONG palette[256];                  /* ARGB; tRNS alphas applied */
};

/* What png_decode() needs to work in, besides the picture: the inflate state
 * and Huffman tables. */
#define PNG_WORKSIZE 4096UL

/* Reads the chunks before the pixels. 0, or -1 when this isn't a PNG it can
 * read (bad signature, IHDR, sizes or a missing IDAT). */
LONG png_read_head(const UBYTE *file, ULONG len, struct png_head *h);

/* Copies the IDAT chunks' data together into idat (h->idat_len bytes). */
void png_gather(const UBYTE *file, ULONG len, UBYTE *idat);

/* Where png_strip() has got to (the caller keeps it; no OS calls, no
 * static data). */
struct png_rows {
    ULONG got;                           /* bytes inflated */
    ULONG at;                            /* plain: where the next row starts in raw */
    ULONG y;                             /* the next row png_strip() gives */
    const UBYTE *prev;                   /* plain: the row above, its filter undone */
    ULONG pass_at[7];                    /* Adam7: where each pass starts in raw */
    ULONG pass_rows[7];                  /* Adam7: how many of its rows came */
};

/* Inflates idat into raw (h->raw_len bytes); with Adam7 it also undoes
 * every pass's filters. work is PNG_WORKSIZE bytes, longword aligned, and
 * idat and work are not needed after it. 0, or -1 when nothing came out of
 * the stream. A stream that ends early or goes wrong keeps what it gave. */
LONG png_unpack(const struct png_head *h, const UBYTE *idat, UBYTE *raw, APTR work, struct png_rows *r);

/* The next n rows of the picture into argb (n * width words, ARGB): a strip
 * to hand on while the rest waits in raw. Rows (and with Adam7, pixels)
 * that never came are cleared; nothing else is written twice. Strips are
 * taken top to bottom, up to the picture's height. */
void png_strip(const struct png_head *h, UBYTE *raw, struct png_rows *r, ULONG *argb, ULONG n);

/* png_unpack(), then the whole picture as one strip into argb (width *
 * height words). 0, or -1 on a damaged stream. */
LONG png_decode(const struct png_head *h, const UBYTE *idat, UBYTE *raw, ULONG *argb, APTR work);

#endif
