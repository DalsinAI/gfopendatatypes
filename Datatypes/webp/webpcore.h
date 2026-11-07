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

/* Sets up every function table and look-up table libwebp's decoder,
 * demuxer and lossless encoder pick on first use, so that no decode or
 * encode writes them afterwards: called once, from dt_init(), while the
 * library loads, and decodes in several tasks at once only read them.
 * 0 when this libwebp isn't the one the datatype was built for. */
int webp_core_init(void);

#endif
