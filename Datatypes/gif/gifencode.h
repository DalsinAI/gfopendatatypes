/*
 * gfopengif.datatype's encoder: pictures to a GIF89a file in memory, written
 * from CompuServe's GIF89a specification, as the decoder (gifdecode.c) was.
 * Our own LZW (its patents ran out in 2003 and 2004); no other library.
 *
 * One frame, not interlaced, with one global colour table. GIF holds 256
 * colours at most and alpha only as one transparent colour:
 *
 * - gif_encode_lut8() writes colour indexes and their table as they are (a
 *   picture of 256 colours or fewer saves exactly), with one index as the
 *   transparent one if asked.
 * - gif_encode() takes 32-bit ARGB. A pixel with alpha under 128 is
 *   transparent (one table entry, given to all of them), any other is
 *   opaque. A picture of 256 colours or fewer (255 with transparent
 *   pixels) is written exactly; one with more is reduced to 256 by median
 *   cut on a 15-bit histogram (no dithering), in integers only.
 *
 * Built to run as a job on another core later, as gif_show() is: no OS
 * calls, no static data written, only the memory env gives it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef GIFENCODE_H
#define GIFENCODE_H

#include <exec/types.h>

struct gife_env {
    void *(*alloc)(struct gife_env *env, ULONG size);
    void (*free)(struct gife_env *env, void *mem);
};

/* Flags */
#define GIFE_OPAQUE 1UL         /* gif_encode(): ignore alpha, no transparent colour */

/* The IoErr() codes it gives */
#define GIFE_ERR_MEMORY 103L    /* ERROR_NO_FREE_STORE */
#define GIFE_ERR_ARGS   116L    /* ERROR_REQUIRED_ARG_MISSING: no pixels, or a size GIF can't hold */

/* Encodes width x height colour indexes (one byte each, rows stride bytes
 * apart, top row first) with their table: colours entries (1 to 256) of
 * R, G, B bytes. transparent is the index that is transparent, or -1 for
 * none. Every index must be below colours. 0 and the file in *gif (from
 * env, *size bytes), or an error code. Width and height are 1 to 65535. */
LONG gif_encode_lut8(struct gife_env *env, const UBYTE *index, ULONG width, ULONG height, ULONG stride,
                     const UBYTE *rgb, ULONG colours, LONG transparent, UBYTE **gif, ULONG *size);

/* Encodes width x height pixels, each four bytes A, R, G, B (0xAARRGGBB on
 * the 68k: picture.datatype's PBPAFMT_ARGB), rows stride bytes apart, top
 * row first, as described above. 0 and the file in *gif (from env, *size
 * bytes), or an error code. Width and height are 1 to 65535. */
LONG gif_encode(struct gife_env *env, const UBYTE *argb, ULONG width, ULONG height, ULONG stride,
                ULONG flags, UBYTE **gif, ULONG *size);

#endif
