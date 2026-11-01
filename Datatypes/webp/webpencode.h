/*
 * webp.datatype's encoder: 32-bit ARGB pixels to a lossless WebP file, by
 * libwebp's own lossless encoder (VP8LEncodeImage, BSD 3-clause with the
 * WebM patent grant), handed to the caller's writer a piece at a time.
 *
 * Lossless only: a picture saved and opened again is the same, pixel for
 * pixel, the colour under fully transparent pixels too ("exact"). Built
 * with libwebp-1.6.0-encoder-no-float.patch, so neither it nor the encoder
 * uses floating point (no FPU, no ROM math libraries), as the decoder.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team (this file; libwebp keeps its
 * licence).
 * SPDX-License-Identifier: MIT
 */
#ifndef WEBPENCODE_H
#define WEBPENCODE_H

#include <exec/types.h>

/* The IoErr() codes it gives */
#define WEBPE_ERR_MEMORY 103L      /* ERROR_NO_FREE_STORE */
#define WEBPE_ERR_ARGS   116L      /* ERROR_REQUIRED_ARG_MISSING: no pixels or a size WebP can't hold */
#define WEBPE_ERR_ENCODE 2001L     /* DTERROR_COULDNT_SAVE: the encoder said no */

/* Encodes width x height pixels, each one 32-bit word 0xAARRGGBB in the
 * machine's order (picture.datatype's PBPAFMT_ARGB on the 68k), rows
 * stride words apart, top row first. The pixels may be changed in place.
 * level: -1 the default (a middle effort), 0 the fastest, up to 9 the
 * smallest file (libwebp's methods 0 to 6, with its quality as effort).
 * put() gets the file a piece at a time and returns 0, or an error code
 * that stops the encode and comes back. 0, or an error code. Width and
 * height are 1 to 16383. */
LONG webp_encode_lossless(ULONG *argb, ULONG width, ULONG height, ULONG stride, LONG level,
                          LONG (*put)(void *h, const UBYTE *p, ULONG n), void *h);

#endif
