/*
 * gfopentga.datatype's encoder: 32-bit ARGB pixels to a Targa (TGA) file in
 * memory, written from Truevision's TGA File Format Specification 2.0, as
 * the decoder (tgadecode.c) was.
 *
 * The file is true-colour, 32 bits with 8 bits of alpha when any pixel is
 * not opaque, else 24 bits; run-length encoded (image type 10) unless the
 * caller asks for none (type 2). Rows are written top row first (the image
 * descriptor's bit 5), packets never cross the end of a row (as version 2
 * asks), and the file ends with the TGA 2.0 footer with no extension area
 * or developer directory, so a reader takes the alpha from the descriptor's
 * attribute bits.
 *
 * Built to run as a job on another core later, as tga_decode() is: it makes
 * no OS calls, writes no static data and gets its memory from the caller.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef TGAENCODE_H
#define TGAENCODE_H

#include <exec/types.h>

struct tgae_env {
    void *(*alloc)(struct tgae_env *env, ULONG size);
    void (*free)(struct tgae_env *env, void *mem);
};

/* Flags */
#define TGAE_OPAQUE 1UL         /* ignore alpha: always 24 bits */
#define TGAE_RAW    2UL         /* no run-length encoding: image type 2 */

/* The IoErr() codes it gives */
#define TGAE_ERR_MEMORY 103L    /* ERROR_NO_FREE_STORE */
#define TGAE_ERR_ARGS   116L    /* ERROR_REQUIRED_ARG_MISSING: no pixels, or a size TGA can't hold */

/* Encodes width x height pixels, each four bytes A, R, G, B (0xAARRGGBB on
 * the 68k: picture.datatype's PBPAFMT_ARGB), rows stride bytes apart, top
 * row first. 0 and the file in *tga (from env, *size bytes), or an error
 * code. Width and height are 1 to 65535. */
LONG tga_encode(struct tgae_env *env, const UBYTE *argb, ULONG width, ULONG height, ULONG stride,
                ULONG flags, UBYTE **tga, ULONG *size);

#endif
