/* GFOpenDatatypes animation.datatype: films written as MP4.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 * Clean-room; see mp4_core.h for what this is written from.
 *
 * No division by a constant (GCC makes most of them a 64-bit multiply,
 * which the 68060 traps): mp4_div() divides by what it is given, and the
 * tests count the object's 64-bit multiplies and divides (none).
 */
#include "mp4_core.h"
#include "mp4_h264.h"

#define MP4_ERROR_NOT_IMPLEMENTED 236   /* dos/dos.h */
#define MP4_PS_MAX 128                  /* an SPS or PPS of ours is a few dozen bytes */
#define MP4_TICKS_MAX 4000000UL         /* frames' ticks * 1000 must fit 32 bits */

/* a / b, a real divide (no constant for GCC to turn into a multiply) */
__attribute__((noipa)) static odt_u32 mp4_div(odt_u32 a, odt_u32 b)
{
    return b ? a / b : 0;
}

__attribute__((noipa)) static odt_u32 mp4_mod(odt_u32 a, odt_u32 b)
{
    return b ? a % b : 0;
}

/* a * c / b with a * c possibly past 32 bits: (a / b) * c + (a % b) * c / b,
 * for c up to 65535 and b up to 65535 */
static odt_u32 mp4_scale(odt_u32 a, odt_u32 c, odt_u32 b)
{
    return mp4_div(a, b) * c + mp4_div(mp4_mod(a, b) * c, b);
}

odt_u32 adt_mp4_qp(odt_s32 quality)
{
    if (quality < 0) quality = ADT_MP4_QUALITY;
    if (quality > 100) quality = 100;
    return 10 + mp4_div((odt_u32)(100 - quality) * 41 + 50, 100);
}

odt_u32 adt_mp4_speed(odt_s32 level)
{
    /* minih264e's speeds that differ: 10 whole-pixel motion, no deblocking;
     * 9 deblocking; 8 quarter-pixel motion, no deblocking; 7 (to 2) both; 1
     * 4x4 intra blocks in P frames too; 0 partitions of a macroblock too */
    static const odt_u8 speeds[10] = { 10, 9, 8, 7, 7, 7, 7, 7, 1, 0 };
    if (level < 0) level = ADT_MP4_LEVEL;
    if (level > 9) level = 9;
    return speeds[level];
}

/* ---- a frame in 4:2:0 YUV ------------------------------------------------------ */

/* BT.601 studio range, 8-bit integer form: Y from R, G, B; U and V from the
 * sums over four pixels (exact: the per-pixel terms add up the same) */
#define MP4_Y(r, g, b) ((odt_u8)(((66 * (odt_s32)(r) + 129 * (odt_s32)(g) + 25 * (odt_s32)(b) + 128) >> 8) + 16))
#define MP4_U(r, g, b) (-38 * (odt_s32)(r) - 74 * (odt_s32)(g) + 112 * (odt_s32)(b))
#define MP4_V(r, g, b) (112 * (odt_s32)(r) - 94 * (odt_s32)(g) - 18 * (odt_s32)(b))
#define MP4_C(sum4) ((odt_u8)(((sum4) + 131584) >> 10))   /* 4 x 128 x 256 + 512, then / 1024 */

void adt_mp4_palette(const odt_u8 *rgb, odt_u32 ncolors, odt_u32 depth, odt_u32 modeid, struct adt_mp4_pal *pal)
{
    odt_u32 i, n = (ncolors > 256 ? 256 : ncolors) * 3;
    odt_u8 *c = pal->rgb;
    for (i = 0; i < 768; i++) c[i] = rgb && i < n ? rgb[i] : 0;
    if ((modeid & ADT_MODE_EHB) && !(modeid & ADT_HAM) && depth == 6)
        for (i = 0; i < 96; i++) c[96 + i] = (odt_u8)(c[i] >> 1);
    for (i = 0; i < 256; i++, c += 3) {
        pal->y[i] = MP4_Y(c[0], c[1], c[2]);
        pal->u[i] = (odt_s16)MP4_U(c[0], c[1], c[2]);
        pal->v[i] = (odt_s16)MP4_V(c[0], c[1], c[2]);
        /* HAM's: each gun's share of Y (the 128 to round with in red's) */
        pal->yr[i] = (odt_u16)(66 * i + 128);
        pal->yg[i] = (odt_u16)(129 * i);
        pal->yb[i] = (odt_u16)(25 * i);
    }
}

/* A byte of a plane as the eight pixels' bits, one a byte, pixel 0 in the
 * top byte of the first word */
#define MP4_B(b, k) ((odt_u32)(((b) >> (7 - (k))) & 1))
#define MP4_S1(b) { MP4_B(b, 0) << 24 | MP4_B(b, 1) << 16 | MP4_B(b, 2) << 8 | MP4_B(b, 3), \
                    MP4_B(b, 4) << 24 | MP4_B(b, 5) << 16 | MP4_B(b, 6) << 8 | MP4_B(b, 7) }
