/*
 * gfopenpng.datatype's encoder (pngencode.h).
 *
 * Each row is converted to RGB or RGBA (alpha undone first when the colour
 * is premultiplied), filtered (PNG 9.2: None, Sub, Up, Average, Paeth) and
 * handed to z.library's deflate; whatever deflate gives out goes into IDAT
 * chunks of up to 32 KB as it comes, so the picture is never held twice.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
#include "pngencode.h"

#define IDAT_MAX 32768UL
#define MAXSIDE  32768UL

struct out {
    struct pnge_env *env;
    struct Library *zbase;
    UBYTE *buf;
    ULONG len, cap;
    LONG err;
};

static void put(struct out *o, const UBYTE *p, ULONG n)
{
    ULONG i;
    if (o->err)
        return;
    if (o->len + n > o->cap) {
        ULONG cap = o->cap ? o->cap : 4096;
        UBYTE *b;
        while (cap < o->len + n)
            cap <<= 1;
        if (!(b = o->env->alloc(o->env, cap))) {
            o->err = PNGE_ERR_MEMORY;
            return;
        }
        for (i = 0; i < o->len; i++)
            b[i] = o->buf[i];
        if (o->buf)
            o->env->free(o->env, o->buf);
        o->buf = b;
        o->cap = cap;
    }
    for (i = 0; i < n; i++)
        o->buf[o->len + i] = p[i];
    o->len += n;
}

static void be32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24);
    p[1] = (UBYTE)(v >> 16);
    p[2] = (UBYTE)(v >> 8);
    p[3] = (UBYTE)v;
}

/* A chunk: length, type, data, CRC of type and data (PNG 5.3) */
static void chunk(struct out *o, const char *type, const UBYTE *data, ULONG n)
{
    UBYTE head[8], crc[4];
    ULONG c;
    be32(head, n);
    head[4] = (UBYTE)type[0];
    head[5] = (UBYTE)type[1];
    head[6] = (UBYTE)type[2];
    head[7] = (UBYTE)type[3];
    put(o, head, 8);
    put(o, data, n);
    c = zl_Crc32(o->zbase, 0, head + 4, 4);
    if (n)
        c = zl_Crc32(o->zbase, c, data, n);
    be32(crc, c);
    put(o, crc, 4);
}

static ULONG absum(const UBYTE *p, ULONG n)
{
    ULONG s = 0, i;
    for (i = 0; i < n; i++)
        s += p[i] < 128 ? p[i] : 256 - p[i];
    return s;
}

