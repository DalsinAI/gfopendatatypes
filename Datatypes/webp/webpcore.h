/*
 * webp.datatype's libwebp side with no Amiga calls in it (webpcore.c), so
 * that tests/ runs the same code on a PC and under qemu-m68k.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team (this file; libwebp keeps its
 * licence).
 * SPDX-License-Identifier: MIT
 */
#ifndef WEBPCORE_H
#define WEBPCORE_H

#include <stddef.h>
#include <stdint.h>

/* Sets up every function table and look-up table libwebp's decoder,
 * demuxer and lossless encoder pick on first use, so that no decode or
 * encode writes them afterwards: called once, from dt_init(), while the
 * library loads, and decodes in several tasks at once only read them.
 * 0 when this libwebp isn't the one the datatype was built for. */
int webp_core_init(void);

/* The first frame of an animated WebP as webp.datatype shows it, ARGB
 * (A, R, G, B bytes), *w x *h pixels for the canvas at (*x, *y); free it
 * with WebPFree(). An animation without alpha whose first frame doesn't
 * cover the canvas comes as the whole canvas, filled with the ANIM chunk's
 * background colour (opaque) around the frame: picture.datatype shows no
 * alpha for it, so the rest would be black. With alpha the rest stays
 * clear, as libwebp's own animation decoder leaves it. NULL if it can't be
 * read. */
uint8_t *webp_first_frame(const uint8_t *data, size_t size, int has_alpha,
                          int *x, int *y, int *w, int *h);

#endif
