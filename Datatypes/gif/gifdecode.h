/*
 * gfopengif.datatype's decoder: GIF pictures and animations read on the
 * Amiga itself, written from CompuServe's GIF89a specification. No other
 * library.
 *
 * GIF87a and GIF89a: global and local colour tables, interlacing,
 * transparency, frame delays, the NETSCAPE2.0 (or ANIMEXTS1.0) loop count,
 * and the four disposal methods. Frames are composited as web browsers
 * show them: onto a transparent screen the size of the logical screen,
 * "restore to background" clearing the frame's area to transparent (not to
 * the background colour), "restore to previous" putting back what was
 * there before the frame. A frame that runs past the screen is cut at its
 * edge; colour indexes past the end of the table are transparent; a frame
 * whose data stops short keeps the rows it has.
 *
 * The screen comes out as 0xAARRGGBB words, top row first. GIF alpha is
 * 0 or 255 only, and every transparent pixel is 0x00000000, so the words
 * are the same premultiplied or not.
 *
 * Built to run as a job on another core later, as png_decode() is: no OS
 * calls, no static data written, only the memory it is handed.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef GIFDECODE_H
#define GIFDECODE_H

#include <exec/types.h>

#define GIF_MAXSIDE   32768UL
#define GIF_MAXPIXELS (64UL << 20)       /* 256 MB of ARGB */

struct gif_frame {
    ULONG data;                          /* offset of the LZW minimum code size byte */
    ULONG palette;                       /* offset of its colour table (local, else global) */
    UWORD colours;                       /* entries in that table; 0 none */
    UWORD x, y, w, h;                    /* where it goes on the screen */
    UWORD delay;                         /* hundredths of a second, as in the file */
    UBYTE disposal;                      /* 0 or 1 keep, 2 clear to transparent, 3 restore previous */
    UBYTE transparent;                   /* 1: index trans is transparent */
    UBYTE trans;
    UBYTE interlaced;
};

struct gif_info {
    ULONG width, height;                 /* the logical screen (frames' extent when that is 0) */
    LONG loops;                          /* -1 no loop extension (play once), 0 forever, else times */
    ULONG frames;
    UBYTE restores;                      /* 1: some frame disposes to previous (needs gif_canvas.saved) */
    UBYTE pad[3];
};

/* What gif_show() works in besides the screen: the LZW tables and the
 * palette. */
#define GIF_WORKSIZE 18432UL

struct gif_canvas {
    ULONG *argb;                         /* width * height words */
    ULONG *saved;                        /* width * height words when info.restores, else NULL */
    APTR work;                           /* GIF_WORKSIZE bytes, longword aligned */
    LONG shown;                          /* the frame on the screen now; -1 none (set it so) */
};

/* Reads the file's structure: the screen, the loop count and up to max
 * frames into frame (frame may be NULL with max 0, to count them). The
 * number of frames in the file (more than max when frame was too short),
 * or -1 when this isn't a GIF it can show (bad signature, no frame, sizes
 * out of range). */
LONG gif_scan(const UBYTE *file, ULONG len, struct gif_info *g, struct gif_frame *frame, ULONG max);

/* Puts frame index (0 to g->frames - 1) on c's screen, composited over the
 * frames before it: forward from the frame shown now, or from the start
 * when index comes before it. Its area is cleared first when c->shown is
 * -1. 0, or -1 for an index out of range. */
LONG gif_show(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *frame,
    struct gif_canvas *c, ULONG index);

/* Frame f by itself, as the colour indexes of its own table (the first
 * frame of a file shows that way: there is nothing under it). Every pixel
 * of pixels (width * height bytes) is set to fill, then the frame's pixels
 * that have a colour are written over it; *holes is the number left
 * without one (outside the frame, its transparent index, or indexes past
 * its table): those are transparent in gif_show()'s screen. work is
 * GIF_WORKSIZE bytes. 0. */
LONG gif_frame_lut8(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *f,
    UBYTE *pixels, APTR work, UBYTE fill, ULONG *holes);

/* The screen as colour indexes of one table for all frames (an
 * animation.datatype's frames): each frame's indexes go through its own
 * 256-byte remap, remap + 256 * table[frame], into that table, and
 * pixels no frame gave a colour (transparent in gif_show()'s screen) are
 * back. */
struct gif_canvas8 {
    UBYTE *lut8;                         /* width * height bytes */
    UBYTE *saved;                        /* width * height bytes when info.restores, else NULL */
    APTR work;                           /* GIF_WORKSIZE bytes, longword aligned */
    const UBYTE *remap;
    const UWORD *table;                  /* a remap for each frame */
    LONG shown;                          /* the frame on the screen now; -1 none (set it so) */
    UBYTE back;
    UBYTE pad[3];
};

/* One colour table for every frame of the file, for a gif_canvas8: the
 * background colour first (the global table's background entry, else
 * black; *back is its index), then each frame's colours, each colour once.
 * Each distinct table (frames that share one, or have the same colours,
 * share it) gets a 256-byte remap in remaps (256 * g->frames bytes,
 * cleared), and tables[frame] says which. palette is 768 bytes, RGB. The
 * number of colours, or -1 when there are more than 256. */
LONG gif_one_table(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *frame,
    UBYTE *palette, UBYTE *remaps, UWORD *tables, UBYTE *back);

/* gif_show() onto a gif_canvas8. 0, or -1 for an index out of range. */
LONG gif_show8(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *frame,
    struct gif_canvas8 *c, ULONG index);

/* 1 when any pixel of c's screen is transparent. */
LONG gif_has_alpha(const struct gif_info *g, const struct gif_canvas *c);

#endif
