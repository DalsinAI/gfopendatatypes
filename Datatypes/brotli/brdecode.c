/*
 * brotli.datatype's decoder (brdecode.h), from RFC 7932.
 *
 * How it streams. The coded bytes are copied into a buffer of 4 KB (ib),
 * and the decoder works in steps: a meta-block header, one category's block
 * types, one prefix code, one context map entry, one command, one literal,
 * and so on. A step reads all its bits before it changes anything, so when
 * the input runs out in the middle of one, the bit position goes back to
 * where the step began and the step runs again once more bytes are in. The
 * largest step, a prefix code of the 704-symbol alphabet, needs well under
 * a kilobyte, so the buffer always holds a whole step. Bits past the end of
 * the input read as zeros and only mark the step short ("shortf"), so a
 * step never mistakes a lack of input for damaged data.
 *
 * The plain bytes go to the caller and into the window (ring) at once. The
 * window starts at 32 KB and doubles as the output grows, up to the 2^WBITS
 * bytes the stream header asks for, then wraps.
 *
 * Prefix codes decode with an 8-bit table and canonically beyond (as
 * gfopenpng's inflate does). Symbols are read with the first bit of the code
 * in the lowest bit, as in deflate (RFC 7932, 3.1).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
#include "brdecode.h"

#define IBSIZE   4096UL
#define FASTBITS 8
#define FASTSIZE (1UL << FASTBITS)
#define NOSINGLE 0xffffU
#define FIRST_RING 32768UL

enum { R_OK, R_NEED, R_ERR, R_FULL };

enum {
    S_HEAD, S_MBHEAD, S_METADATA, S_UNCOMP, S_BT, S_DP, S_NTD, S_CMHEAD, S_CMENT, S_CMMTF,
    S_TREES, S_CMD, S_INSERT, S_DIST, S_COPY, S_WORD, S_DONE
};

struct tree {
    UWORD count[16];               /* codes of each length */
    UWORD single;                  /* the one symbol of a code of no bits, or NOSINGLE */
    UWORD pad;
    UWORD *fast;                   /* FASTSIZE entries: (length << 11) | symbol; 0: longer */
    UWORD *sym;                    /* symbols in code order */
};

struct group {                     /* one category's prefix codes for a meta-block */
    struct tree *t;                /* the trees, then their tables, in one block */
    ULONG n;
};

struct br_state {
    struct sdt_env *env;
    LONG err;

    /* input */
    ULONG ilen, bpos, mark;
    UBYTE shortf, state, islast, cm_target;

    /* output of this run */
    UBYTE *out;
    ULONG room, made;

    /* window */
    UBYTE *ring;
    ULONG rsize, rmax, rpos, window, total;
    UBYTE full, p1, p2, implicit;

    /* the meta-block */
    ULONG mlen;
    ULONG nbt[3], btype[3], bcount[3], brb[3][2];
    ULONG bt_k;
    ULONG npostfix, ndirect, ntl, ntd;
    UBYTE *cmapl, *cmapd;
    struct group gl, gi, gd;
    ULONG tree_i;

    /* a context map being read */
    UBYTE *cm;
    ULONG cm_size, cm_i, cm_ntrees, rlemax;

    /* the command */
    ULONG insert, copy, dist, wlen, wpos;
    ULONG last4[4];
    UBYTE word[48];

    struct tree tt[3], tc[3], tcm, tcl;
    UBYTE cmodes[256];
    UBYTE lens[704];
    UWORD fixfast[8][FASTSIZE];
    UWORD fixsym[3 * 258 + 3 * 26 + 272 + 18];
    UBYTE ib[IBSIZE];
};

/* ---- tables (RFC 7932) ---------------------------------------------------- */

/* 3.5: the order of the code length code lengths */
static const UBYTE cl_order[18] = { 1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* 3.5: the fixed code for the code length code lengths (0 00, 3 01, 4 10,
 * 2 110, 1 1110, 5 1111), by the next four bits as they arrive */
static const UBYTE clcl_len[16] = { 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4 };
static const UBYTE clcl_val[16] = { 0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5 };

/* 6: block counts */
static const UWORD bc_base[26] = {
    1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113, 145, 177, 209, 241, 305, 369, 497,
    753, 1265, 2289, 4337, 8433, 16625
};
static const UBYTE bc_extra[26] = {
    2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24
};

/* 5: insert and copy lengths, and the cells of the command alphabet */
static const UWORD ins_base[24] = {
    0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594
};
static const UBYTE ins_extra[24] = {
    0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24
};
static const UWORD cpy_base[24] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54, 70, 102, 134, 198, 326, 582, 1094, 2118
};
static const UBYTE cpy_extra[24] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24
};
static const UBYTE cell_ins[11] = { 0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16 };
static const UBYTE cell_cpy[11] = { 0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16 };

