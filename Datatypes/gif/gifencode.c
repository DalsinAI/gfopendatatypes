/*
 * gfopengif.datatype's encoder (gifencode.h).
 *
 * The file: "GIF89a"; the logical screen descriptor (the picture's size, a
 * global table of 2 to 256 entries, background 0, no aspect ratio); the
 * table; a graphic control extension only when there is a transparent
 * colour (no delay, disposal unspecified); the image descriptor (at 0,0,
 * the whole screen, no local table, not interlaced); the LZW data in
 * sub-blocks of up to 255 bytes and the empty one that ends them; and the
 * trailer, 0x3B.
 *
 * The LZW: codes from the minimum code size plus one bit up to 12 bits,
 * least significant bit first; a clear code first, and again whenever the
 * table is full (4096 codes) and the next string needs a new one; the end
 * of information code last. The code size grows as GIF decoders expect it
 * ("early change"): a code is written at the larger size from the code
 * after the one whose new entry reached 2 to the power of the size, and
 * the end code is written at the size the decoder will have reached after
 * the last code, which adds one more entry on its side.
 *
 * Median cut (gif_encode() with more than 256 colours): the pixels' colours
 * counted in 32x32x32 boxes; the box whose longest side times its pixels is
 * the largest is cut across that side where half its pixels fall, until
 * there are 256 (255 with a transparent colour) or no box can be cut; each
 * box's colour is its pixels' average, and each pixel takes its box's.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include "gifencode.h"

#define HSIZE 5003UL                   /* the LZW hash table: a prime, over 4096 */
#define MAXCODE 4096UL

/* ---------------------------------------------------------------- writing */

struct out {
    UBYTE *p;                          /* the file so far */
    UBYTE *block;                      /* the sub-block being filled (its count byte) */
    ULONG acc;                         /* bits not yet written, least significant first */
    ULONG nacc;
};

static void put16(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)v;
    p[1] = (UBYTE)(v >> 8);
}

static void put_byte(struct out *o, UBYTE b)
{
    if (!o->block) {                   /* a new sub-block */
        o->block = o->p++;
        *o->block = 0;
    }
    *o->p++ = b;
    if (++*o->block == 255)
        o->block = 0;
}

static void put_code(struct out *o, ULONG code, ULONG bits)
{
    o->acc |= code << o->nacc;
    o->nacc += bits;
    while (o->nacc >= 8) {
        put_byte(o, (UBYTE)o->acc);
        o->acc >>= 8;
        o->nacc -= 8;
    }
}

static void put_end(struct out *o)
{
    if (o->nacc)
        put_byte(o, (UBYTE)o->acc);
    o->acc = o->nacc = 0;
    *o->p++ = 0;                       /* the empty sub-block */
    o->block = 0;
}

/* The pixels as LZW, in sub-blocks; work is HSIZE longwords and HSIZE words. */
static void lzw(struct out *o, const UBYTE *index, ULONG width, ULONG height, ULONG stride, ULONG mcs,
                ULONG *keys, UWORD *codes)
{
    const ULONG clear = 1UL << mcs, eoi = clear + 1;
    ULONG cs = mcs + 1, next = eoi + 1, prefix = 0, x, y, i;
    int started = 0;

    for (i = 0; i < HSIZE; i++)
        keys[i] = 0;
    put_code(o, clear, cs);
    for (y = 0; y < height; y++) {
        const UBYTE *row = index + y * stride;
        for (x = 0; x < width; x++) {
            ULONG c = row[x], key, h;
            if (!started) {
                prefix = c;
                started = 1;
                continue;
            }
            key = ((prefix << 8) | c) + 1;               /* 1 to 2^20: 0 is an empty slot */
            h = ((c << 12) ^ prefix) % HSIZE;
            while (keys[h] && keys[h] != key)
                if (++h == HSIZE)
                    h = 0;
            if (keys[h]) {                               /* the string goes on */
                prefix = codes[h];
                continue;
            }
            put_code(o, prefix, cs);
            if (next < MAXCODE) {
                keys[h] = key;
                codes[h] = (UWORD)next;
                if (next >= (1UL << cs) && cs < 12)
                    cs++;
                next++;
            } else {                                     /* the table is full: start again */
                put_code(o, clear, cs);
                for (i = 0; i < HSIZE; i++)
                    keys[i] = 0;
                cs = mcs + 1;
                next = eoi + 1;
            }
            prefix = c;
        }
    }
    put_code(o, prefix, cs);
    /* The decoder adds an entry for that last code: its size may grow with it. */
    if (next < MAXCODE && next >= (1UL << cs) && cs < 12)
        cs++;
    put_code(o, eoi, cs);
    put_end(o);
}