#define MP4_S4(b) MP4_S1(b), MP4_S1((b) + 1), MP4_S1((b) + 2), MP4_S1((b) + 3)
#define MP4_S16(b) MP4_S4(b), MP4_S4((b) + 4), MP4_S4((b) + 8), MP4_S4((b) + 12)
#define MP4_S64(b) MP4_S16(b), MP4_S16((b) + 16), MP4_S16((b) + 32), MP4_S16((b) + 48)
static const odt_u32 mp4_spread[256][2] = { MP4_S64(0), MP4_S64(64), MP4_S64(128), MP4_S64(192) };
typedef odt_u32 mp4_u32u __attribute__((may_alias, aligned(1)));

/* Row y's w pixels as colour indexes into idx (room for w rounded up to 8,
 * and one more), the last repeated once more when w is odd */
static void mp4_indexes(const struct adt_BitMap *bm, odt_u32 y, odt_u32 w, odt_u32 depth, odt_u8 *idx)
{
    const odt_u8 *pl[8];
    odt_u32 sh[8], p, n = 0, x, bytes = (w + 7) >> 3, nb = bytes, nd = depth, hi1 = 0, lo1 = 0;
    if (nd > bm->Depth) nd = bm->Depth;
    if (nd > 8) nd = 8;
    if (y >= bm->Rows) nd = 0;
    if (nb > bm->BytesPerRow) nb = bm->BytesPerRow;
    /* the planes with bits to read; a plane of ones is the same in every byte */
    for (p = 0; p < nd; p++) {
        const odt_u8 *q = bm->Planes[p];
        if (q == (const odt_u8 *)~0UL) {
            hi1 |= 0x01010101UL << p;
            lo1 |= 0x01010101UL << p;
        } else if (q) {
            pl[n] = q + y * bm->BytesPerRow;
            sh[n++] = p;
        }
    }
    for (x = 0; x < nb; x++, idx += 8) {
        odt_u32 hi = hi1, lo = lo1, k;
        for (k = 0; k < n; k++) {
            const odt_u32 *t = mp4_spread[pl[k][x]];
            hi |= t[0] << sh[k];
            lo |= t[1] << sh[k];
        }
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__)
        /* pixel 0 in the top byte is the first byte in memory: two longs
         * (a 68020 and up writes them anywhere) */
        ((mp4_u32u *)idx)[0] = hi;
        ((mp4_u32u *)idx)[1] = lo;
#else
        idx[0] = (odt_u8)(hi >> 24); idx[1] = (odt_u8)(hi >> 16); idx[2] = (odt_u8)(hi >> 8); idx[3] = (odt_u8)hi;
        idx[4] = (odt_u8)(lo >> 24); idx[5] = (odt_u8)(lo >> 16); idx[6] = (odt_u8)(lo >> 8); idx[7] = (odt_u8)lo;
#endif
    }
    for (; x < bytes; x++, idx += 8)    /* past the bitmap's row: 0 */
        for (p = 0; p < 8; p++) idx[p] = 0;
    idx -= bytes << 3;
    if (w & 1) idx[w] = idx[w - 1];
}

/* HAM: row y as R, G, B into out (cw pixels), from colour 0 at the row's
 * start; HAM6 (bits 4 and 5 say what) or HAM8 (bits 6 and 7) */
static void mp4_ham(const struct adt_BitMap *bm, odt_u32 y, odt_u32 w, odt_u32 cw, odt_u32 depth, odt_u32 bits,
                    const odt_u8 *pal, odt_u8 *out)
{
    odt_u8 *idx = out + 2 * cw;        /* the row's last third: each read before it is written over */
    odt_u32 x, mask = (1UL << bits) - 1, r = pal[0], g = pal[1], b = pal[2];
    mp4_indexes(bm, y, w, depth, idx);
    for (x = 0; x < w; x++) {
        odt_u32 i = idx[x], v = i & mask, c;
        switch (i >> bits & 3) {
        case 0: r = pal[v * 3]; g = pal[v * 3 + 1]; b = pal[v * 3 + 2]; break;
        case 1: c = v << (8 - bits); b = c | c >> bits; break;
        case 2: c = v << (8 - bits); r = c | c >> bits; break;
        default: c = v << (8 - bits); g = c | c >> bits; break;
        }
        out[3 * x] = (odt_u8)r;
        out[3 * x + 1] = (odt_u8)g;
        out[3 * x + 2] = (odt_u8)b;
    }
    if (w < cw) {
        out[3 * w] = out[3 * w - 3];
        out[3 * w + 1] = out[3 * w - 2];
        out[3 * w + 2] = out[3 * w - 1];
    }
}

