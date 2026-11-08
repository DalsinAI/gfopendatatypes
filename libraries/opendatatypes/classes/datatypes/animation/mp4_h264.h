/* GFOpenDatatypes animation.datatype: the H.264 encoder under the MP4
 * writer (mp4_core.c), in plain C with no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * mp4_h264.c builds minih264e (h264/minih264e.h, CC0, upstream commit
 * b0baea7 with our big-endian patches: upstream/minih264/) for the class:
 * H.264 baseline profile (CAVLC, one slice a frame, one reference frame),
 * a fixed quantiser, integer only, no SIMD, no threads, no SVC. It calls
 * nothing outside itself: not even libgcc's arithmetic on a 68040, and no
 * 64-bit multiplies or divides (the 68060 traps them). Plain C types, so
 * a host builds the same code for the tests.
 */
#ifndef OPENDATATYPES_MP4_H264_H
#define OPENDATATYPES_MP4_H264_H

/* Bytes the encoder needs for a film of w x h (both even, 2 to 4080):
 * 0 when it can't take that size */
unsigned long mp4h_size(unsigned long w, unsigned long h);

/* Starts an encoder in mem (mp4h_size bytes, any alignment, any contents):
 * a key frame (IDR, with the SPS and PPS before it) every gop frames, 0
 * the first only. NULL when it can't. */
void *mp4h_init(void *mem, unsigned long w, unsigned long h, unsigned long gop);

/* The planes' row lengths and rows: w and h rounded up to 16 (whole
 * macroblocks), and half that for U and V */
#define MP4H_PAD(n) (((n) + 15) & ~15UL)

/* Encodes the next frame: y holds w x h pixels in MP4H_PAD(w) x
 * MP4H_PAD(h) bytes, rows MP4H_PAD(w) apart; u and v w/2 x h/2 in half
 * that (4:2:0, BT.601's studio range). The encoder writes over all of it,
 * the padding too (it becomes its reconstruction). qp 10 to 51, speed 0
 * (smallest) to 10 (fastest). The frame's NAL units, each after a 00 00
 * 00 01 start code, are at *out for *n bytes, the caller's to change,
 * until the next call. 0, or not 0 when it failed. */
int mp4h_encode(void *enc, unsigned char *y, unsigned char *u, unsigned char *v, unsigned long qp,
                unsigned long speed, unsigned char **out, unsigned long *n);

#endif