/* The most the file can take: one code a pixel, at 12 bits, with the clear
 * codes, the end code and a count byte for each 255 bytes. 0 when that is
 * past what 32 bits can count. */
static ULONG capacity(ULONG width, ULONG height)
{
    ULONG pixels, bytes;
    if (width > 65535UL || height > 65535UL || width > 0x0FFFFFFFUL / height)
        return 0;
    pixels = width * height;
    bytes = (pixels + pixels / 2048 + 8) / 2 * 3 + 3;
    return 13 + 768 + 8 + 10 + 1 + bytes + bytes / 255 + 2 + 1 + 1 + 16;
}

static LONG write_gif(struct gife_env *env, const UBYTE *index, ULONG width, ULONG height, ULONG stride,
                      const UBYTE *rgb, ULONG colours, LONG transparent, UBYTE **gif, ULONG *size)
{
    ULONG bits = 1, cap, i, *keys;
    UWORD *codes;
    UBYTE *buf;
    struct out o;

    while ((1UL << bits) < colours)
        bits++;
    if (!(cap = capacity(width, height)))
        return GIFE_ERR_ARGS;
    if (!(keys = env->alloc(env, HSIZE * 4 + HSIZE * 2)))
        return GIFE_ERR_MEMORY;
    codes = (UWORD *)(keys + HSIZE);
    if (!(buf = env->alloc(env, cap))) {
        env->free(env, keys);
        return GIFE_ERR_MEMORY;
    }

    o.p = buf;
    for (i = 0; i < 6; i++)
        *o.p++ = (UBYTE)"GIF89a"[i];
    put16(o.p, width);
    put16(o.p + 2, height);
    o.p += 4;
    *o.p++ = (UBYTE)(0x80 | (7 << 4) | (bits - 1));     /* a global table, 8 bits a primary */
    *o.p++ = 0;                                         /* background colour */
    *o.p++ = 0;                                         /* no aspect ratio */
    for (i = 0; i < (1UL << bits) * 3; i++)
        *o.p++ = i < colours * 3 ? rgb[i] : 0;
    if (transparent >= 0) {                             /* graphic control extension */
        *o.p++ = 0x21;
        *o.p++ = 0xF9;
        *o.p++ = 4;
        *o.p++ = 1;                                     /* a transparent colour */
        *o.p++ = 0;
        *o.p++ = 0;                                     /* no delay */
        *o.p++ = (UBYTE)transparent;
        *o.p++ = 0;
    }
    *o.p++ = 0x2C;                                      /* image descriptor */
    put16(o.p, 0);
    put16(o.p + 2, 0);
    put16(o.p + 4, width);
    put16(o.p + 6, height);
    o.p += 8;
    *o.p++ = 0;                                         /* no local table, not interlaced */
    *o.p++ = (UBYTE)(bits < 2 ? 2 : bits);              /* LZW minimum code size */
    o.block = 0;
    o.acc = o.nacc = 0;
    lzw(&o, index, width, height, stride, bits < 2 ? 2 : bits, keys, codes);
    *o.p++ = 0x3B;
    env->free(env, keys);
    *gif = buf;
    *size = (ULONG)(o.p - buf);
    return 0;
}

LONG gif_encode_lut8(struct gife_env *env, const UBYTE *index, ULONG width, ULONG height, ULONG stride,
                     const UBYTE *rgb, ULONG colours, LONG transparent, UBYTE **gif, ULONG *size)
{
    ULONG x, y;
    *gif = 0;
    *size = 0;
    if (!index || !rgb || !width || !height || width > 65535UL || height > 65535UL || stride < width
        || !colours || colours > 256 || transparent >= (LONG)colours || transparent < -1)
        return GIFE_ERR_ARGS;
    for (y = 0; y < height; y++)                        /* an index past the table is refused */
        for (x = 0; x < width; x++)
            if (index[y * stride + x] >= colours)
                return GIFE_ERR_ARGS;
    return write_gif(env, index, width, height, stride, rgb, colours, transparent, gif, size);
}


/* ----------------------------------------------------- colours to indexes */

#define TABLE 1024UL                   /* the exact table's hash: four slots a colour or more */