void adt_mp4_yuv(const struct adt_BitMap *bm, odt_u32 w, odt_u32 h, odt_u32 depth, odt_u32 modeid,
                 const struct adt_mp4_pal *pal, odt_u8 *y, odt_u8 *u, odt_u8 *v, odt_u32 ys, odt_u8 *row)
{
    odt_u32 cw = (w + 1) & ~1UL, ch = (h + 1) & ~1UL, bits = 0, j, x;
    odt_u32 iw = ((w + 7) & ~7UL) + 4;  /* an index row's room */
    if (modeid & ADT_HAM) bits = depth <= 6 ? 4 : 6;
    for (j = 0; j < ch; j += 2) {
        odt_u32 j0 = j < h ? j : h - 1, j1 = j + 1 < h ? j + 1 : h - 1;
        odt_u8 *y0 = y + j * ys, *y1 = y0 + ys, *uu = u + (j >> 1) * (ys >> 1), *vv = v + (j >> 1) * (ys >> 1);
        if (!bits) {
            /* each colour's Y, U and V from the tables */
            odt_u8 *a = row, *b = row + iw;
            const odt_u8 *ty = pal->y;
            const odt_s16 *tu = pal->u, *tv = pal->v;
            mp4_indexes(bm, j0, w, depth, a);
            if (j1 != j0) mp4_indexes(bm, j1, w, depth, b);
            else b = a;
            for (x = 0; x < cw; x += 2) {
                odt_u32 i0 = a[x], i1 = a[x + 1], i2 = b[x], i3 = b[x + 1];
                y0[x] = ty[i0];
                y0[x + 1] = ty[i1];
                y1[x] = ty[i2];
                y1[x + 1] = ty[i3];
                uu[x >> 1] = MP4_C(tu[i0] + tu[i1] + tu[i2] + tu[i3]);
                vv[x >> 1] = MP4_C(tv[i0] + tv[i1] + tv[i2] + tv[i3]);
            }
        } else {
            odt_u8 *a = row, *b = row + 3 * cw + 8;
            const odt_u16 *tr = pal->yr, *tg = pal->yg, *tb = pal->yb;
            mp4_ham(bm, j0, w, cw, depth, bits, pal->rgb, a);
            if (j1 != j0) mp4_ham(bm, j1, w, cw, depth, bits, pal->rgb, b);
            else b = a;
            for (x = 0; x < cw; x += 2) {
                const odt_u8 *p = a + 3 * x, *q = b + 3 * x;
                odt_s32 r4 = p[0] + p[3] + q[0] + q[3], g4 = p[1] + p[4] + q[1] + q[4], b4 = p[2] + p[5] + q[2] + q[5];
                y0[x] = (odt_u8)(((tr[p[0]] + tg[p[1]] + tb[p[2]]) >> 8) + 16);
                y0[x + 1] = (odt_u8)(((tr[p[3]] + tg[p[4]] + tb[p[5]]) >> 8) + 16);
                y1[x] = (odt_u8)(((tr[q[0]] + tg[q[1]] + tb[q[2]]) >> 8) + 16);
                y1[x + 1] = (odt_u8)(((tr[q[3]] + tg[q[4]] + tb[q[5]]) >> 8) + 16);
                uu[x >> 1] = MP4_C(MP4_U(r4, g4, b4));
                vv[x >> 1] = MP4_C(MP4_V(r4, g4, b4));
            }
        }
    }
}

/* ---- the file -------------------------------------------------------------------- */

static void mp4_put32(odt_u8 *p, odt_u32 v)
{
    p[0] = (odt_u8)(v >> 24); p[1] = (odt_u8)(v >> 16); p[2] = (odt_u8)(v >> 8); p[3] = (odt_u8)v;
}

/* Boxes into p, or (p NULL) only counted */
struct mp4_out {
    odt_u8 *p;
    odt_u32 n;
};

static void o8(struct mp4_out *o, odt_u32 v)
{
    if (o->p) o->p[o->n] = (odt_u8)v;
    o->n++;
}

static void o16(struct mp4_out *o, odt_u32 v) { o8(o, v >> 8); o8(o, v); }
static void o32(struct mp4_out *o, odt_u32 v) { o16(o, v >> 16); o16(o, v); }
static void ozero(struct mp4_out *o, odt_u32 n) { while (n--) o8(o, 0); }

static void obytes(struct mp4_out *o, const odt_u8 *b, odt_u32 n)
{
    while (n--) o8(o, *b++);
}

static void ostr(struct mp4_out *o, const char *s)
{
    while (*s) o8(o, (odt_u8)*s++);
    o8(o, 0);
}

static odt_u32 box(struct mp4_out *o, odt_u32 type)
{
    odt_u32 at = o->n;
    o32(o, 0);
    o32(o, type);
    return at;
}