/* 4: the sixteen short distance codes: which of the last four, and how far off */
static const UBYTE sd_index[16] = { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
static const BYTE sd_delta[16] = { 0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3 };

/* 8: the dictionary's words of each length, 2^NDBITS of them */
static const UBYTE nd_bits[25] = {
    0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10, 9, 9, 8, 7, 7, 8, 7, 7, 6, 6, 5, 5
};
static const ULONG nd_offset[25] = {
    0, 0, 0, 0, 0, 4096, 9216, 21504, 35840, 44032, 53248, 63488, 74752, 87040, 93696,
    100864, 104704, 106752, 108928, 113536, 115968, 118528, 119872, 121280, 122016
};

/* 7.1: the context of a literal in UTF8 mode is lut0[p1] | lut1[p2], in
 * signed mode (lut2[p1] << 3) | lut2[p2] */
static const UBYTE lut0[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  4,  0,  0,  4,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     8, 12, 16, 12, 12, 20, 12, 16, 24, 28, 12, 12, 32, 12, 36, 12,
    44, 44, 44, 44, 44, 44, 44, 44, 44, 44, 32, 32, 24, 40, 28, 12,
    12, 48, 52, 52, 52, 48, 52, 52, 52, 48, 52, 52, 52, 52, 52, 48,
    52, 52, 52, 52, 52, 48, 52, 52, 52, 52, 52, 24, 12, 28, 12, 12,
    12, 56, 60, 60, 60, 56, 60, 60, 60, 56, 60, 60, 60, 60, 60, 56,
    60, 60, 60, 60, 60, 56, 60, 60, 60, 60, 60, 24, 12, 28, 12,  0,
     0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,
     0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,
     0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,
     0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,
     2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,
     2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,
     2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,
     2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,  2,  3,
};
static const UBYTE lut1[256] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,
     1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,
     1,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  1,  1,  1,  1,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
};
static const UBYTE lut2[256] = {
     0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
     6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,
};

/* ---- bits ------------------------------------------------------------------ */

/* The next 25 bits or more, the first in the lowest; zeros past the input. */
static ULONG peek(const struct br_state *s)
{
    ULONG b = s->bpos >> 3, v = 0, i;
    for (i = 0; i < 4 && b + i < s->ilen; i++)
        v |= (ULONG)s->ib[b + i] << (8 * i);
    return v >> (s->bpos & 7);
}

static void drop(struct br_state *s, ULONG n)
{
    s->bpos += n;
    if (s->bpos > s->ilen * 8)
        s->shortf = 1;
}

/* n bits, up to 24 */
static ULONG bits(struct br_state *s, ULONG n)
{
    ULONG v;
    if (!n)
        return 0;
    v = peek(s) & ((1UL << n) - 1);
    drop(s, n);
    return v;
}

/* 9.2: a number of 0 to 255 */
static ULONG varlen8(struct br_state *s)
{
    ULONG n;
    if (!bits(s, 1))
        return 0;
    n = bits(s, 3);
    return n ? (1UL << n) + bits(s, n) : 1;
}

/* ---- prefix codes ------------------------------------------------------------ */

/* Canonical codes from the lengths (3.2). 0, or -1 when the lengths
 * over-subscribe the code. */
