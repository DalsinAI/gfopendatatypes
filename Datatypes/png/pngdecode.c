/*
 * gfopenpng.datatype's decoder (pngdecode.h): the chunks, inflate, the five
 * row filters, Adam7 and the colour types, to 0xAARRGGBB.
 *
 * Inflate (RFC 1951) decodes Huffman codes of up to nine bits with one table
 * lookup and longer ones canonically a bit at a time; the bit buffer reads
 * ahead up to four bytes, which a stored block gives back. A stream that
 * ends early or goes wrong keeps the rows it reached (the rest stay clear),
 * as web browsers show a cut-off picture.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "pngdecode.h"

/* ---- inflate ------------------------------------------------------------------------------- */

#define FASTBITS 9
#define FASTSIZE (1 << FASTBITS)

typedef struct {
    UWORD fast[FASTSIZE];               /* the next nine bits, reversed: (length << 9) | symbol; 0 = longer */
    UWORD count[16];                    /* codes of each length */
    UWORD symbol[288];                  /* symbols, in canonical order */
} huff;

typedef struct {
    const UBYTE *in, *in_end;
    ULONG bitbuf;
    LONG bitcnt;
    LONG pad;                           /* bits of zeros put in past the end of the input */
    LONG bad;
    UBYTE *out;
    ULONG outpos, outmax;
} inf_state;

typedef struct {
    inf_state s;
    huff lencode, distcode;
    UBYTE lens[320];
} inf_work;

typedef char inf_work_fits[sizeof(inf_work) <= PNG_WORKSIZE ? 1 : -1];

static void fill(inf_state *s)
{
    while (s->bitcnt <= 24) {
        ULONG b;
        if (s->in < s->in_end)
            b = *s->in++;
        else {
            b = 0;
            s->pad += 8;
        }
        s->bitbuf |= b << s->bitcnt;
        s->bitcnt += 8;
    }
}

static void drop(inf_state *s, LONG n)
{
    s->bitbuf >>= n;
    s->bitcnt -= n;
    if (s->bitcnt < s->pad)
        s->bad = 1;                     /* used bits that were never there */
}

static LONG bits(inf_state *s, LONG n)
{
    ULONG v;
    if (!n)
        return 0;
    fill(s);
    v = s->bitbuf & ((1UL << n) - 1);
    drop(s, n);
    return (LONG)v;
}

/* 0, or -1 when the lengths don't make a code (too many codes of a length). */
static LONG build(huff *h, const UBYTE *len, LONG n)
{
    UWORD offs[16], next[16];
    LONG i, left = 1;
    for (i = 0; i < 16; i++)
        h->count[i] = 0;
    for (i = 0; i < n; i++)
        h->count[len[i]]++;
    h->count[0] = 0;
    for (i = 1; i < 16; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (i = 1; i < 15; i++)
        offs[i + 1] = (UWORD)(offs[i] + h->count[i]);
    next[1] = 0;                        /* the first code of each length (RFC 1951, 3.2.2) */
    for (i = 2; i < 16; i++)
        next[i] = (UWORD)((next[i - 1] + h->count[i - 1]) << 1);
    for (i = 0; i < FASTSIZE; i++)
        h->fast[i] = 0;
    for (i = 0; i < n; i++) {
        LONG l = len[i];
        if (!l)
            continue;
        h->symbol[offs[l]++] = (UWORD)i;
        if (l <= FASTBITS) {
            ULONG c = next[l], r = 0, k, j;
            for (k = 0; k < (ULONG)l; k++) {     /* the code, first bit lowest, as it arrives */
                r = (r << 1) | (c & 1);
                c >>= 1;
            }
            for (j = r; j < FASTSIZE; j += 1UL << l)
                h->fast[j] = (UWORD)((l << FASTBITS) | i);
        }
        next[l]++;
    }
    return 0;
}

static LONG decode(inf_state *s, const huff *h)
{
    LONG code = 0, first = 0, index = 0, len;
    UWORD e;
    fill(s);
    e = h->fast[s->bitbuf & (FASTSIZE - 1)];
    if (e) {
        drop(s, e >> FASTBITS);
        return s->bad ? -1 : (LONG)(e & (FASTSIZE - 1));
    }
    /* longer than nine bits (or no such code): canonically, a bit at a time */
    for (len = 1; len < 16; len++) {
        LONG count;
        code |= (LONG)(s->bitbuf & 1);
        drop(s, 1);
        if (s->bad)
            return -1;
        count = h->count[len];
        if (code - first < count)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const UWORD lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
                                 131, 163, 195, 227, 258 };
static const UBYTE lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const UWORD dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const UBYTE dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* One block's codes, to its end. 0; 1 when the output is full; -1 on a bad stream. */
static LONG codes(inf_state *s, const huff *lencode, const huff *distcode)
{
    UBYTE *out = s->out;
    for (;;) {
        LONG sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->outpos >= s->outmax)
                return 1;
            out[s->outpos++] = (UBYTE)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            ULONG len, dist, from;
            sym -= 257;
            if (sym >= 29)
                return -1;
            len = lbase[sym] + (ULONG)bits(s, lext[sym]);
            sym = decode(s, distcode);
            if (sym < 0 || sym >= 30)
                return -1;
            dist = dbase[sym] + (ULONG)bits(s, dext[sym]);
            if (s->bad || dist > s->outpos)
                return -1;
            if (s->outpos + len > s->outmax) {
                len = s->outmax - s->outpos;
                if (!len)
                    return 1;
            }
            from = s->outpos - dist;
            while (len--)
                out[s->outpos++] = out[from++];
        }
    }
}

