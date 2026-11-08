/* GFOpenDatatypes animation.datatype: the H.264 encoder under the MP4
 * writer. See mp4_h264.h.
 * Copyright (c) 2026 AmiCradle Team. MIT (this file); h264/minih264e.h is
 * CC0 (upstream/minih264/LICENSE).
 *
 * minih264e is built here with what the class needs only: no SVC layers,
 * no threads, no temporal denoise, the plain C code (no SSE or NEON; on a
 * host MINIH264_NO_SIMD gives the same code, which the tests compare with
 * the m68k build byte for byte). Its memset, memcpy and memmove are ours,
 * below, so the object calls nothing outside itself: a class library links
 * no C library.
 */
#define NDEBUG 1
#define H264E_SVC_API 0
#define H264E_MAX_THREADS 0
#define H264E_ENABLE_DENOISE 0
#ifndef MINIH264_NO_SIMD
#define MINIH264_NO_SIMD 1
#endif
#define MINIH264_PADDED_INPUT 1          /* mp4h_encode's planes are whole macroblocks */

#include <stddef.h>
#include <string.h>
#include "mp4_h264.h"

static void *mp4h_memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static void *mp4h_memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

static void *mp4h_memmove(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q) {
        while (n--) *p++ = *q++;
    } else {
        p += n;
        q += n;
        while (n--) *--p = *--q;
    }
    return d;
}

#define memset mp4h_memset
#define memcpy mp4h_memcpy
#define memmove mp4h_memmove

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wpointer-arith"
#endif
/* No jump tables: a switch's table of 16-bit offsets sits in the code, and
 * an offset of $F2xx or $F3xx reads as an FPU instruction to objdump (GCC
 * 15's H264E_encode had one, the frame type's switch, read as FSAVE), so
 * the test that the encoder has no FPU instructions could not tell. Compare
 * chains instead, the same code as far as anything else goes. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("no-jump-tables")
#endif
#define MINIH264_IMPLEMENTATION
#include "h264/minih264e.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* The encoder's own state, then its scratch, in the caller's memory */
struct mp4h {
    H264E_persist_t *enc;
    H264E_scratch_t *scratch;
    unsigned long w, h;
};

#define MP4H_ALIGN(p) ((unsigned char *)(((uintptr_t)(p) + 15) & ~(uintptr_t)15))

static int mp4h_params(H264E_create_param_t *cp, unsigned long w, unsigned long h, unsigned long gop)
{
    if (w < 2 || h < 2 || w > 4080 || h > 4080 || (w & 1) || (h & 1)) return 0;
    mp4h_memset(cp, 0, sizeof *cp);
    cp->width = (int)w;
    cp->height = (int)h;
    cp->gop = (int)gop;
    cp->const_input_flag = 0;          /* the input is ours to write over: one frame less */
    return 1;
}

unsigned long mp4h_size(unsigned long w, unsigned long h)
{
    H264E_create_param_t cp;
    int sp, ss;
    if (!mp4h_params(&cp, w, h, 0) || H264E_sizeof(&cp, &sp, &ss) || sp <= 0 || ss <= 0) return 0;
    return sizeof(struct mp4h) + 16 + (unsigned long)sp + 16 + (unsigned long)ss + 16;
}

void *mp4h_init(void *mem, unsigned long w, unsigned long h, unsigned long gop)
{
    H264E_create_param_t cp;
    struct mp4h *e;
    int sp, ss;
    unsigned char *p;
    if (!mem || !mp4h_params(&cp, w, h, gop) || H264E_sizeof(&cp, &sp, &ss)) return 0;
    e = (struct mp4h *)MP4H_ALIGN(mem);
    p = MP4H_ALIGN((unsigned char *)e + sizeof *e);
    e->enc = (H264E_persist_t *)p;
    e->scratch = (H264E_scratch_t *)MP4H_ALIGN(p + sp);
    e->w = w;
    e->h = h;
    if (H264E_init(e->enc, &cp)) return 0;
    return e;
}

int mp4h_encode(void *enc, unsigned char *y, unsigned char *u, unsigned char *v, unsigned long qp,
                unsigned long speed, unsigned char **out, unsigned long *n)
{
    struct mp4h *e = enc;
    H264E_run_param_t rp;
    H264E_io_yuv_t io;
    unsigned char *data = 0;
    int size = 0;
    if (!e || !y || !u || !v || !out || !n) return 1;
    if (qp < 10) qp = 10;
    if (qp > 51) qp = 51;
    if (speed > 10) speed = 10;
    mp4h_memset(&rp, 0, sizeof rp);
    rp.encode_speed = (int)speed;
    rp.qp_min = rp.qp_max = (int)qp;   /* a fixed quantiser: no rate control */
    io.yuv[0] = y;
    io.yuv[1] = u;
    io.yuv[2] = v;
    io.stride[0] = (int)MP4H_PAD(e->w);
    io.stride[1] = io.stride[2] = (int)(MP4H_PAD(e->w) >> 1);
    if (H264E_encode(e->enc, e->scratch, &rp, &io, &data, &size) || !data || size <= 0) return 1;
    *out = data;
    *n = (unsigned long)size;
    return 0;
}