static odt_u32 fullbox(struct mp4_out *o, odt_u32 type, odt_u32 version, odt_u32 flags)
{
    odt_u32 at = box(o, type);
    o32(o, version << 24 | flags);
    return at;
}

static void done(struct mp4_out *o, odt_u32 at)
{
    if (o->p) mp4_put32(o->p + at, o->n - at);
}

#define ID(a, b, c, d) ODT_ID(a, b, c, d)

static void omatrix(struct mp4_out *o)
{
    o32(o, 0x00010000); o32(o, 0); o32(o, 0);
    o32(o, 0); o32(o, 0x00010000); o32(o, 0);
    o32(o, 0); o32(o, 0); o32(o, 0x40000000);
}

/* What the frames left for the moov */
struct mp4_w {
    odt_u32 cw, ch, width, height, fps;
    odt_u32 frames;                     /* written */
    odt_u32 *vsize, *voff, *vdur;
    odt_u8 *vsync;
    odt_u32 nsync;
    odt_u32 ticks;                      /* the frames' durations, in 1/fps seconds */
    odt_u32 *aoff, *alen;               /* each sound chunk's place and samples */
    odt_u32 chunks, samples, rate, channels;
    odt_u8 sps[MP4_PS_MAX], pps[MP4_PS_MAX];
    odt_u32 spslen, ppslen;
};

static void otkhd(struct mp4_out *o, odt_u32 id, odt_u32 duration, odt_u32 sound, odt_u32 w, odt_u32 h)
{
    odt_u32 at = fullbox(o, ID('t', 'k', 'h', 'd'), 0, 3);   /* enabled, in the movie */
    o32(o, 0);                          /* creation and modification times */
    o32(o, 0);
    o32(o, id);
    o32(o, 0);
    o32(o, duration);                   /* in the mvhd's timescale */
    ozero(o, 8);
    o16(o, 0);                          /* layer */
    o16(o, 0);                          /* alternate group */
    o16(o, sound ? 0x0100 : 0);         /* volume */
    o16(o, 0);
    omatrix(o);
    o32(o, w << 16);
    o32(o, h << 16);
    done(o, at);
}

static void omdhd(struct mp4_out *o, odt_u32 timescale, odt_u32 duration)
{
    odt_u32 at = fullbox(o, ID('m', 'd', 'h', 'd'), 0, 0);
    o32(o, 0);
    o32(o, 0);
    o32(o, timescale);
    o32(o, duration);
    o16(o, 0x55C4);                     /* "und" */
    o16(o, 0);
    done(o, at);
}

static void ohdlr(struct mp4_out *o, odt_u32 type, const char *name)
{
    odt_u32 at = fullbox(o, ID('h', 'd', 'l', 'r'), 0, 0);
    o32(o, 0);
    o32(o, type);
    ozero(o, 12);
    ostr(o, name);
    done(o, at);
}

static void odinf(struct mp4_out *o)
{
    odt_u32 at = box(o, ID('d', 'i', 'n', 'f')), dref = fullbox(o, ID('d', 'r', 'e', 'f'), 0, 0), url;
    o32(o, 1);
    url = fullbox(o, ID('u', 'r', 'l', ' '), 0, 1);   /* the data is in this file */
    done(o, url);
    done(o, dref);
    done(o, at);
}

static void ostco(struct mp4_out *o, const odt_u32 *off, odt_u32 n)
{
    odt_u32 at = fullbox(o, ID('s', 't', 'c', 'o'), 0, 0), i;
    o32(o, n);
    for (i = 0; i < n; i++) o32(o, off[i]);
    done(o, at);
}