static LONG stored(inf_state *s)
{
    ULONG len;
    drop(s, s->bitcnt & 7);             /* to the byte */
    while (s->bitcnt >= 8) {            /* give back the bytes read ahead */
        if (s->pad >= 8)
            s->pad -= 8;
        else
            s->in--;
        s->bitcnt -= 8;
    }
    s->bitbuf = 0;
    s->bitcnt = 0;
    s->pad = 0;
    if (s->in + 4 > s->in_end)
        return -1;
    len = s->in[0] | ((ULONG)s->in[1] << 8);
    if ((s->in[2] | ((ULONG)s->in[3] << 8)) != (~len & 0xffff))
        return -1;
    s->in += 4;
    if (s->in + len > s->in_end)
        return -1;
    while (len--) {
        if (s->outpos >= s->outmax)
            return 1;
        s->out[s->outpos++] = *s->in++;
    }
    return 0;
}

static LONG fixed(inf_work *w)
{
    LONG i;
    for (i = 0; i < 144; i++) w->lens[i] = 8;
    for (; i < 256; i++) w->lens[i] = 9;
    for (; i < 280; i++) w->lens[i] = 7;
    for (; i < 288; i++) w->lens[i] = 8;
    build(&w->lencode, w->lens, 288);
    for (i = 0; i < 30; i++) w->lens[i] = 5;
    build(&w->distcode, w->lens, 30);
    return codes(&w->s, &w->lencode, &w->distcode);
}