static int build(struct tree *t, const UBYTE *lens, ULONG n)
{
    UWORD offs[16], next[16];
    LONG left = 1;
    ULONG i;
    t->single = NOSINGLE;
    for (i = 0; i < 16; i++)
        t->count[i] = 0;
    for (i = 0; i < n; i++)
        t->count[lens[i]]++;
    t->count[0] = 0;
    for (i = 1; i < 16; i++) {
        left = (left << 1) - t->count[i];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (i = 1; i < 15; i++)
        offs[i + 1] = (UWORD)(offs[i] + t->count[i]);
    next[1] = 0;
    for (i = 2; i < 16; i++)
        next[i] = (UWORD)((next[i - 1] + t->count[i - 1]) << 1);
    for (i = 0; i < FASTSIZE; i++)
        t->fast[i] = 0;
    for (i = 0; i < n; i++) {
        ULONG l = lens[i];
        if (!l)
            continue;
        t->sym[offs[l]++] = (UWORD)i;
        if (l <= FASTBITS) {
            ULONG c = next[l], r = 0, k, j;
            for (k = 0; k < l; k++) {   /* the code reversed: its first bit lowest */
                r = (r << 1) | (c & 1);
                c >>= 1;
            }
            for (j = r; j < FASTSIZE; j += 1UL << l)
                t->fast[j] = (UWORD)((l << 11) | i);
        }
        next[l]++;
    }
    return 0;
}

static ULONG decode(struct br_state *s, const struct tree *t)
{
    ULONG v, e, len;
    LONG code = 0, first = 0, index = 0;
    if (t->single != NOSINGLE)
        return t->single;
    v = peek(s);
    e = t->fast[v & (FASTSIZE - 1)];
    if (e) {
        drop(s, e >> 11);
        return e & 0x7ff;
    }
    for (len = 1; len < 16; len++) {
        LONG count = t->count[len];
        code |= (LONG)((v >> (len - 1)) & 1);
        if (code - first < count) {
            drop(s, len);
            return t->sym[index + code - first];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    drop(s, 15);                       /* not a code: cannot happen with a complete code */
    s->shortf = 1;
    return 0;
}

/* 3.4 and 3.5: a prefix code over alpha symbols, into t. 0, or -1 when it
 * is not a valid code (or the input ran out: shortf). */
static int readcode(struct br_state *s, ULONG alpha, struct tree *t)
{
    UBYTE *lens = s->lens, cl[18];
    ULONG hskip = bits(s, 2), i;
    for (i = 0; i < alpha; i++)
        lens[i] = 0;
    if (hskip == 1) {
        ULONG nsym = bits(s, 2) + 1, abits = 0, sy[4], j;
        while ((1UL << abits) < alpha)
            abits++;
        for (i = 0; i < nsym; i++) {
            sy[i] = bits(s, abits);
            if (sy[i] >= alpha)
                return -1;
            for (j = 0; j < i; j++)
                if (sy[j] == sy[i])
                    return -1;
        }
        if (nsym == 1) {
            build(t, lens, 0);
            t->single = (UWORD)sy[0];
            return 0;
        }
        if (nsym == 2)
            lens[sy[0]] = lens[sy[1]] = 1;
        else if (nsym == 3) {
            lens[sy[0]] = 1;
            lens[sy[1]] = lens[sy[2]] = 2;
        } else if (bits(s, 1)) {
            lens[sy[0]] = 1;
            lens[sy[1]] = 2;
            lens[sy[2]] = lens[sy[3]] = 3;
        } else
            lens[sy[0]] = lens[sy[1]] = lens[sy[2]] = lens[sy[3]] = 2;
        return build(t, lens, alpha);
    }
    {
        LONG space = 32, sp = 32768;
        ULONG num = 0, one = 0, sym = 0, prev = 8, rep = 0, replen = 0;
        for (i = 0; i < 18; i++)
            cl[i] = 0;
        for (i = hskip; i < 18; i++) {
            ULONG v = peek(s) & 15, len = clcl_val[v];
            drop(s, clcl_len[v]);
            cl[cl_order[i]] = (UBYTE)len;
            if (len) {
                space -= 32L >> len;
                num++;
                one = cl_order[i];
                if (space <= 0)
                    break;
            }
        }
        if (num != 1 && space != 0)
            return -1;
        if (num == 1) {
            build(&s->tcl, cl, 0);
            s->tcl.single = (UWORD)one;
        } else if (build(&s->tcl, cl, 18))
            return -1;
        while (sym < alpha && sp > 0) {
            ULONG c = decode(s, &s->tcl);
            if (s->shortf)
                return -1;
            if (c < 16) {
                rep = 0;
                lens[sym++] = (UBYTE)c;
                if (c) {
                    prev = c;
                    sp -= 32768L >> c;
                }
            } else {
                ULONG eb = c == 16 ? 2 : 3, nl = c == 16 ? prev : 0, old, delta;
                if (replen != nl) {
                    rep = 0;
                    replen = nl;
                }
                old = rep;
                if (rep) {
                    rep -= 2;
                    rep <<= eb;
                }
                rep += bits(s, eb) + 3;
                delta = rep - old;
                if (delta > alpha - sym)
                    return -1;
                for (i = 0; i < delta; i++)
                    lens[sym++] = (UBYTE)replen;
                if (replen)
                    sp -= (LONG)(delta << (15 - replen));
            }
        }
        if (sp != 0)
            return -1;
        return build(t, lens, alpha);
    }
}

static ULONG blockcount(struct br_state *s, ULONG k)
{
    ULONG c = decode(s, &s->tc[k]);
    if (c > 25)
        c = 25;
    return bc_base[c] + bits(s, bc_extra[c]);
}

/* ---- memory and output ------------------------------------------------------- */

static void *get(struct br_state *s, ULONG n)
{
    void *p = s->env->alloc(s->env, n ? n : 1);
    if (!p)
        s->err = SDT_ERR_MEMORY;
    return p;
}

static void let(struct br_state *s, void *p)
{
    if (p)
        s->env->free(s->env, p);
}

static void free_block(struct br_state *s)
{
    let(s, s->cmapl);
    let(s, s->cmapd);
    let(s, s->gl.t);
    let(s, s->gi.t);
    let(s, s->gd.t);
    s->cmapl = s->cmapd = 0;
    s->gl.t = s->gi.t = s->gd.t = 0;
}

static int group(struct br_state *s, struct group *g, ULONG n, ULONG alpha)
{
    ULONG i;
    UWORD *pool;
    g->n = n;
    g->t = get(s, n * sizeof(struct tree) + n * (FASTSIZE + alpha) * 2);
    if (!g->t)
        return -1;
    pool = (UWORD *)(g->t + n);
    for (i = 0; i < n; i++) {
        g->t[i].fast = pool;
        pool += FASTSIZE;
        g->t[i].sym = pool;
        pool += alpha;
    }
    return 0;
}

/* One plain byte, to the caller and the window. The caller has checked
 * the room. 0, or -1 when the window cannot grow. */
static int put(struct br_state *s, UBYTE b)
{
    if (s->rpos == s->rsize) {
        if (s->rsize < s->rmax) {
            ULONG n = s->rsize ? s->rsize << 1 : (FIRST_RING < s->rmax ? FIRST_RING : s->rmax), i;
            UBYTE *r = get(s, n);
            if (!r)
                return -1;
            for (i = 0; i < s->rsize; i++)
                r[i] = s->ring[i];
            let(s, s->ring);
            s->ring = r;
            s->rsize = n;
        } else
            s->rpos = 0;
    }
    s->ring[s->rpos++] = b;
    *s->out++ = b;
    s->room--;
    s->made++;
    s->total++;
    if (s->total >= s->window)
        s->full = 1;
    s->p2 = s->p1;
    s->p1 = b;
    return 0;
}

static void end_block(struct br_state *s)
{
    free_block(s);
    s->state = s->islast ? S_DONE : S_MBHEAD;
}

/* ---- the steps ------------------------------------------------------------------ */

#define BAD() return s->shortf ? R_NEED : R_ERR
#define NEED_CHECK() do { if (s->shortf) return R_NEED; } while (0)

/* 9.1: WBITS */
static LONG step_head(struct br_state *s)
{
    ULONG w = 16;
    if (bits(s, 1)) {
        ULONG n = bits(s, 3);
        if (n)
            w = 17 + n;
        else {
            n = bits(s, 3);
            if (n == 1)
                BAD();                 /* large windows are not RFC 7932 */
            w = n ? 8 + n : 17;
        }
    }
    NEED_CHECK();
    s->rmax = 1UL << w;
    s->window = s->rmax - 16;
    s->state = S_MBHEAD;
    return R_OK;
}

/* 9.2: a meta-block header */
static LONG step_mbhead(struct br_state *s)
{
    ULONG islast = bits(s, 1), code, v, unc = 0;
    if (islast && bits(s, 1)) {
        NEED_CHECK();
        s->state = S_DONE;             /* ISLASTEMPTY */
        return R_OK;
    }
    code = bits(s, 2);
    if (code == 3) {                   /* metadata */
        ULONG nb, len = 0, i;
        if (bits(s, 1))
            BAD();
        nb = bits(s, 2);
        for (i = 0; i < nb; i++) {
            ULONG b = bits(s, 8);
            if (i + 1 == nb && nb > 1 && !b)
                BAD();
            len |= b << (8 * i);
        }
        if (nb)
            len++;
        if (bits(s, (8 - (s->bpos & 7)) & 7))
            BAD();
        NEED_CHECK();
        s->islast = (UBYTE)islast;
        s->mlen = len;
        s->state = S_METADATA;
        return R_OK;
    }
    v = bits(s, 4 * (code + 4));
    if (code > 0 && !(v >> (4 * (code + 3))))
        BAD();                         /* a last nibble of 0 in more than four */
    if (!islast)
        unc = bits(s, 1);
    if (unc && bits(s, (8 - (s->bpos & 7)) & 7))
        BAD();
    NEED_CHECK();
    s->islast = (UBYTE)islast;
    s->mlen = v + 1;
    s->bt_k = 0;
    s->state = unc ? S_UNCOMP : S_BT;
    return R_OK;
}

static LONG step_metadata(struct br_state *s)
{
    ULONG have = s->ilen - (s->bpos >> 3), n = s->mlen < have ? s->mlen : have;
    if (!s->mlen) {
        end_block(s);
        return R_OK;
    }
    if (!n)
        return R_NEED;
    s->bpos += 8 * n;
    s->mlen -= n;
    return R_OK;
}

static LONG step_uncomp(struct br_state *s)
{
    ULONG have = s->ilen - (s->bpos >> 3), n = s->mlen < have ? s->mlen : have, i;
    if (!s->mlen) {
        end_block(s);
        return R_OK;
    }
    if (!s->room)
        return R_FULL;
    if (!n)
        return R_NEED;
    if (n > s->room)
        n = s->room;
    for (i = 0; i < n; i++)
        if (put(s, s->ib[(s->bpos >> 3) + i]))
            return R_ERR;
    s->bpos += 8 * n;
    s->mlen -= n;
    return R_OK;
}

/* 9.2: one category's NBLTYPES, its two codes and first count */
static LONG step_bt(struct br_state *s)
{
    ULONG k = s->bt_k, n = varlen8(s) + 1, c = 0;
    if (n >= 2) {
        if (readcode(s, n + 2, &s->tt[k]) || readcode(s, 26, &s->tc[k]))
            BAD();
        c = blockcount(s, k);
    }
    NEED_CHECK();
    s->nbt[k] = n;
    s->btype[k] = 0;
    s->brb[k][0] = 1;
    s->brb[k][1] = 0;
    s->bcount[k] = c;
    if (++s->bt_k == 3)
        s->state = S_DP;
    return R_OK;
}

static void zero(UBYTE *p, ULONG n)
{
    while (n--)
        *p++ = 0;
}

/* NPOSTFIX, NDIRECT, the context modes and NTREESL */
static LONG step_dp(struct br_state *s)
{
    ULONG np = bits(s, 2), nd = bits(s, 4) << np, i, nt;
    for (i = 0; i < s->nbt[0]; i++)
        s->cmodes[i] = (UBYTE)bits(s, 2);
    nt = varlen8(s) + 1;
    NEED_CHECK();
    s->npostfix = np;
    s->ndirect = nd;
    s->ntl = nt;
    if (!(s->cmapl = get(s, 64 * s->nbt[0])))
        return R_ERR;
    if (nt >= 2) {
        s->cm = s->cmapl;
        s->cm_size = 64 * s->nbt[0];
        s->cm_ntrees = nt;
        s->cm_target = 0;
        s->state = S_CMHEAD;
    } else {
        zero(s->cmapl, 64 * s->nbt[0]);
        s->state = S_NTD;
    }
    return R_OK;
}

static LONG step_ntd(struct br_state *s)
{
    ULONG nt = varlen8(s) + 1;
    NEED_CHECK();
    s->ntd = nt;
    if (!(s->cmapd = get(s, 4 * s->nbt[2])))
        return R_ERR;
    if (nt >= 2) {
        s->cm = s->cmapd;
        s->cm_size = 4 * s->nbt[2];
        s->cm_ntrees = nt;
        s->cm_target = 1;
        s->state = S_CMHEAD;
    } else {
        zero(s->cmapd, 4 * s->nbt[2]);
        s->tree_i = 0;
        s->state = S_TREES;
    }
    return R_OK;
}

/* 7.3: a context map's RLEMAX and code */
static LONG step_cmhead(struct br_state *s)
{
    ULONG rle = bits(s, 1) ? bits(s, 4) + 1 : 0;
    if (readcode(s, s->cm_ntrees + rle, &s->tcm))
        BAD();
    NEED_CHECK();
    s->rlemax = rle;
    s->cm_i = 0;
    s->state = S_CMENT;
    return R_OK;
}

static LONG step_cment(struct br_state *s)
{
    ULONG sym, run = 1, v = 0;
    if (s->cm_i == s->cm_size) {
        s->state = S_CMMTF;
        return R_OK;
    }
    sym = decode(s, &s->tcm);
    if (sym && sym <= s->rlemax)
        run = (1UL << sym) + bits(s, sym);
    else if (sym)
        v = sym - s->rlemax;
    NEED_CHECK();
    if (run > s->cm_size - s->cm_i)
        return R_ERR;
    while (run--)
        s->cm[s->cm_i++] = (UBYTE)v;
    return R_OK;
}

static LONG step_cmmtf(struct br_state *s)
{
    ULONG imtf = bits(s, 1), i;
    NEED_CHECK();
    if (imtf) {                        /* 7.3: the inverse move-to-front transform */
        UBYTE mtf[256];
        for (i = 0; i < 256; i++)
            mtf[i] = (UBYTE)i;
        for (i = 0; i < s->cm_size; i++) {
            ULONG j = s->cm[i];
            UBYTE v = mtf[j];
            s->cm[i] = v;
            for (; j; j--)
                mtf[j] = mtf[j - 1];
            mtf[0] = v;
        }
    }
    for (i = 0; i < s->cm_size; i++)
        if (s->cm[i] >= s->cm_ntrees)
            return R_ERR;
    if (s->cm_target == 0)
        s->state = S_NTD;
    else {
        s->tree_i = 0;
        s->state = S_TREES;
    }
    return R_OK;
}

/* The meta-block's prefix codes, one a step */
static LONG step_trees(struct br_state *s)
{
    ULONG nl = s->ntl, ni = s->nbt[1], nd = s->ntd, j = s->tree_i;
    ULONG dalpha = 16 + s->ndirect + (48UL << s->npostfix);
    if (!s->gl.t) {
        if (group(s, &s->gl, nl, 256) || group(s, &s->gi, ni, 704) || group(s, &s->gd, nd, dalpha))
            return R_ERR;
    }
    if (j < nl) {
        if (readcode(s, 256, &s->gl.t[j]))
            BAD();
    } else if (j < nl + ni) {
        if (readcode(s, 704, &s->gi.t[j - nl]))
            BAD();
    } else if (readcode(s, dalpha, &s->gd.t[j - nl - ni]))
        BAD();
    NEED_CHECK();
    if (++s->tree_i == nl + ni + nd)
        s->state = S_CMD;
    return R_OK;
}

/* 6: a block switch command for category k */
static LONG step_switch(struct br_state *s, ULONG k)
{
    ULONG sym = decode(s, &s->tt[k]), t, c;
    c = blockcount(s, k);
    NEED_CHECK();
    if (sym == 0)
        t = s->brb[k][0];
    else if (sym == 1)
        t = s->brb[k][1] + 1;
    else
        t = sym - 2;
    if (t >= s->nbt[k])
        t -= s->nbt[k];
    s->brb[k][0] = s->brb[k][1];
    s->brb[k][1] = t;
    s->btype[k] = t;
    s->bcount[k] = c;
    return R_OK;
}

/* 5: an insert-and-copy command */
static LONG step_cmd(struct br_state *s)
{
    ULONG sym, cell, ic, cc, ins, cpy;
    if (s->nbt[1] >= 2 && !s->bcount[1])
        return step_switch(s, 1);
    sym = decode(s, &s->gi.t[s->btype[1]]);
    cell = sym >> 6;
    if (cell > 10)
        cell = 10;
    ic = cell_ins[cell] + ((sym >> 3) & 7);
    cc = cell_cpy[cell] + (sym & 7);
    ins = ins_base[ic] + bits(s, ins_extra[ic]);
    cpy = cpy_base[cc] + bits(s, cpy_extra[cc]);
    NEED_CHECK();
    if (ins > s->mlen)
        return R_ERR;
    s->bcount[1]--;
    s->insert = ins;
    s->copy = cpy;
    s->implicit = sym < 128;
    s->state = S_INSERT;
    return R_OK;
}

static LONG step_insert(struct br_state *s)
{
    ULONG ctx, lit, bt;
    UBYTE p1 = s->p1, p2 = s->p2;
    if (!s->insert) {
        if (!s->mlen)
            end_block(s);              /* the copy part is not used */
        else
            s->state = S_DIST;
        return R_OK;
    }
    if (!s->room)
        return R_FULL;
    if (s->nbt[0] >= 2 && !s->bcount[0])
        return step_switch(s, 0);
    bt = s->btype[0];
    switch (s->cmodes[bt]) {
    case 0: ctx = p1 & 0x3f; break;                         /* LSB6 */
    case 1: ctx = p1 >> 2; break;                           /* MSB6 */
    case 2: ctx = lut0[p1] | lut1[p2]; break;               /* UTF8 */
    default: ctx = ((ULONG)lut2[p1] << 3) | lut2[p2]; break; /* Signed */
    }
    lit = decode(s, &s->gl.t[s->cmapl[64 * bt + ctx]]);
    NEED_CHECK();
    s->bcount[0]--;
    if (put(s, (UBYTE)lit))
        return R_ERR;
    s->insert--;
    s->mlen--;
    return R_OK;
}

/* 8: a static dictionary word, transformed, into s->word */
static LONG dict_word(struct br_state *s, ULONG len, ULONG id)
{
    ULONG nb = nd_bits[len], idx = id & ((1UL << nb) - 1), t = id >> nb, kind, start = 0, n = len, i;
    const UBYTE *w, *p;
    UBYTE *o = s->word, *wb;
    if (t >= 121)
        return R_ERR;
    w = br_dict + nd_offset[len] + idx * len;
    kind = br_xf_kind[t];
    if (kind >= 1 && kind <= 9)
        n = n > kind ? n - kind : 0;
    else if (kind >= 12) {
        start = kind - 11;
        n = start < n ? n - start : 0;
    }
    p = br_xf_pool + br_xf_prefix[t];
    for (i = 0; i < p[0]; i++)
        *o++ = p[1 + i];
    wb = o;
    for (i = 0; i < n; i++)
        *o++ = w[start + i];
    if (kind == 10 || kind == 11) {    /* UppercaseFirst, UppercaseAll (Appendix B) */
        ULONG at = 0;
        while (at < n) {
            UBYTE *c = wb + at;
            ULONG step;
            if (c[0] < 0xc0) {
                if (c[0] >= 'a' && c[0] <= 'z')
                    c[0] ^= 32;
                step = 1;
            } else if (c[0] < 0xe0) {
                if (at + 1 < n)
                    c[1] ^= 32;
                step = 2;
            } else {
                if (at + 2 < n)
                    c[2] ^= 5;
                step = 3;
            }
            if (kind == 10)
                break;
            at += step;
        }
    }
    p = br_xf_pool + br_xf_suffix[t];
    for (i = 0; i < p[0]; i++)
        *o++ = p[1 + i];
    s->wlen = (ULONG)(o - s->word);
    s->wpos = 0;
    return R_OK;
}

static LONG step_dist(struct br_state *s)
{
    ULONG dcode = 0, d, maxd;
    if (s->implicit)
        d = s->last4[0];
    else {
        ULONG dctx = s->copy > 4 ? 3 : s->copy - 2;
        if (s->nbt[2] >= 2 && !s->bcount[2])
            return step_switch(s, 2);
        dcode = decode(s, &s->gd.t[s->cmapd[4 * s->btype[2] + dctx]]);
        if (dcode < 16) {
            LONG v = (LONG)s->last4[sd_index[dcode]] + sd_delta[dcode];
            NEED_CHECK();
            if (v <= 0)
                return R_ERR;
            d = (ULONG)v;
        } else if (dcode < 16 + s->ndirect)
            d = dcode - 15;
        else {
            ULONG x = dcode - s->ndirect - 16, np = s->npostfix;
            ULONG nbits = 1 + (x >> (np + 1)), hcode = x >> np, lcode = x & ((1UL << np) - 1);
            ULONG offset = ((2 + (hcode & 1)) << nbits) - 4;
            d = ((offset + bits(s, nbits)) << np) + lcode + s->ndirect + 1;
        }
        NEED_CHECK();
        s->bcount[2]--;
    }
    maxd = s->full ? s->window : s->total;
    if (d > maxd) {
        if (s->copy < 4 || s->copy > 24 || dict_word(s, s->copy, d - maxd - 1))
            return R_ERR;
        if (s->wlen > s->mlen)
            return R_ERR;
        s->mlen -= s->wlen;
        s->state = S_WORD;
        return R_OK;
    }
    if (s->copy > s->mlen)
        return R_ERR;
    if (dcode) {
        s->last4[3] = s->last4[2];
        s->last4[2] = s->last4[1];
        s->last4[1] = s->last4[0];
        s->last4[0] = d;
    }
    s->mlen -= s->copy;
    s->dist = d;
    s->state = S_COPY;
    return R_OK;
}

static LONG step_copy(struct br_state *s)
{
    while (s->copy) {
        if (!s->room)
            return R_FULL;
        if (put(s, s->ring[(s->rpos - s->dist) & (s->rsize - 1)]))
            return R_ERR;
        s->copy--;
    }
    if (s->mlen)
        s->state = S_CMD;
    else
        end_block(s);
    return R_OK;
}

static LONG step_word(struct br_state *s)
{
    while (s->wpos < s->wlen) {
        if (!s->room)
            return R_FULL;
        if (put(s, s->word[s->wpos]))
            return R_ERR;
        s->wpos++;
    }
    if (s->mlen)
        s->state = S_CMD;
    else
        end_block(s);
    return R_OK;
}

static LONG step(struct br_state *s)
{
    switch (s->state) {
    case S_HEAD: return step_head(s);
    case S_MBHEAD: return step_mbhead(s);
    case S_METADATA: return step_metadata(s);
    case S_UNCOMP: return step_uncomp(s);
    case S_BT: return step_bt(s);
    case S_DP: return step_dp(s);
    case S_NTD: return step_ntd(s);
    case S_CMHEAD: return step_cmhead(s);
    case S_CMENT: return step_cment(s);
    case S_CMMTF: return step_cmmtf(s);
    case S_TREES: return step_trees(s);
    case S_CMD: return step_cmd(s);
    case S_INSERT: return step_insert(s);
    case S_DIST: return step_dist(s);
    case S_COPY: return step_copy(s);
    case S_WORD: return step_word(s);
    default: return R_OK;
    }
}

/* ---- the interface --------------------------------------------------------------- */

struct br_state *br_new(struct sdt_env *env)
{
    struct br_state *s = env->alloc(env, sizeof(*s));
    UWORD *sym;
    ULONG i;
    if (!s)
        return 0;
    zero((UBYTE *)s, sizeof(*s) - IBSIZE);
    s->env = env;
    s->state = S_HEAD;
    s->last4[0] = 4;                   /* 4.: the last distances start as 4, 11, 15, 16 */
    s->last4[1] = 11;
    s->last4[2] = 15;
    s->last4[3] = 16;
    sym = s->fixsym;
    for (i = 0; i < 3; i++) {
        s->tt[i].fast = s->fixfast[i];
        s->tt[i].sym = sym;
        sym += 258;
        s->tc[i].fast = s->fixfast[3 + i];
        s->tc[i].sym = sym;
        sym += 26;
    }
    s->tcm.fast = s->fixfast[6];
    s->tcm.sym = sym;
    sym += 272;
    s->tcl.fast = s->fixfast[7];
    s->tcl.sym = sym;
    return s;
}

/* Moves the bytes before the step's start to the front, and copies in as
 * many of the caller's as fit. How many it took. */
static ULONG refill(struct br_state *s, struct sdt_call *c)
{
    ULONG drop = s->mark >> 3, n, i;
    if (drop) {
        for (i = drop; i < s->ilen; i++)
            s->ib[i - drop] = s->ib[i];
        s->ilen -= drop;
        s->bpos -= 8 * drop;
        s->mark -= 8 * drop;
    }
    n = c->in_len - c->in_used;
    if (n > IBSIZE - s->ilen)
        n = IBSIZE - s->ilen;
    for (i = 0; i < n; i++)
        s->ib[s->ilen + i] = c->in[c->in_used + i];
    s->ilen += n;
    c->in_used += n;
    return n;
}

LONG br_run(struct br_state *s, struct sdt_call *c)
{
    LONG r = R_OK;
    s->out = c->out;
    s->room = c->out_len;
    s->made = 0;
    s->err = 0;
    while (s->state != S_DONE) {
        s->mark = s->bpos;
        s->shortf = 0;
        r = step(s);
        if (r == R_OK)
            continue;
        if (r == R_NEED) {
            s->bpos = s->mark;
            if (refill(s, c))
                continue;
            if (c->in_used < c->in_len) {
                r = R_ERR;             /* a step larger than the buffer: not a valid stream */
                break;
            }
            break;
        }
        break;
    }
    c->out_made = s->made;
    if (s->state == S_DONE)
        return SDT_DONE;
    if (r == R_ERR) {
        c->error = s->err ? s->err : SDT_ERR_DATA;
        return SDT_ERROR;
    }
    return SDT_MORE;
}

void br_free(struct br_state *s)
{
    if (!s)
        return;
    free_block(s);
    let(s, s->ring);
    s->env->free(s->env, s);
}
