/*
 * gfopentga.datatype's decoder: Targa (TGA) pictures read on the Amiga itself,
 * written from Truevision's TGA File Format Specification 2.0. No other
 * library.
 *
 * Image types 1, 2 and 3 (colour-mapped, true-colour and greyscale) and
 * their run-length encoded forms 9, 10 and 11. Colour-mapped pictures with
 * 8 or 16-bit indexes and a 15, 16, 24 or 32-bit colour map; true-colour
 * pictures of 15, 16, 24 and 32 bits; greyscale of 8 bits, or 16 with
 * alpha. Rows bottom-up (the default) or top-down, and right to left. Run
 * packets may cross the end of a row, as version 1 files let them.
 *
 * The picture comes out as 0xAARRGGBB words, top row first. Alpha is taken
 * from a 32-bit pixel or colour map entry, the top bit of a 16-bit one, or
 * a 16-bit greyscale pixel's second byte, when the header gives the
 * picture attribute (alpha) bits; a TGA 2.0 extension area that says the
 * alpha is not there, or not to be used, wins, and premultiplied alpha is
 * undone. An alpha channel that is 0 everywhere is taken as none: some
 * writers give 32-bit pictures an empty one.
 *
 * Built to run as a job on another core later, as png_decode() is:
 * tga_decode() makes no OS calls, writes no static data and touches only
 * the memory it is handed.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef TGADECODE_H
#define TGADECODE_H

#include <exec/types.h>

#define TGA_MAXPIXELS (64UL << 20)       /* 256 MB of ARGB */

struct tga_head {
    ULONG width, height;
    ULONG data_off;                      /* where the pixels start */
    ULONG map_off;                       /* where the colour map starts */
    UWORD map_first, map_len;            /* first index and entries in the colour map */
    UBYTE type;                          /* 1, 2 or 3 (the RLE forms are rle = 1) */
    UBYTE rle;
    UBYTE depth;                         /* bits per pixel: 8, 15, 16, 24, 32 */
    UBYTE map_depth;                     /* bits per colour map entry: 15, 16, 24, 32 */
    UBYTE alpha_bits;                    /* the descriptor's attribute bits */
    UBYTE top_down, right_left;
    UBYTE alpha;                         /* 1: the picture carries alpha */
    UBYTE premultiplied;                 /* 1: the extension area says so */
    UBYTE pad[3];
};

/* Reads the header (and the TGA 2.0 footer and extension area, when there
 * are any). 0, or -1 when this isn't a TGA it can read. */
LONG tga_read_head(const UBYTE *file, ULONG len, struct tga_head *h);

/* Decodes the picture into argb (width * height words), the colour map
 * first into pal (h->map_len words; NULL when there is none). 0, or -1 when
 * the header does not fit the file. Pixels a short file leaves out are
 * opaque black. *alpha says whether the result has alpha after all (an
 * empty alpha channel is dropped). No OS calls. */
LONG tga_decode(const struct tga_head *h, const UBYTE *file, ULONG len, ULONG *argb, ULONG *pal, UBYTE *alpha);

#endif