/* Each pixel's index, when the opaque pixels have no more than max - first
 * colours: those go in rgb from entry first on (entry 0 is the transparent
 * one when first is 1). The number of entries, or 0 when there are more. */
static ULONG exact(const UBYTE *argb, ULONG w, ULONG h, ULONG stride, ULONG first, ULONG max,
                   UBYTE *idx, UBYTE *rgb, ULONG *slot, UBYTE *slotidx)
{
    ULONG n = first, x, y, i, last = 0xFFFFFFFFUL;
    UBYTE lastidx = 0;
    for (i = 0; i < TABLE; i++)
        slot[i] = 0;
    for (y = 0; y < h; y++) {
        const UBYTE *row = argb + y * stride;
        for (x = 0; x < w; x++) {
            const UBYTE *p = row + 4 * x;
            ULONG c, k;
            if (first && p[0] < 0x80) {
                *idx++ = 0;
                continue;
            }
            c = ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3] | 0x01000000UL;  /* 0: an empty slot */
            if (c != last) {
                k = ((c * 2654435761UL) & 0xFFFFFFFFUL) >> 22;
                while (slot[k] && slot[k] != c)
                    k = (k + 1) & (TABLE - 1);
                if (!slot[k]) {
                    if (n == max)
                        return 0;
                    slot[k] = c;
                    slotidx[k] = (UBYTE)n;
                    rgb[3 * n] = p[1];
                    rgb[3 * n + 1] = p[2];
                    rgb[3 * n + 2] = p[3];
                    n++;
                }
                last = c;
                lastidx = slotidx[k];
            }
            *idx++ = lastidx;
        }
    }
    return n ? n : 1;
}

struct box {
    ULONG start, n;                    /* its bins in the list */
    ULONG pixels;
    UBYTE lo[3], hi[3];
    UBYTE pad[2];
};

#define BIN(p) (((ULONG)((p)[1] >> 3) << 10) | ((ULONG)((p)[2] >> 3) << 5) | (ULONG)((p)[3] >> 3))

static ULONG coord(ULONG bin, int axis)
{
    return (bin >> (10 - 5 * axis)) & 31;
}

static void measure(struct box *b, const UWORD *list, const ULONG *hist)
{
    ULONG i;
    int a;
    b->pixels = 0;
    for (a = 0; a < 3; a++) {
        b->lo[a] = 31;
        b->hi[a] = 0;
    }
    for (i = b->start; i < b->start + b->n; i++) {
        b->pixels += hist[list[i]];
        for (a = 0; a < 3; a++) {
            ULONG v = coord(list[i], a);
            if (v < b->lo[a]) b->lo[a] = (UBYTE)v;
            if (v > b->hi[a]) b->hi[a] = (UBYTE)v;
        }
    }
}

/* Median cut: each pixel's index, the table from entry first on. The
 * number of entries. hist is 32768 longwords, list 32768 words, box max
 * boxes. */
