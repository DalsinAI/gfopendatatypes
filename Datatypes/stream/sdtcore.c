/*
 * stream.datatype's core (sdtcore.h).
 *
 * The coded bytes wait in one buffer, q, from q_start to q_end. A feed
 * moves what is left to the front and grows the buffer only when it must,
 * so a program that takes as it feeds keeps it at the size of a piece.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include "sdtcore.h"

#define PULL_CHUNK 8192UL
#define FIRST_OUT  16384UL

static void copy(UBYTE *d, const UBYTE *s, ULONG n)
{
    while (n--)
        *d++ = *s++;
}

void sdt_init(struct sdt_core *c, struct sdt_env *env, sdt_codec_fn codec, void *ctx)
{
    c->env = env;
    c->codec = codec;
    c->codec_ctx = ctx;
    c->pull = 0;
    c->pull_ctx = 0;
    c->q = 0;
    c->q_start = c->q_end = c->q_size = 0;
    c->ended = c->done = c->failed = c->pad = 0;
    c->error = 0;
    c->total_in = c->total_out = 0;
}

void sdt_set_pull(struct sdt_core *c, sdt_pull_fn pull, void *ctx)
{
    c->pull = pull;
    c->pull_ctx = ctx;
}

static void fail(struct sdt_core *c, LONG err)
{
    c->failed = 1;
    c->error = err;
}

/* Room for n more bytes after q_end. 0, or SDT_ERR_MEMORY. */
static LONG room(struct sdt_core *c, ULONG n)
{
    ULONG left = c->q_end - c->q_start, size;
    UBYTE *q;
    if (c->q_start) {
        copy(c->q, c->q + c->q_start, left);
        c->q_start = 0;
        c->q_end = left;
    }
    if (c->q_size - c->q_end >= n)
        return 0;
    size = c->q_size ? c->q_size : 4096;
    while (size - left < n) {
        if (size > 0x7fffffffUL)
            return SDT_ERR_MEMORY;
        size <<= 1;
    }
    if (!(q = c->env->alloc(c->env, size)))
        return SDT_ERR_MEMORY;
    if (c->q) {
        copy(q, c->q, left);
        c->env->free(c->env, c->q);
    }
    c->q = q;
    c->q_size = size;
    return 0;
}

LONG sdt_feed(struct sdt_core *c, const UBYTE *data, ULONG len)
{
    LONG err;
    if (c->failed)
        return c->error;
    if (c->ended)
        return SDT_ERR_ENDED;
    if (c->done || !len)
        return 0;                      /* past the end of the stream: not needed */
    if ((err = room(c, len)))
        return err;
    copy(c->q + c->q_end, data, len);
    c->q_end += len;
    return 0;
}

void sdt_end(struct sdt_core *c)
{
    c->ended = 1;
}

ULONG sdt_take(struct sdt_core *c, UBYTE *buf, ULONG len, ULONG *flags)
{
    ULONG made = 0;
    UBYTE stalled = 0;
    *flags = 0;
    while (!c->done && !c->failed && made < len) {
        struct sdt_call call;
        LONG rc;
        if ((stalled || c->q_start == c->q_end) && !c->ended && c->pull) {
            LONG err = 0;
            ULONG got;
            if ((err = room(c, PULL_CHUNK))) {
                fail(c, err);
                break;
            }
            got = c->pull(c->pull_ctx, c->q + c->q_end, PULL_CHUNK, &err);
            if (got == (ULONG)-1) {
                fail(c, err ? err : SDT_ERR_DATA);
                break;
            }
            if (!got)
                c->ended = 1;
            c->q_end += got;
            stalled = 0;
        }
        call.op = SDT_OP_RUN;
        call.flags = c->ended ? SDT_LAST : 0;
        call.in = c->q ? c->q + c->q_start : 0;
        call.in_len = c->q_end - c->q_start;
        call.out = buf + made;
        call.out_len = len - made;
        call.in_used = call.out_made = 0;
        call.error = 0;
        rc = c->codec(c->codec_ctx, &call);
        if (call.in_used > call.in_len || call.out_made > call.out_len) {
            fail(c, SDT_ERR_DATA);     /* a decoder that says more than it was given */
            break;
        }
        c->q_start += call.in_used;
        c->total_in += call.in_used;
        made += call.out_made;
        c->total_out += call.out_made;
        if (rc == SDT_DONE)
            c->done = 1;
        else if (rc != SDT_MORE)
            fail(c, call.error ? call.error : SDT_ERR_DATA);
        else if (!call.in_used && !call.out_made) {
            if (c->ended)
                fail(c, SDT_ERR_SHORT);
            else if (c->pull && !stalled)
                stalled = 1;           /* pull more, then try again */
            else
                break;                 /* wait for a feed */
            continue;
        }
        stalled = 0;
    }
    if (c->failed)
        *flags |= SDT_F_ERROR;
    else if (c->done)
        *flags |= SDT_F_END;
    else if (made < len)
        *flags |= SDT_F_HUNGRY;
    return made;
}

LONG sdt_decode_all(struct sdt_core *c, UBYTE **data, ULONG *size)
{
    UBYTE *out = 0;
    ULONG have = 0, cap = 0, flags = 0;
    *data = 0;
    *size = 0;
    c->ended = c->pull ? c->ended : 1;
    for (;;) {
        if (have == cap) {
            ULONG ncap = cap ? cap << 1 : FIRST_OUT;
            UBYTE *n;
            if (ncap <= cap || !(n = c->env->alloc(c->env, ncap))) {
                if (out)
                    c->env->free(c->env, out);
                return SDT_ERR_MEMORY;
            }
            if (out) {
                copy(n, out, have);
                c->env->free(c->env, out);
            }
            out = n;
            cap = ncap;
        }
        have += sdt_take(c, out + have, cap - have, &flags);
        if (flags & SDT_F_ERROR) {
            c->env->free(c->env, out);
            return c->error;
        }
        if (flags & SDT_F_END)
            break;
        if ((flags & SDT_F_HUNGRY) && have < cap) {
            /* no pull and no more input: cut short */
            c->env->free(c->env, out);
            return SDT_ERR_SHORT;
        }
    }
    *data = out;
    *size = have;
    return 0;
}

void sdt_free(struct sdt_core *c)
{
    if (c->q)
        c->env->free(c->env, c->q);
    c->q = 0;
    c->q_start = c->q_end = c->q_size = 0;
}