static UBYTE paeth(UBYTE a, UBYTE b, UBYTE c)
{
    LONG p = (LONG)a + b - c, pa = p - a, pb = p - b, pc = p - c;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Filter f of row cur (prev above it, bpp bytes a pixel) into dst, the
 * filter type byte first. */
static void filter(UBYTE *dst, const UBYTE *cur, const UBYTE *prev, ULONG n, ULONG bpp, ULONG f)
{
    ULONG i;
    dst[0] = (UBYTE)f;
    dst++;
    for (i = 0; i < n; i++) {
        UBYTE a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
        UBYTE x = cur[i];
        switch (f) {
        case 0: dst[i] = x; break;
        case 1: dst[i] = (UBYTE)(x - a); break;
        case 2: dst[i] = (UBYTE)(x - b); break;
        case 3: dst[i] = (UBYTE)(x - (UBYTE)(((ULONG)a + b) >> 1)); break;
        default: dst[i] = (UBYTE)(x - paeth(a, b, c)); break;
        }
    }
}

/* Deflate's output so far into IDAT chunks; flush: all of it. */
static LONG pump(struct out *o, struct zl_stream *z, UBYTE *idat, LONG mode)
{
    for (;;) {
        LONG rc = zl_Deflate(o->zbase, z, mode);
        if (rc != ZL_OK && rc != ZL_STREAM_END && rc != ZL_BUF_ERROR)
            return PNGE_ERR_DEFLATE;
        if (!z->avail_out || rc == ZL_STREAM_END) {
            ULONG n = IDAT_MAX - z->avail_out;
            if (n)
                chunk(o, "IDAT", idat, n);
            z->next_out = idat;
            z->avail_out = IDAT_MAX;
            if (rc == ZL_STREAM_END)
                return o->err;
            continue;
        }
        if (mode != ZL_FINISH && !z->avail_in)
            return o->err;
    }
}

LONG png_encode(struct Library *zbase, struct pnge_env *env, const UBYTE *argb, ULONG width,
                ULONG height, ULONG stride, ULONG flags, LONG level, UBYTE **png, ULONG *size)
{
    static const UBYTE sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    struct out o;
    struct zl_stream z;
    UBYTE ihdr[13], *mem, *cur, *prev, *best, *tmp, *idat;
    ULONG x, y, bpp = 3, n, i;
    LONG err = 0, rc;
    BOOL alpha = FALSE;

    *png = 0;
    *size = 0;
    if (!argb || !width || !height || width > MAXSIDE || height > MAXSIDE || stride < width * 4)
        return PNGE_ERR_ARGS;
    if (!(flags & PNGE_OPAQUE))
        for (y = 0; y < height && !alpha; y++)
            for (x = 0; x < width; x++)
                if (argb[y * stride + 4 * x] != 255) {
                    alpha = TRUE;
                    break;
                }
    if (alpha)
        bpp = 4;
    n = width * bpp;
    if (level < -1 || level > 9)
        level = -1;

    o.env = env;
    o.zbase = zbase;
    o.buf = 0;
    o.len = o.cap = 0;
    o.err = 0;
    /* rows: current, previous, best filtered, a trial; then the IDAT buffer */
    if (!(mem = env->alloc(env, 2 * n + 2 * (n + 1) + IDAT_MAX)))
        return PNGE_ERR_MEMORY;
    cur = mem;
    prev = cur + n;
    best = prev + n;
    tmp = best + n + 1;
    idat = tmp + n + 1;
    for (i = 0; i < n; i++)
        prev[i] = 0;

    for (i = 0; i < sizeof(z); i++)
        ((UBYTE *)&z)[i] = 0;
    if ((rc = zl_DeflateInit2(zbase, &z, level, ZL_DEFLATED, 15, 9, ZL_DEFAULT_STRATEGY)) != ZL_OK) {
        env->free(env, mem);
        return rc == ZL_MEM_ERROR ? PNGE_ERR_MEMORY : PNGE_ERR_DEFLATE;
    }
    z.next_out = idat;
    z.avail_out = IDAT_MAX;

    put(&o, sig, 8);
    be32(ihdr, width);
    be32(ihdr + 4, height);
    ihdr[8] = 8;                       /* bit depth */
    ihdr[9] = alpha ? 6 : 2;           /* RGBA or RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0; /* deflate, adaptive filters, no interlace */
    chunk(&o, "IHDR", ihdr, 13);

    for (y = 0; y < height && !err; y++) {
        const UBYTE *p = argb + y * stride;
        UBYTE *d = cur, *sw;
        ULONG f, bestsum = 0xffffffffUL;
        for (x = 0; x < width; x++, p += 4) {
            ULONG a = p[0], r = p[1], g = p[2], b = p[3];
            if ((flags & PNGE_PREMULTIPLIED) && a != 255) {
                if (!a)
                    r = g = b = 0;
                else {                  /* round to nearest, at most 255 */
                    r = (r * 255 + a / 2) / a;
                    g = (g * 255 + a / 2) / a;
                    b = (b * 255 + a / 2) / a;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
            }
            *d++ = (UBYTE)r;
            *d++ = (UBYTE)g;
            *d++ = (UBYTE)b;
            if (alpha)
                *d++ = (UBYTE)a;
        }
        for (f = 0; f < 5; f++) {
            ULONG s;
            if (level == 0 && f)
                break;
            filter(tmp, cur, prev, n, bpp, f);
            s = absum(tmp + 1, n);
            if (s < bestsum) {
                bestsum = s;
                sw = best;
                best = tmp;
                tmp = sw;
            }
        }
        z.next_in = best;
        z.avail_in = n + 1;
        err = pump(&o, &z, idat, ZL_NO_FLUSH);
        sw = prev;
        prev = cur;
        cur = sw;
    }
    if (!err) {
        z.next_in = 0;
        z.avail_in = 0;
        err = pump(&o, &z, idat, ZL_FINISH);
    }
    zl_DeflateEnd(zbase, &z);
    env->free(env, mem);
    if (!err) {
        chunk(&o, "IEND", 0, 0);
        err = o.err;
    }
    if (err) {
        if (o.buf)
            env->free(env, o.buf);
        return err;
    }
    *png = o.buf;
    *size = o.len;
    return 0;
}