static ULONG median_cut(const UBYTE *argb, ULONG w, ULONG h, ULONG stride, ULONG first, ULONG max,
                        UBYTE *idx, UBYTE *rgb, ULONG *hist, UWORD *list, struct box *box)
{
    ULONG x, y, i, nbins = 0, nbox = 1, want = max - first;

    for (i = 0; i < 32768; i++)
        hist[i] = 0;
    for (y = 0; y < h; y++) {
        const UBYTE *row = argb + y * stride;
        for (x = 0; x < w; x++) {
            const UBYTE *p = row + 4 * x;
            if (!(first && p[0] < 0x80))
                hist[BIN(p)]++;
        }
    }
    for (i = 0; i < 32768; i++)
        if (hist[i])
            list[nbins++] = (UWORD)i;
    box[0].start = 0;
    box[0].n = nbins;
    measure(&box[0], list, hist);

    while (nbox < want) {
        ULONG best = 0, score = 0, acc = 0, cut, lo, hi, v[32];
        struct box *b;
        int axis = 0, a;
        for (i = 0; i < nbox; i++) {                     /* the box to cut */
            ULONG side = 0, s;
            if (box[i].n < 2)
                continue;
            for (a = 0; a < 3; a++)
                if ((ULONG)(box[i].hi[a] - box[i].lo[a]) > side)
                    side = (ULONG)(box[i].hi[a] - box[i].lo[a]);
            s = side * ((box[i].pixels >> 8) + 1);
            if (s > score) {
                score = s;
                best = i;
            }
        }
        if (!score)
            break;                                       /* every box is one bin */
        b = &box[best];
        for (a = 1; a < 3; a++)
            if (b->hi[a] - b->lo[a] > b->hi[axis] - b->lo[axis])
                axis = a;
        for (i = 0; i < 32; i++)
            v[i] = 0;
        for (i = b->start; i < b->start + b->n; i++)
            v[coord(list[i], axis)] += hist[list[i]];
        /* where half the pixels fall: lo <= cut < hi, so both halves have bins */
        for (cut = b->lo[axis]; cut < (ULONG)b->hi[axis] - 1; cut++) {
            acc += v[cut];
            if (acc >= b->pixels - acc)
                break;
        }
        lo = b->start;
        hi = b->start + b->n;
        while (lo < hi) {                                /* bins at or below the cut first */
            if (coord(list[lo], axis) <= cut)
                lo++;
            else {
                UWORD t = list[lo];
                list[lo] = list[--hi];
                list[hi] = t;
            }
        }
        box[nbox].start = lo;
        box[nbox].n = b->start + b->n - lo;
        b->n = lo - b->start;
        measure(b, list, hist);
        measure(&box[nbox], list, hist);
        nbox++;
    }

    /* Each box's colour, the average of its pixels; then hist says each bin's entry. */
    for (i = 0; i < nbox; i++) {
        unsigned long long sum[3] = { 0, 0, 0 };
        ULONG j, e = first + i;
        int a;
        for (j = box[i].start; j < box[i].start + box[i].n; j++)
            for (a = 0; a < 3; a++) {
                ULONG c5 = coord(list[j], a);
                sum[a] += (unsigned long long)((c5 << 3) | (c5 >> 2)) * hist[list[j]];
            }
        for (a = 0; a < 3; a++)
            rgb[3 * e + a] = box[i].pixels
                ? (UBYTE)((sum[a] + box[i].pixels / 2) / box[i].pixels) : 0;
    }
    for (i = 0; i < nbox; i++) {
        ULONG j;
        for (j = box[i].start; j < box[i].start + box[i].n; j++)
            hist[list[j]] = first + i;
    }
    for (y = 0; y < h; y++) {
        const UBYTE *row = argb + y * stride;
        for (x = 0; x < w; x++) {
            const UBYTE *p = row + 4 * x;
            *idx++ = (first && p[0] < 0x80) ? 0 : (UBYTE)hist[BIN(p)];
        }
    }
    return nbins ? first + nbox : 1;
}

LONG gif_encode(struct gife_env *env, const UBYTE *argb, ULONG width, ULONG height, ULONG stride,
                ULONG flags, UBYTE **gif, ULONG *size)
{
    ULONG first = 0, colours, x, y;
    UBYTE *idx, *rgb, *work;
    LONG err;

    *gif = 0;
    *size = 0;
    if (!argb || !width || !height || width > 65535UL || height > 65535UL || stride < width * 4
        || !capacity(width, height))
        return GIFE_ERR_ARGS;
    if (!(flags & GIFE_OPAQUE))
        for (y = 0; y < height && !first; y++)
            for (x = 0; x < width; x++)
                if (argb[y * stride + 4 * x] < 0x80) {
                    first = 1;                           /* entry 0 is the transparent colour */
                    break;
                }
    if (!(idx = env->alloc(env, width * height + 768)))
        return GIFE_ERR_MEMORY;
    rgb = idx + width * height;
    rgb[0] = rgb[1] = rgb[2] = 0;
    if (!(work = env->alloc(env, TABLE * 4 + TABLE))) {
        env->free(env, idx);
        return GIFE_ERR_MEMORY;
    }
    colours = exact(argb, width, height, stride, first, 256, idx, rgb, (ULONG *)work, work + TABLE * 4);
    env->free(env, work);
    if (!colours) {                                      /* more than GIF holds: median cut */
        if (!(work = env->alloc(env, 32768UL * 4 + 32768UL * 2 + 256 * sizeof(struct box)))) {
            env->free(env, idx);
            return GIFE_ERR_MEMORY;
        }
        colours = median_cut(argb, width, height, stride, first, 256, idx, rgb, (ULONG *)work,
                             (UWORD *)(work + 32768UL * 4),
                             (struct box *)(work + 32768UL * 4 + 32768UL * 2));
        env->free(env, work);
    }
    err = write_gif(env, idx, width, height, width, rgb, colours, first ? 0 : -1, gif, size);
    env->free(env, idx);
    return err;
}