static void ovideo(struct mp4_out *o, const struct mp4_w *m, odt_u32 movie_ms)
{
    odt_u32 trak = box(o, ID('t', 'r', 'a', 'k')), mdia, minf, stbl, at, at2, i, n;
    otkhd(o, 1, movie_ms, 0, m->width, m->height);
    mdia = box(o, ID('m', 'd', 'i', 'a'));
    omdhd(o, m->fps * 1000, m->ticks * 1000);
    ohdlr(o, ID('v', 'i', 'd', 'e'), "VideoHandler");
    minf = box(o, ID('m', 'i', 'n', 'f'));
    at = fullbox(o, ID('v', 'm', 'h', 'd'), 0, 1);
    ozero(o, 8);                        /* graphicsmode, opcolor */
    done(o, at);
    odinf(o);
    stbl = box(o, ID('s', 't', 'b', 'l'));
    at = fullbox(o, ID('s', 't', 's', 'd'), 0, 0);
    o32(o, 1);
    at2 = box(o, ID('a', 'v', 'c', '1'));
    ozero(o, 6);
    o16(o, 1);                          /* data_reference_index */
    ozero(o, 16);
    o16(o, m->cw);
    o16(o, m->ch);
    o32(o, 0x00480000);                 /* 72 dpi */
    o32(o, 0x00480000);
    o32(o, 0);
    o16(o, 1);                          /* a frame a sample */
    ozero(o, 32);                       /* compressorname */
    o16(o, 0x0018);
    o16(o, 0xFFFF);
    {
        odt_u32 c = box(o, ID('a', 'v', 'c', 'C'));
        o8(o, 1);
        o8(o, m->sps[1]);               /* profile, its compatibility, level: the SPS's */
        o8(o, m->sps[2]);
        o8(o, m->sps[3]);
        o8(o, 0xFF);                    /* 4-byte NAL lengths */
        o8(o, 0xE1);                    /* one SPS */
        o16(o, m->spslen);
        obytes(o, m->sps, m->spslen);
        o8(o, 1);                       /* one PPS */
        o16(o, m->ppslen);
        obytes(o, m->pps, m->ppslen);
        done(o, c);
    }
    done(o, at2);
    done(o, at);
    /* stts: runs of equal durations */
    at = fullbox(o, ID('s', 't', 't', 's'), 0, 0);
    for (i = 0, n = 0; i < m->frames; i++)
        if (!i || m->vdur[i] != m->vdur[i - 1]) n++;
    o32(o, n);
    for (i = 0; i < m->frames; i = n) {
        for (n = i + 1; n < m->frames && m->vdur[n] == m->vdur[i]; n++) {}
        o32(o, n - i);
        o32(o, m->vdur[i] * 1000);
    }
    done(o, at);
    if (m->nsync < m->frames) {
        at = fullbox(o, ID('s', 't', 's', 's'), 0, 0);
        o32(o, m->nsync);
        for (i = 0; i < m->frames; i++)
            if (m->vsync[i]) o32(o, i + 1);
        done(o, at);
    }
    at = fullbox(o, ID('s', 't', 's', 'c'), 0, 0);
    o32(o, 1);
    o32(o, 1);                          /* from chunk 1: a sample a chunk */
    o32(o, 1);
    o32(o, 1);
    done(o, at);
    at = fullbox(o, ID('s', 't', 's', 'z'), 0, 0);
    o32(o, 0);
    o32(o, m->frames);
    for (i = 0; i < m->frames; i++) o32(o, m->vsize[i]);
    done(o, at);
    ostco(o, m->voff, m->frames);
    done(o, stbl);
    done(o, minf);
    done(o, mdia);
    done(o, trak);
}

static void osound(struct mp4_out *o, const struct mp4_w *m)
{
    odt_u32 trak = box(o, ID('t', 'r', 'a', 'k')), mdia, minf, stbl, at, at2, i, n;
    otkhd(o, 2, mp4_scale(m->samples, 1000, m->rate), 1, 0, 0);
    mdia = box(o, ID('m', 'd', 'i', 'a'));
    omdhd(o, m->rate, m->samples);
    ohdlr(o, ID('s', 'o', 'u', 'n'), "SoundHandler");
    minf = box(o, ID('m', 'i', 'n', 'f'));
    at = fullbox(o, ID('s', 'm', 'h', 'd'), 0, 0);
    o32(o, 0);                          /* balance */
    done(o, at);
    odinf(o);
    stbl = box(o, ID('s', 't', 'b', 'l'));
    at = fullbox(o, ID('s', 't', 's', 'd'), 0, 0);
    o32(o, 1);
    at2 = box(o, ID('t', 'w', 'o', 's'));   /* signed PCM, big-endian: 8 bits as Paula plays */
    ozero(o, 6);
    o16(o, 1);
    ozero(o, 8);                        /* version 0 */
    o16(o, m->channels);
    o16(o, 8);
    o16(o, 0);
    o16(o, 0);
    o32(o, m->rate << 16);
    done(o, at2);
    done(o, at);
    at = fullbox(o, ID('s', 't', 't', 's'), 0, 0);
    o32(o, 1);
    o32(o, m->samples);
    o32(o, 1);
    done(o, at);
    at = fullbox(o, ID('s', 't', 's', 'c'), 0, 0);
    for (i = 0, n = 0; i < m->chunks; i++)
        if (!i || m->alen[i] != m->alen[i - 1]) n++;
    o32(o, n);
    for (i = 0; i < m->chunks; i++)
        if (!i || m->alen[i] != m->alen[i - 1]) {
            o32(o, i + 1);
            o32(o, m->alen[i]);
            o32(o, 1);
        }
    done(o, at);
    at = fullbox(o, ID('s', 't', 's', 'z'), 0, 0);
    o32(o, 1);                          /* every sample (a sample frame) the same */
    o32(o, m->samples);
    done(o, at);
    ostco(o, m->aoff, m->chunks);
    done(o, stbl);
    done(o, minf);
    done(o, mdia);
    done(o, trak);
}

