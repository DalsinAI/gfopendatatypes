/*
 * gzip.datatype's decoder (gzcore.h).
 *
 * The wrapper is told from the first two bytes: 1f 8b is gzip; a CMF and
 * FLG that RFC 1950 allows (method 8, a window of 32K or less, the pair a
 * multiple of 31) is zlib; anything else is taken as raw deflate. Those two
 * bytes are held back until the wrapper is known, then handed to inflate
 * first. After a gzip member, another may follow (RFC 1952, 2.2); anything
 * else after it is left unread, as gzip -d leaves trailing bytes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include "gzcore.h"

enum { P_HEAD, P_INFLATE, P_MEMBER, P_DONE };

void gz_init(struct gz_state *s, struct Library *zbase, LONG want)
{
    UBYTE *p = (UBYTE *)&s->z;
    ULONG i;
    for (i = 0; i < sizeof(s->z); i++)
        p[i] = 0;
    s->zbase = zbase;
    s->want = (UBYTE)(want >= GZ_AUTO && want <= GZ_RAW ? want : GZ_AUTO);
    s->kind = GZ_AUTO;
    s->phase = P_HEAD;
    s->live = 0;
    s->nhead = s->usedhead = 0;
}

const char *gz_format(const struct gz_state *s)
{
    switch (s->kind) {
    case GZ_GZIP: return "gzip";
    case GZ_ZLIB: return "zlib";
    case GZ_RAW: return "deflate";
    default: return 0;
    }
}

static UBYTE tell(const UBYTE *h, ULONG n)
{
    if (n >= 2 && h[0] == 0x1f && h[1] == 0x8b)
        return GZ_GZIP;
    if (n >= 2 && (h[0] & 0x0f) == 8 && (h[0] >> 4) <= 7 && (((ULONG)h[0] << 8) | h[1]) % 31 == 0)
        return GZ_ZLIB;
    return GZ_RAW;
}

static LONG zerr(LONG rc)
{
    return rc == ZL_MEM_ERROR ? SDT_ERR_MEMORY : SDT_ERR_DATA;
}

LONG gz_run(struct gz_state *s, struct sdt_call *c)
{
    const UBYTE *in = c->in;
    ULONG in_len = c->in_len;

    if (s->phase == P_DONE)
        return SDT_DONE;
    if (s->phase == P_HEAD) {
        LONG rc;
        if (s->want != GZ_AUTO)
            s->kind = s->want;
        else {
            while (s->nhead < 2 && c->in_used < in_len)
                s->head[s->nhead++] = in[c->in_used++];
            if (s->nhead < 2 && !(c->flags & SDT_LAST))
                return SDT_MORE;
            if (!s->nhead) {
                s->phase = P_DONE;     /* no bytes at all: an empty body */
                return SDT_DONE;
            }
            s->kind = tell(s->head, s->nhead);
        }
        rc = zl_InflateInit2(s->zbase, &s->z, s->kind == GZ_GZIP ? 31 : s->kind == GZ_ZLIB ? 15 : -15);
        if (rc != ZL_OK) {
            c->error = zerr(rc);
            return SDT_ERROR;
        }
        s->live = 1;
        s->phase = P_INFLATE;
    }
    for (;;) {
        if (s->phase == P_MEMBER) {
            ULONG left = in_len - c->in_used;
            const UBYTE *p = in + c->in_used;
            if (left >= 2 ? p[0] != 0x1f || p[1] != 0x8b : left == 1 ? p[0] != 0x1f : 0) {
                s->phase = P_DONE;
                return SDT_DONE;
            }
            if (left < 2) {
                if (c->flags & SDT_LAST) {
                    s->phase = P_DONE;
                    return SDT_DONE;
                }
                return SDT_MORE;
            }
            if (zl_InflateReset(s->zbase, &s->z) != ZL_OK) {
                c->error = SDT_ERR_DATA;
                return SDT_ERROR;
            }
            s->phase = P_INFLATE;
        }
        {
            ULONG held = s->nhead - s->usedhead, avail, before_in, before_out;
            LONG rc;
            if (held) {
                s->z.next_in = s->head + s->usedhead;
                s->z.avail_in = held;
            } else {
                s->z.next_in = in + c->in_used;
                s->z.avail_in = in_len - c->in_used;
            }
            avail = s->z.avail_in;
            s->z.next_out = c->out + c->out_made;
            s->z.avail_out = c->out_len - c->out_made;
            if (!s->z.avail_out)
                return SDT_MORE;
            before_in = avail;
            before_out = s->z.avail_out;
            rc = zl_Inflate(s->zbase, &s->z, ZL_NO_FLUSH);
            if (held)
                s->usedhead += (UBYTE)(before_in - s->z.avail_in);
            else
                c->in_used += before_in - s->z.avail_in;
            c->out_made += before_out - s->z.avail_out;
            if (rc == ZL_STREAM_END) {
                if (s->kind == GZ_GZIP) {
                    s->phase = P_MEMBER;
                    continue;
                }
                s->phase = P_DONE;
                return SDT_DONE;
            }
            if (rc == ZL_BUF_ERROR)
                return SDT_MORE;       /* no progress possible: more input or room */
            if (rc != ZL_OK) {
                c->error = zerr(rc);
                return SDT_ERROR;
            }
            if (before_in == s->z.avail_in && before_out == s->z.avail_out)
                return SDT_MORE;
            if (!held && c->in_used == in_len && s->z.avail_out)
                return SDT_MORE;       /* all input used, room left: wants more */
        }
    }
}

void gz_free(struct gz_state *s)
{
    if (s->live)
        zl_InflateEnd(s->zbase, &s->z);
    s->live = 0;
}

LONG gz_encode(struct Library *zbase, LONG kind, LONG level, const UBYTE *in, ULONG len,
               LONG (*put)(void *h, const UBYTE *p, ULONG n), void *h, UBYTE *work, ULONG room)
{
    struct zl_stream z;
    LONG rc, err = 0, wb = kind == GZ_ZLIB ? 15 : kind == GZ_RAW ? -15 : 31;
    ULONG i;
    UBYTE *zp = (UBYTE *)&z;

    if (level < -1 || level > 9)
        level = -1;
    for (i = 0; i < sizeof z; i++)             /* NULL zalloc/zfree: z.library allocates */
        zp[i] = 0;
    if ((rc = zl_DeflateInit2(zbase, &z, level, ZL_DEFLATED, wb, 8, ZL_DEFAULT_STRATEGY)) != ZL_OK)
        return rc == ZL_MEM_ERROR ? SDT_ERR_MEMORY : SDT_ERR_DATA;
    z.next_in = in;
    z.avail_in = len;
    do {
        z.next_out = work;
        z.avail_out = room;
        rc = zl_Deflate(zbase, &z, ZL_FINISH);
        if (rc != ZL_OK && rc != ZL_STREAM_END && rc != ZL_BUF_ERROR) {
            err = rc == ZL_MEM_ERROR ? SDT_ERR_MEMORY : SDT_ERR_DATA;
            break;
        }
        if (room - z.avail_out && (err = put(h, work, room - z.avail_out)) != 0)
            break;
    } while (rc != ZL_STREAM_END);
    zl_DeflateEnd(zbase, &z);
    return err;
}