static LONG dynamic(inf_work *w)
{
    static const UBYTE order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    inf_state *s = &w->s;
    UBYTE *len = w->lens;
    LONG nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4, i;
    if (s->bad || nlen > 286 || ndist > 30)
        return -1;
    for (i = 0; i < 19; i++)
        len[order[i]] = (UBYTE)(i < ncode ? bits(s, 3) : 0);
    if (s->bad || build(&w->lencode, len, 19) < 0)
        return -1;
    for (i = 0; i < nlen + ndist;) {
        LONG sym = decode(s, &w->lencode), rep;
        UBYTE v = 0;
        if (sym < 0)
            return -1;
        if (sym < 16) {
            len[i++] = (UBYTE)sym;
            continue;
        }
        if (sym == 16) {
            if (!i)
                return -1;
            v = len[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17)
            rep = 3 + bits(s, 3);
        else
            rep = 11 + bits(s, 7);
        if (s->bad || i + rep > nlen + ndist)
            return -1;
        while (rep--)
            len[i++] = v;
    }
    if (!len[256])
        return -1;                      /* no end-of-block code */
    if (build(&w->lencode, len, nlen) < 0 || build(&w->distcode, len + nlen, ndist) < 0)
        return -1;
    return codes(s, &w->lencode, &w->distcode);
}

/* A zlib stream (RFC 1950) inflated into out: the bytes it gave, as many as
 * it reached before any fault. */
static ULONG zlib_inflate(inf_work *w, const UBYTE *in, ULONG inlen, UBYTE *out, ULONG outmax)
{
    inf_state *s = &w->s;
    LONG last, r;
    if (inlen < 2 || (in[0] & 15) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 || (in[1] & 0x20))
        return 0;
    s->in = in + 2;
    s->in_end = in + inlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    s->pad = 0;
    s->bad = 0;
    s->out = out;
    s->outpos = 0;
    s->outmax = outmax;
    do {
        LONG type;
        last = bits(s, 1);
        type = bits(s, 2);
        if (s->bad)
            break;
        r = type == 0 ? stored(s) : type == 1 ? fixed(w) : type == 2 ? dynamic(w) : -1;
        if (r)
            break;                      /* full (1), or a fault (-1): keep what came */
    } while (!last);
    return s->outpos;
}

/* ---- the chunks -------------------------------------------------------------------------------- */

#define CK_IHDR 0x49484452UL
#define CK_PLTE 0x504C5445UL
#define CK_tRNS 0x74524E53UL
#define CK_IDAT 0x49444154UL
#define CK_IEND 0x49454E44UL

static ULONG be32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static const UBYTE adam7[7][4] = {      /* x start, y start, x step, y step */
    { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

static ULONG channels(UBYTE ctype)
{
    return ctype == 2 ? 3 : ctype == 4 ? 2 : ctype == 6 ? 4 : 1;
}

static ULONG rowbytes(const struct png_head *h, ULONG w)
{
    return (w * channels(h->ctype) * h->depth + 7) >> 3;
}

/* A pass's size in pixels (the whole picture when not interlaced). */
static void pass_size(const struct png_head *h, LONG pass, ULONG *pw, ULONG *ph)
{
    if (!h->interlace) {
        *pw = h->width;
        *ph = h->height;
        return;
    }
    *pw = h->width > adam7[pass][0] ? (h->width - adam7[pass][0] + adam7[pass][2] - 1) / adam7[pass][2] : 0;
    *ph = h->height > adam7[pass][1] ? (h->height - adam7[pass][1] + adam7[pass][3] - 1) / adam7[pass][3] : 0;
    if (!*pw || !*ph)
        *pw = *ph = 0;
}

LONG png_read_head(const UBYTE *file, ULONG len, struct png_head *h)
{
    static const UBYTE sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    ULONG pos, i, pal_n = 0;
    LONG seen_idat = 0, passes, p;
    const UBYTE *d;
    UBYTE *z = (UBYTE *)h;
    for (i = 0; i < sizeof(*h); i++)
        z[i] = 0;
    if (len < 8 + 25)
        return -1;
    for (i = 0; i < 8; i++)
        if (file[i] != sig[i])
            return -1;
    if (be32(file + 8) != 13 || be32(file + 12) != CK_IHDR)
        return -1;                      /* IHDR comes first */
    d = file + 16;
    h->width = be32(d);
    h->height = be32(d + 4);
    h->depth = d[8];
    h->ctype = d[9];
    h->interlace = d[12];
    if (!h->width || !h->height || h->width > PNG_MAXSIDE || h->height > PNG_MAXSIDE
        || h->width * h->height > PNG_MAXPIXELS || d[10] || d[11] || h->interlace > 1)
        return -1;
    switch (h->ctype) {
    case 0: if (h->depth != 1 && h->depth != 2 && h->depth != 4 && h->depth != 8 && h->depth != 16) return -1; break;
    case 3: if (h->depth != 1 && h->depth != 2 && h->depth != 4 && h->depth != 8) return -1; break;
    case 2: case 4: case 6: if (h->depth != 8 && h->depth != 16) return -1; break;
    default: return -1;
    }
    if (h->ctype == 4 || h->ctype == 6)
        h->alpha = 1;
    for (i = 0; i < 256; i++)
        h->palette[i] = 0xff000000UL;
    for (pos = 8; pos + 12 <= len;) {
        ULONG n = be32(file + pos), type = be32(file + pos + 4);
        if (n > len - pos - 12)
            break;                      /* cut off: what came before still counts */
        d = file + pos + 8;
        if (type == CK_PLTE && !seen_idat) {
            pal_n = n / 3 > 256 ? 256 : n / 3;
            for (i = 0; i < pal_n; i++)
                h->palette[i] = 0xff000000UL | ((ULONG)d[i * 3] << 16) | ((ULONG)d[i * 3 + 1] << 8) | d[i * 3 + 2];
        } else if (type == CK_tRNS && !seen_idat) {
            if (h->ctype == 3) {
                for (i = 0; i < n && i < 256; i++)
                    h->palette[i] = (h->palette[i] & 0x00ffffffUL) | ((ULONG)d[i] << 24);
                h->alpha = 1;
            } else if (h->ctype == 0 && n >= 2) {
                h->key[0] = (UWORD)((d[0] << 8) | d[1]);
                h->has_key = h->alpha = 1;
            } else if (h->ctype == 2 && n >= 6) {
                h->key[0] = (UWORD)((d[0] << 8) | d[1]);
                h->key[1] = (UWORD)((d[2] << 8) | d[3]);
                h->key[2] = (UWORD)((d[4] << 8) | d[5]);
                h->has_key = h->alpha = 1;
            }
        } else if (type == CK_IDAT) {
            seen_idat = 1;
            if (h->idat_len + n < h->idat_len)
                return -1;
            h->idat_len += n;
        } else if (type == CK_IEND)
            break;
        pos += n + 12;
    }
    if (!h->idat_len)
        return -1;
    passes = h->interlace ? 7 : 1;
    for (p = 0; p < passes; p++) {
        ULONG pw, ph;
        pass_size(h, p, &pw, &ph);
        if (pw)
            h->raw_len += ph * (rowbytes(h, pw) + 1);
    }
    return 0;
}

void png_gather(const UBYTE *file, ULONG len, UBYTE *idat)
{
    ULONG pos, at = 0;
    for (pos = 8; pos + 12 <= len;) {
        ULONG n = be32(file + pos), type = be32(file + pos + 4), i;
        if (n > len - pos - 12)
            break;
        if (type == CK_IDAT)
            for (i = 0; i < n; i++)
                idat[at++] = file[pos + 8 + i];
        else if (type == CK_IEND)
            break;
        pos += n + 12;
    }
}

/* ---- the rows ------------------------------------------------------------------------------- */

static LONG paeth(LONG a, LONG b, LONG c)
{
    LONG p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Undoes one row's filter in place; prev is the row above in the same pass, or NULL. */
static void unfilter(UBYTE f, UBYTE *cur, const UBYTE *prev, ULONG n, ULONG bpp)
{
    ULONG i;
    switch (f) {
    case 1:
        for (i = bpp; i < n; i++)
            cur[i] = (UBYTE)(cur[i] + cur[i - bpp]);
        break;
    case 2:
        if (prev)
            for (i = 0; i < n; i++)
                cur[i] = (UBYTE)(cur[i] + prev[i]);
        break;
    case 3:
        for (i = 0; i < n; i++) {
            ULONG a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0;
            cur[i] = (UBYTE)(cur[i] + ((a + b) >> 1));
        }
        break;
    case 4:
        for (i = 0; i < n; i++) {
            LONG a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            cur[i] = (UBYTE)(cur[i] + paeth(a, b, c));
        }
        break;
    default:                            /* 0, and an unknown filter: the bytes as they are */
        break;
    }
}

/* One row of n pixels to ARGB, each step words after the last. */
static void convert(const struct png_head *h, const UBYTE *s, ULONG n, ULONG *d, ULONG step)
{
    ULONG x, depth = h->depth;
    switch (h->ctype) {
    case 0:
        if (depth < 8) {
            ULONG per = 8 / depth, max = (1UL << depth) - 1, mul = 255 / max;
            for (x = 0; x < n; x++, d += step) {
                ULONG v = (s[x / per] >> ((per - 1 - x % per) * depth)) & max, g = v * mul;
                *d = ((h->has_key && v == h->key[0]) ? 0 : 0xff000000UL) | (g << 16) | (g << 8) | g;
            }
        } else if (depth == 8) {
            for (x = 0; x < n; x++, d += step) {
                ULONG g = s[x];
                *d = ((h->has_key && g == h->key[0]) ? 0 : 0xff000000UL) | (g << 16) | (g << 8) | g;
            }
        } else {
            for (x = 0; x < n; x++, d += step) {
                ULONG g = s[x * 2], v = (g << 8) | s[x * 2 + 1];
                *d = ((h->has_key && v == h->key[0]) ? 0 : 0xff000000UL) | (g << 16) | (g << 8) | g;
            }
        }
        break;
    case 3:
        if (depth < 8) {
            ULONG per = 8 / depth, max = (1UL << depth) - 1;
            for (x = 0; x < n; x++, d += step)
                *d = h->palette[(s[x / per] >> ((per - 1 - x % per) * depth)) & max];
        } else
            for (x = 0; x < n; x++, d += step)
                *d = h->palette[s[x]];
        break;
    case 2:
        if (depth == 8) {
            if (h->has_key)
                for (x = 0; x < n; x++, s += 3, d += step)
                    *d = ((s[0] == h->key[0] && s[1] == h->key[1] && s[2] == h->key[2]) ? 0 : 0xff000000UL)
                         | ((ULONG)s[0] << 16) | ((ULONG)s[1] << 8) | s[2];
            else
                for (x = 0; x < n; x++, s += 3, d += step)
                    *d = 0xff000000UL | ((ULONG)s[0] << 16) | ((ULONG)s[1] << 8) | s[2];
        } else {
            for (x = 0; x < n; x++, s += 6, d += step) {
                ULONG key = h->has_key && ((s[0] << 8) | s[1]) == h->key[0] && ((s[2] << 8) | s[3]) == h->key[1]
                            && ((s[4] << 8) | s[5]) == h->key[2];
                *d = (key ? 0 : 0xff000000UL) | ((ULONG)s[0] << 16) | ((ULONG)s[2] << 8) | s[4];
            }
        }
        break;
    case 4:
        if (depth == 8)
            for (x = 0; x < n; x++, s += 2, d += step)
                *d = ((ULONG)s[1] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[0] << 8) | s[0];
        else
            for (x = 0; x < n; x++, s += 4, d += step)
                *d = ((ULONG)s[2] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[0] << 8) | s[0];
        break;
    default:                            /* 6 */
        if (depth == 8)
            for (x = 0; x < n; x++, s += 4, d += step)
                *d = ((ULONG)s[3] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[1] << 8) | s[2];
        else
            for (x = 0; x < n; x++, s += 8, d += step)
                *d = ((ULONG)s[6] << 24) | ((ULONG)s[0] << 16) | ((ULONG)s[2] << 8) | s[4];
        break;
    }
}

LONG png_decode(const struct png_head *h, const UBYTE *idat, UBYTE *raw, ULONG *argb, APTR work)
{
    ULONG got, at = 0, i, bpp = (channels(h->ctype) * h->depth + 7) >> 3;
    LONG pass, passes = h->interlace ? 7 : 1;
    got = zlib_inflate((inf_work *)work, idat, h->idat_len, raw, h->raw_len);
    if (!got)
        return -1;
    for (i = got; i < h->raw_len; i++)
        raw[i] = 0;                     /* cut short: the rest stays clear */
    for (i = 0; i < h->width * h->height; i++)
        argb[i] = 0;
    for (pass = 0; pass < passes; pass++) {
        ULONG pw, ph, rb, y;
        const UBYTE *prev = 0;
        pass_size(h, pass, &pw, &ph);
        if (!pw)
            continue;
        rb = rowbytes(h, pw);
        for (y = 0; y < ph; y++) {
            UBYTE *row = raw + at;
            if (at >= got)
                return 0;               /* no more rows came */
            unfilter(row[0], row + 1, prev, rb, bpp);
            if (h->interlace) {
                ULONG ty = adam7[pass][1] + y * adam7[pass][3];
                convert(h, row + 1, pw, argb + ty * h->width + adam7[pass][0], adam7[pass][2]);
            } else
                convert(h, row + 1, pw, argb + y * h->width, 1);
            prev = row + 1;
            at += rb + 1;
        }
    }
    return 0;
}