static void omoov(struct mp4_out *o, const struct mp4_w *m)
{
    odt_u32 moov = box(o, ID('m', 'o', 'o', 'v')), at, ms = mp4_scale(m->ticks, 1000, m->fps), sms = 0;
    if (m->samples) sms = mp4_scale(m->samples, 1000, m->rate);
    at = fullbox(o, ID('m', 'v', 'h', 'd'), 0, 0);
    o32(o, 0);
    o32(o, 0);
    o32(o, 1000);                       /* milliseconds */
    o32(o, ms > sms ? ms : sms);
    o32(o, 0x00010000);                 /* rate 1.0 */
    o16(o, 0x0100);                     /* volume 1.0 */
    ozero(o, 10);
    omatrix(o);
    ozero(o, 24);
    o32(o, m->samples ? 3 : 2);         /* next_track_ID */
    done(o, at);
    ovideo(o, m, ms);
    if (m->samples) osound(o, m);
    done(o, moov);
}

/* ---- writing ---------------------------------------------------------------------- */

struct mp4_job {
    const struct adt_mp4_spec *s;
    const struct adt_mp4_io *io;
    struct mp4_w m;
    odt_u32 pos;                        /* bytes written */
    odt_u8 *abuf;                       /* this frame's sound, interleaved */
    odt_u32 abuf_size, alen;            /* its size, and samples in it now */
    odt_u32 film_at, film_rem;          /* the film's sample: where the next frame's starts */
};

static odt_s32 mp4_put(struct mp4_job *j, const odt_u8 *p, odt_u32 n)
{
    odt_s32 err;
    if (!n) return 0;
    if (j->pos + n < j->pos) return MP4_ERROR_NOT_IMPLEMENTED;   /* past 4 GB */
    if ((err = j->io->aw.put(j->io->aw.h, p, n))) return err;
    j->pos += n;
    return 0;
}

static int mp4_room(struct mp4_job *j, odt_u32 n)
{
    if (n <= j->abuf_size) return 1;
    if (j->abuf) j->io->aw.free(j->io->aw.h, j->abuf);
    j->abuf_size = 0;
    if (!(j->abuf = j->io->aw.alloc(j->io->aw.h, n))) return 0;
    j->abuf_size = n;
    return 1;
}

/* The sound for a frame of dur ticks: its own (fi), or its share of the
 * film's sample. 0, or an error. */
static odt_s32 mp4_frame_sound(struct mp4_job *j, const struct adt_mp4_frame *fi, odt_u32 dur, int last)
{
    const struct adt_mp4_spec *s = j->s;
    const odt_s8 *l = 0, *r = 0;
    odt_u32 n = 0, i, ch;
    j->alen = 0;
    if (!s->sound) return 0;
    if (s->left && s->sound_len) {
        /* rate / fps samples a tick, the remainder carried */
        odt_u32 q = mp4_div(s->rate, s->fps), rm = mp4_mod(s->rate, s->fps), t;
        for (t = 0; t < dur; t++) {
            n += q;
            if ((j->film_rem += rm) >= s->fps) {
                j->film_rem -= s->fps;
                n++;
            }
        }
        if (j->film_at >= s->sound_len) n = 0;
        else if (last || n > s->sound_len - j->film_at) n = s->sound_len - j->film_at;
        l = s->left + j->film_at;
        r = s->right ? s->right + j->film_at : 0;
        j->film_at += n;
        if (!j->m.channels && n) {
            j->m.channels = r ? 2 : 1;
            j->m.rate = s->rate;
        }
    } else if (fi->left && fi->sound_len) {
        l = fi->left;
        r = fi->right;
        n = fi->sound_len;
        if (!j->m.channels) {
            j->m.channels = r ? 2 : 1;
            j->m.rate = fi->rate ? fi->rate : s->rate;
        }
    }
    if (!n) return 0;
    if (!j->m.rate || j->m.rate > 65535) return MP4_ERROR_NOT_IMPLEMENTED;
    ch = j->m.channels;
    if (!mp4_room(j, n * ch)) return DTC_ERROR_NO_FREE_STORE;
    if (ch == 1) {
        for (i = 0; i < n; i++) j->abuf[i] = (odt_u8)l[i];
    } else {
        /* mono into a stereo track goes to both sides */
        for (i = 0; i < n; i++) {
            j->abuf[2 * i] = (odt_u8)l[i];
            j->abuf[2 * i + 1] = (odt_u8)(r ? r[i] : l[i]);
        }
    }
    j->alen = n;
    return 0;
}

/* One frame's access unit: the encoder's NAL units with their start codes
 * made 4-byte lengths, the SPS and PPS kept for the avcC and left out */
