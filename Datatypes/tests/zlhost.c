/*
 * z.library's calls (common/zlibrary.h) on x86 or ARM64 cores, for the
 * tests: over GFOpenZlib's own core (amicradle-guest
 * libraries/openzlib/zlib_core.cpp, built for the host by
 * tests/test-stream.sh), so the tests run the same inflate and deflate as
 * z.library. With ZLHOST_SYSTEM the host's zlib stands in instead, for a
 * machine with no amicradle-guest checkout; the test says which.
 *
 * Each zl_stream carries its host stream in its state pointer: the fields
 * the datatypes use are copied in before each call and out after it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include "zlibrary.h"

#ifdef ZLHOST_SYSTEM
#include <zlib.h>
typedef z_stream hs;
#define H_OK Z_OK
#else
#include "zlib_core.h"
typedef struct oz_stream hs;
static void *h_alloc(struct oz_env *e, oz_u32 n) { (void)e; return malloc(n ? n : 1); }
static void h_free(struct oz_env *e, void *p) { (void)e; free(p); }
static struct oz_env env = { h_alloc, h_free };
#endif

static hs *in(struct zl_stream *s)
{
    hs *h = s->state;
    h->next_in = (void *)s->next_in;
    h->avail_in = s->avail_in;
    h->next_out = s->next_out;
    h->avail_out = s->avail_out;
    return h;
}

static LONG out(struct zl_stream *s, LONG rc)
{
    hs *h = s->state;
    s->total_in += (ULONG)(h->next_in - (const unsigned char *)s->next_in);
    s->total_out += (ULONG)(h->next_out - s->next_out);
    s->next_in = h->next_in;
    s->avail_in = h->avail_in;
    s->next_out = h->next_out;
    s->avail_out = h->avail_out;
    s->msg = h->msg;
    s->adler = h->adler;
    return rc;
}

static hs *fresh(struct zl_stream *s)
{
    hs *h = calloc(1, sizeof(*h));
    s->state = h;
    s->total_in = s->total_out = 0;
    return h;
}

static LONG gone(struct zl_stream *s, LONG rc)
{
    if (rc != 0) {
        free(s->state);
        s->state = NULL;
    }
    return rc;
}

LONG zl_InflateInit2(struct Library *ZBase, struct zl_stream *s, LONG windowBits)
{
    hs *h = fresh(s);
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    return gone(s, inflateInit2(h, windowBits));
#else
    return gone(s, oz_inflateInit2(&env, h, windowBits));
#endif
}

LONG zl_Inflate(struct Library *ZBase, struct zl_stream *s, LONG flush)
{
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    return out(s, inflate(in(s), flush));
#else
    return out(s, oz_inflate(in(s), flush));
#endif
}

LONG zl_InflateEnd(struct Library *ZBase, struct zl_stream *s)
{
    LONG rc;
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    rc = inflateEnd(s->state);
#else
    rc = oz_inflateEnd(s->state);
#endif
    free(s->state);
    s->state = NULL;
    return rc;
}

LONG zl_InflateReset(struct Library *ZBase, struct zl_stream *s)
{
    (void)ZBase;
    s->total_in = s->total_out = 0;
#ifdef ZLHOST_SYSTEM
    return inflateReset(s->state);
#else
    return oz_inflateReset(s->state);
#endif
}

LONG zl_DeflateInit2(struct Library *ZBase, struct zl_stream *s, LONG level, LONG method,
                     LONG windowBits, LONG memLevel, LONG strategy)
{
    hs *h = fresh(s);
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    return gone(s, deflateInit2(h, level, method, windowBits, memLevel, strategy));
#else
    return gone(s, oz_deflateInit2(&env, h, level, method, windowBits, memLevel, strategy));
#endif
}

LONG zl_Deflate(struct Library *ZBase, struct zl_stream *s, LONG flush)
{
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    return out(s, deflate(in(s), flush));
#else
    return out(s, oz_deflate(in(s), flush));
#endif
}

LONG zl_DeflateEnd(struct Library *ZBase, struct zl_stream *s)
{
    LONG rc;
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    rc = deflateEnd(s->state);
#else
    rc = oz_deflateEnd(s->state);
#endif
    free(s->state);
    s->state = NULL;
    return rc;
}

ULONG zl_Crc32(struct Library *ZBase, ULONG crc, const UBYTE *buf, ULONG len)
{
    (void)ZBase;
#ifdef ZLHOST_SYSTEM
    return (ULONG)crc32(crc, buf, len);
#else
    return oz_crc32(crc, buf, len);
#endif
}