static odt_s32 mp4_access_unit(struct mp4_job *j, odt_u8 *d, odt_u32 n, odt_u32 f)
{
    odt_u32 i = 0, run = 0, size = 0;
    odt_s32 err;
    int idr = 0, have = 0;
    while (i + 4 <= n) {
        odt_u32 s = i + 4, e = s, len, type;
        if (d[i] || d[i + 1] || d[i + 2] || d[i + 3] != 1) return ODT_DTERROR_COULDNT_SAVE;
        while (e + 4 <= n && (d[e] || d[e + 1] || d[e + 2] || d[e + 3] != 1)) e++;
        if (e + 4 > n) e = n;
        len = e - s;
        if (!len) return ODT_DTERROR_COULDNT_SAVE;
        type = d[s] & 31;
        if (type == 7 || type == 8) {
            odt_u8 *keep = type == 7 ? j->m.sps : j->m.pps;
            odt_u32 *kl = type == 7 ? &j->m.spslen : &j->m.ppslen;
            if (!*kl) {
                odt_u32 k;
                if (len > MP4_PS_MAX || (type == 7 && len < 4)) return ODT_DTERROR_COULDNT_SAVE;
                for (k = 0; k < len; k++) keep[k] = d[s + k];
                *kl = len;
            }
            /* what came before it goes out first */
            if (have && (err = mp4_put(j, d + run, i - run))) return err;
            have = 0;
        } else {
            if (!have) {
                run = i;
                have = 1;
            }
            if (type == 5) idr = 1;
            mp4_put32(d + i, len);
            size += 4 + len;
        }
        i = e;
    }
    if (i != n) return ODT_DTERROR_COULDNT_SAVE;
    if (have && (err = mp4_put(j, d + run, n - run))) return err;
    if (!size) return ODT_DTERROR_COULDNT_SAVE;
    j->m.vsize[f] = size;
    j->m.vsync[f] = (odt_u8)idr;
    if (idr) j->m.nsync++;
    return 0;
}

odt_s32 adt_mp4_write(const struct adt_mp4_spec *s, const struct adt_mp4_io *io)
{
    struct mp4_job *j = 0;
    const struct adt_aw_io *a;
    odt_u32 cw, ch, f, mdat, encsize, ysize, i, ncol;
    odt_u8 *mem = 0, *yuv = 0, *row = 0, *film_rgb = 0, *moov = 0;
    struct adt_mp4_pal *pal = 0;
    void *enc;
    odt_s32 err = 0;
    struct mp4_out o;

    if (!s || !io || !io->aw.alloc || !io->aw.free || !io->aw.put || !io->aw.patch || !io->aw.frame || !io->aw.done ||
        !s->width || !s->height || !s->depth || s->depth > 8 || !s->frames || !s->fps)
        return DTC_ERROR_REQUIRED_ARG_MISSING;
    a = &io->aw;
    cw = (s->width + 1) & ~1UL;
    ch = (s->height + 1) & ~1UL;
    if (cw > 4080 || ch > 4080 || s->fps > 4000 || !(encsize = mp4h_size(cw, ch))) return MP4_ERROR_NOT_IMPLEMENTED;
    ysize = MP4H_PAD(cw) * MP4H_PAD(ch);   /* whole macroblocks: the encoder writes them all */
    if (s->frames > 0x3FFFFFFFUL) return MP4_ERROR_NOT_IMPLEMENTED;
    if (!(j = a->alloc(a->h, sizeof *j))) return DTC_ERROR_NO_FREE_STORE;
    for (i = 0; i < sizeof *j; i++) ((odt_u8 *)j)[i] = 0;
    j->s = s;
    j->io = io;
    j->m.cw = cw;
    j->m.ch = ch;
    j->m.width = s->width;
    j->m.height = s->height;
    j->m.fps = s->fps;
    mem = a->alloc(a->h, encsize);
    yuv = a->alloc(a->h, ysize + (ysize >> 1));
    row = a->alloc(a->h, ADT_MP4_ROW(cw));
    pal = a->alloc(a->h, 2 * sizeof *pal + 768);   /* the film's, a frame's own, the film's colours */
    j->m.vsize = a->alloc(a->h, s->frames * 4);
    j->m.voff = a->alloc(a->h, s->frames * 4);
    j->m.vdur = a->alloc(a->h, s->frames * 4);
    j->m.vsync = a->alloc(a->h, s->frames);
    j->m.aoff = a->alloc(a->h, s->frames * 4);
    j->m.alen = a->alloc(a->h, s->frames * 4);
    if (!mem || !yuv || !row || !pal || !j->m.vsize || !j->m.voff || !j->m.vdur || !j->m.vsync || !j->m.aoff ||
        !j->m.alen) {
        err = DTC_ERROR_NO_FREE_STORE;
        goto out;
    }
    if (!(enc = mp4h_init(mem, cw, ch, s->gop))) {
        err = MP4_ERROR_NOT_IMPLEMENTED;
        goto out;
    }
    /* the film's colours, and its palette */
    film_rgb = (odt_u8 *)(pal + 2);
    ncol = s->colors ? (s->ncolors > 256 ? 256 : s->ncolors) : 0;
    for (i = 0; i < ncol; i++) {
        film_rgb[3 * i] = s->colors[i].red;
        film_rgb[3 * i + 1] = s->colors[i].green;
        film_rgb[3 * i + 2] = s->colors[i].blue;
    }
    adt_mp4_palette(film_rgb, ncol, s->depth, s->modeid, pal);

    /* ftyp, and the mdat's header (its size patched at the end) */
    {
        static const odt_u8 head[] = {
            0, 0, 0, 32, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0,
            'i', 's', 'o', 'm', 'i', 's', 'o', '2', 'a', 'v', 'c', '1', 'm', 'p', '4', '1',
            0, 0, 0, 0, 'm', 'd', 'a', 't'
        };
        if ((err = mp4_put(j, head, sizeof head))) goto out;
        mdat = 32;
    }

    for (f = 0; f < s->frames; f++) {
        struct adt_mp4_frame fi;
        const struct adt_BitMap *bm;
        odt_u8 *d;
        odt_u32 n;
        for (i = 0; i < sizeof fi; i++) ((odt_u8 *)&fi)[i] = 0;
        if (!(bm = a->frame(a->h, f))) {
            err = ODT_DTERROR_COULDNT_SAVE;
            goto out;
        }
        if (io->info) io->info(a->h, f, &fi);
        if (!fi.duration) fi.duration = 1;
        if (fi.rgb) adt_mp4_palette(fi.rgb, fi.ncolors, s->depth, s->modeid, pal + 1);
        adt_mp4_yuv(bm, s->width, s->height, s->depth, s->modeid, fi.rgb ? pal + 1 : pal, yuv, yuv + ysize,
                    yuv + ysize + (ysize >> 2), MP4H_PAD(cw), row);
        err = mp4_frame_sound(j, &fi, fi.duration, f + 1 == s->frames);
        a->done(a->h, f);
        if (err) goto out;
        if (j->m.ticks + fi.duration > MP4_TICKS_MAX) {
            err = MP4_ERROR_NOT_IMPLEMENTED;
            goto out;
        }
        if (mp4h_encode(enc, yuv, yuv + ysize, yuv + ysize + (ysize >> 2), s->qp, s->speed,
                        &d, (unsigned long *)&n)) {
            err = ODT_DTERROR_COULDNT_SAVE;
            goto out;
        }
        j->m.voff[f] = j->pos;
        j->m.vdur[f] = fi.duration;
        j->m.ticks += fi.duration;
        if ((err = mp4_access_unit(j, d, n, f))) goto out;
        j->m.frames = f + 1;
        if (j->alen) {
            j->m.aoff[j->m.chunks] = j->pos;
            j->m.alen[j->m.chunks++] = j->alen;
            j->m.samples += j->alen;
            if ((err = mp4_put(j, j->abuf, j->alen * j->m.channels))) goto out;
        }
    }
    if (!j->m.spslen || !j->m.ppslen) {
        err = ODT_DTERROR_COULDNT_SAVE;
        goto out;
    }
    if ((err = a->patch(a->h, mdat, j->pos - mdat))) goto out;

    o.p = 0;
    o.n = 0;
    omoov(&o, &j->m);
    if (!(moov = a->alloc(a->h, o.n))) {
        err = DTC_ERROR_NO_FREE_STORE;
        goto out;
    }
    i = o.n;
    o.p = moov;
    o.n = 0;
    omoov(&o, &j->m);
    if (o.n != i) err = ODT_DTERROR_COULDNT_SAVE;
    else err = mp4_put(j, moov, o.n);
out:
    if (moov) a->free(a->h, moov);
    if (j) {
        if (j->abuf) a->free(a->h, j->abuf);
        if (j->m.vsize) a->free(a->h, j->m.vsize);
        if (j->m.voff) a->free(a->h, j->m.voff);
        if (j->m.vdur) a->free(a->h, j->m.vdur);
        if (j->m.vsync) a->free(a->h, j->m.vsync);
        if (j->m.aoff) a->free(a->h, j->m.aoff);
        if (j->m.alen) a->free(a->h, j->m.alen);
        a->free(a->h, j);
    }
    if (pal) a->free(a->h, pal);
    if (row) a->free(a->h, row);
    if (yuv) a->free(a->h, yuv);
    if (mem) a->free(a->h, mem);
    return err;
}
