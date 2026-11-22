/*
 * smacker.datatype's core (smk_core.h). MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include "smk_core.h"

#define LEAF 0x8000u                /* a small tree's leaf, its byte in the low 8 bits */
#define NODE 0x80000000UL           /* a big tree's node, the offset to its right child below */
#define SMALL_MAX 511               /* entries in a tree of 256 leaves */
#define SMALL_DEPTH 27              /* the longest code a small tree may give */
#define BIG_DEPTH 500

enum { BLK_MONO, BLK_FULL, BLK_SKIP, BLK_FILL };

/* ---- bits, from the low bit of each byte up ------------------------------ */

struct bits {
    const uint8_t *p;
    uint32_t len, at;               /* at: the next byte to take */
    uint32_t cache;
    unsigned have;
};

static void bits_init(struct bits *b, const uint8_t *p, uint32_t len)
{
    b->p = p;
    b->len = len;
    b->at = 0;
    b->cache = 0;
    b->have = 0;
}

/* Past the end every bit is 0 */
static unsigned bit(struct bits *b)
{
    unsigned v;
    if (!b->have) {
        b->cache = b->at < b->len ? b->p[b->at] : 0;
        b->at++;
        b->have = 8;
    }
    v = b->cache & 1;
    b->cache >>= 1;
    b->have--;
    return v;
}

static unsigned bitsn(struct bits *b, unsigned n)
{
    unsigned v = 0, i;
    for (i = 0; i < n; i++)
        v |= bit(b) << i;
    return v;
}

static int overrun(const struct bits *b)
{
    return b->at > b->len;
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ---- small trees: a byte for each leaf ------------------------------------ */

struct small {
    uint16_t t[SMALL_MAX];          /* depth first: a node holds the offset to its right child */
    uint16_t n;                     /* 0: no tree, every value 0 */
};

/* One tree as the stream gives it, depth first: a 1 is a node (its 0
 * branch follows, then its 1 branch), a 0 a leaf and the 8 bits of its
 * byte. Kept as a stack of the nodes whose branches are still open. */
static int small_read(struct small *h, struct bits *b)
{
    uint16_t open[SMALL_DEPTH + 1];
    uint8_t right[SMALL_DEPTH + 1];
    unsigned sp = 0, leaves = 0;

    h->n = 0;
    for (;;) {
        if (sp > SMALL_DEPTH || h->n >= SMALL_MAX || overrun(b))
            return -1;
        if (bit(b)) {
            open[sp] = h->n;
            right[sp++] = 0;
            h->t[h->n++] = 0;
            continue;
        }
        if (leaves++ >= 256)
            return -1;
        h->t[h->n++] = (uint16_t)(LEAF | bitsn(b, 8));
        /* close the branches this leaf ends */
        while (sp && right[sp - 1])
            sp--;
        if (!sp)
            return 0;
        h->t[open[sp - 1]] = (uint16_t)(h->n - open[sp - 1]);
        right[sp - 1] = 1;
    }
}

static unsigned small_get(const struct small *h, struct bits *b)
{
    unsigned i = 0;
    if (!h->n)
        return 0;
    while (!(h->t[i] & LEAF))
        i += bit(b) ? h->t[i] : 1;
    return h->t[i] & 0xFF;
}

/* ---- big trees: 16-bit values, three cached ------------------------------- */

/* A tree of up to (size + 3) / 4 entries: its low and high byte trees, the
 * three escapes (a leaf with one of them stands for its cache), then the
 * tree, depth first like a small tree, each leaf a value from the byte
 * trees. A cache with no leaf gets an entry of its own after the tree. */
static int big_read(const struct smk_io *io, struct smk_bigtree *h, struct bits *b, uint32_t size)
{
    struct small lo, hi;
    uint32_t esc[3], *open = NULL, length, v;
    uint8_t *right = NULL;
    unsigned sp = 0, k;
    long last[3] = { -1, -1, -1 };
    int rc = -1;

    if (size >= 0x0FFFFFFFUL)
        return -1;
    if (bit(b)) {
        if (small_read(&lo, b))
            return -1;
        bit(b);
    } else
        lo.n = 0;
    if (bit(b)) {
        if (small_read(&hi, b))
            return -1;
        bit(b);
    } else
        hi.n = 0;
    for (k = 0; k < 3; k++)
        esc[k] = bitsn(b, 16);

    length = (size + 3) >> 2;
    if (!(h->t = io->alloc(io->ctx, (length + 3) * sizeof *h->t))
        || !(open = io->alloc(io->ctx, (BIG_DEPTH + 1) * sizeof *open))
        || !(right = io->alloc(io->ctx, BIG_DEPTH + 1)))
        goto out;
    h->n = 0;
    for (;;) {
        if (sp > BIG_DEPTH || h->n + 1 >= length || overrun(b))
            goto out;
        if (bit(b)) {
            open[sp] = h->n;
            right[sp++] = 0;
            h->t[h->n++] = NODE;
            continue;
        }
        v = small_get(&lo, b);
        v |= small_get(&hi, b) << 8;
        for (k = 0; k < 3; k++)
            if (v == esc[k]) {
                last[k] = h->n;
                v = 0;
                break;
            }
        h->t[h->n++] = v;
        while (sp && right[sp - 1])
            sp--;
        if (!sp)
            break;
        h->t[open[sp - 1]] = NODE | (h->n - open[sp - 1]);
        right[sp - 1] = 1;
    }
    bit(b);
    for (k = 0; k < 3; k++) {
        if (last[k] < 0) {
            last[k] = h->n;
            h->t[h->n++] = 0;
        }
        h->last[k] = (uint32_t)last[k];
    }
    rc = 0;
out:
    if (open)
        io->free(io->ctx, open);
    if (right)
        io->free(io->ctx, right);
    return rc;
}

static void big_reset(struct smk_bigtree *h)
{
    if (h->t)
        h->t[h->last[0]] = h->t[h->last[1]] = h->t[h->last[2]] = 0;
}

/* A value; one that differs from the newest cache pushes the caches along */
static uint32_t big_get(struct smk_bigtree *h, struct bits *b)
{
    uint32_t *t = h->t, i = 0, v;
    if (!t)
        return 0;
    while (t[i] & NODE)
        i += bit(b) ? t[i] & ~NODE : 1;
    v = t[i];
    if (v != t[h->last[0]]) {
        t[h->last[2]] = t[h->last[1]];
        t[h->last[1]] = t[h->last[0]];
        t[h->last[0]] = v;
    }
    return v;
}

/* ---- the film ------------------------------------------------------------- */

static int readat(const struct smk_io *io, uint32_t at, void *buf, uint32_t n)
{
    return io->read(io->ctx, at, buf, n) == (long)n ? 0 : -1;
}

static void freep(const struct smk_io *io, void *pp)
{
    void **p = pp;
    if (*p)
        io->free(io->ctx, *p);
    *p = NULL;
}

void smk_close(struct smk *s)
{
    const struct smk_io *io = s->io;
    if (!io)
        return;
    freep(io, &s->offset);
    freep(io, &s->type);
    freep(io, &s->key);
    freep(io, &s->mmap.t);
    freep(io, &s->mclr.t);
    freep(io, &s->full.t);
    freep(io, &s->types.t);
    freep(io, &s->buf);
    freep(io, &s->pixels);
}

int smk_open(struct smk *s, const struct smk_io *io)
{
    uint8_t h[SMK_HEAD], *table = NULL, *trees = NULL;
    uint32_t trees_size, sizes[4], i, at;
    int32_t rate;
    struct bits b;
    int rc = -1;

    memset(s, 0, sizeof *s);
    s->io = io;
    if (readat(io, 0, h, SMK_HEAD) || h[0] != 'S' || h[1] != 'M' || h[2] != 'K' || (h[3] != '2' && h[3] != '4'))
        return -1;
    s->version = h[3] - '0';
    s->width = le32(h + 4);
    s->height = le32(h + 8);
    s->frames = le32(h + 12);
    rate = (int32_t)le32(h + 16);
    s->flags = le32(h + 20);
    trees_size = le32(h + 52);
    for (i = 0; i < 4; i++)
        sizes[i] = le32(h + 56 + 4 * i);
    if (!s->width || !s->height || s->width > 8192 || s->height > 8192 || !s->frames || s->frames > 0x100000UL
        || trees_size > 0x1000000UL)
        return -1;
    s->fps_den = rate > 0 ? (uint32_t)rate * 100 : rate < 0 ? 0u - (uint32_t)rate : 10000;
    if (!s->fps_den || (rate > 0 && (uint32_t)rate > 0xFFFFFFFFUL / 100))
        s->fps_den = 10000;
    s->stored = s->frames + (s->flags & SMK_FLAG_RING ? 1 : 0);
    for (i = 0; i < SMK_TRACKS; i++) {
        uint32_t r = le32(h + 72 + 4 * i);
        struct smk_track *t = &s->track[i];
        if (!r)
            continue;
        t->rate = r & 0xFFFFFF;
        t->channels = r & SMK_AUD_STEREO ? 2 : 1;
        t->bits = r & SMK_AUD_16BITS ? 16 : 8;
        t->packed = r & SMK_AUD_PACKED ? 1 : 0;
        t->playable = t->rate && !(t->packed && (r & SMK_AUD_BINK));
        t->max_bytes = le32(h + 24 + 4 * i);
        if (t->max_bytes > 0x400000UL)       /* minutes of sound in a frame: a damaged header */
            t->playable = 0;
    }

    if (!(table = io->alloc(io->ctx, s->stored * 5))
        || !(s->offset = io->alloc(io->ctx, (s->stored + 1) * sizeof *s->offset))
        || !(s->type = io->alloc(io->ctx, s->stored)) || !(s->key = io->alloc(io->ctx, s->stored))
        || readat(io, SMK_HEAD, table, s->stored * 5))
        goto out;
    at = SMK_HEAD + s->stored * 5 + trees_size;
    for (i = 0; i < s->stored; i++) {
        uint32_t size = le32(table + 4 * i);
        s->offset[i] = at;
        s->key[i] = size & 1;
        s->type[i] = table[s->stored * 4 + i];
        size &= ~3UL;
        if (size > 0x1000000UL)              /* no frame is 16 MB: a damaged table */
            goto out;
        if (size > s->max_frame)
            s->max_frame = size;
        if (at + size < at)
            goto out;
        at += size;
    }
    s->offset[s->stored] = at;

    if (!(trees = io->alloc(io->ctx, trees_size ? trees_size : 1))
        || (trees_size && readat(io, SMK_HEAD + s->stored * 5, trees, trees_size)))
        goto out;
    bits_init(&b, trees, trees_size);
    if ((bit(&b) && big_read(io, &s->mmap, &b, sizes[0])) || (bit(&b) && big_read(io, &s->mclr, &b, sizes[1]))
        || (bit(&b) && big_read(io, &s->full, &b, sizes[2])) || (bit(&b) && big_read(io, &s->types, &b, sizes[3])))
        goto out;

    if (!(s->buf = io->alloc(io->ctx, s->max_frame ? s->max_frame : 4))
        || !(s->pixels = io->alloc(io->ctx, s->width * s->height)))
        goto out;
    memset(s->pixels, 0, s->width * s->height);
    rc = 0;
out:
    if (table)
        io->free(io->ctx, table);
    if (trees)
        io->free(io->ctx, trees);
    return rc;
}

unsigned smk_fps(const struct smk *s)
{
    unsigned f = (100000UL + s->fps_den / 2) / s->fps_den;
    return f ? f : 1;
}

/* 6-bit colour to 8 bits */
static uint8_t c6(unsigned v)
{
    v &= 0x3F;
    return (uint8_t)(v << 2 | v >> 4);
}

/* A palette change: entries kept, copied from the old palette, or new */
static void palette(struct smk *s, const uint8_t *p, uint32_t n)
{
    uint8_t old[256 * 3];
    uint32_t at = 0, sz = 0, j, off;
    memcpy(old, s->palette, sizeof old);
    while (sz < 256 && at < n) {
        unsigned t = p[at++];
        if (t & 0x80)
            sz += (t & 0x7F) + 1;
        else if (t & 0x40) {
            if (at >= n)
                break;
            off = p[at++];
            j = (t & 0x3F) + 1;
            if (off + j > 256)
                break;
            while (j-- && sz < 256) {
                memcpy(s->palette + sz * 3, old + off * 3, 3);
                sz++;
                off++;
            }
        } else {
            if (at + 2 > n)
                break;
            s->palette[sz * 3] = c6(t);
            s->palette[sz * 3 + 1] = c6(p[at]);
            s->palette[sz * 3 + 2] = c6(p[at + 1]);
            at += 2;
            sz++;
        }
    }
    s->palette_changed = 1;
}

/* Smacker's DPCM: the first samples whole, then differences through one
 * byte tree (8-bit) or two (16-bit, low and high) per channel */
static void audio_packed(const struct smk_track *t, const uint8_t *p, uint32_t n, void *pcm, uint32_t *bytes)
{
    struct bits b;
    struct small trees[4];
    uint32_t unp, i, count;
    int stereo, wide, k, ntrees;
    int pred[2] = { 0, 0 };

    *bytes = 0;
    if (n < 4)
        return;
    unp = le32(p);
    bits_init(&b, p + 4, n - 4);
    if (!bit(&b))
        return;
    stereo = bit(&b);
    wide = bit(&b);
    if (stereo != (t->channels == 2) || wide != (t->bits == 16))
        return;
    if (unp > t->max_bytes)
        unp = t->max_bytes;
    ntrees = 1 << (stereo + wide);
    for (k = 0; k < ntrees; k++) {
        bit(&b);
        if (small_read(&trees[k], &b))
            return;
        bit(&b);
    }
    if (wide) {
        int16_t *out = pcm;
        count = unp / 2;
        for (k = stereo; k >= 0; k--) {
            unsigned v = bitsn(&b, 16);
            pred[k] = (int16_t)(v >> 8 | (v & 0xFF) << 8);
        }
        for (i = 0; i <= (uint32_t)stereo && i < count; i++)
            out[i] = (int16_t)pred[i];
        for (; i < count; i++) {
            int c = (i & stereo) ? 1 : 0;
            unsigned v = small_get(&trees[c * 2], &b);
            v |= small_get(&trees[c * 2 + 1], &b) << 8;
            pred[c] = (int16_t)(pred[c] + (int16_t)v);
            out[i] = (int16_t)pred[c];
        }
        *bytes = i * 2;
    } else {
        uint8_t *out = pcm;
        count = unp;
        for (k = stereo; k >= 0; k--)
            pred[k] = bitsn(&b, 8);
        for (i = 0; i <= (uint32_t)stereo && i < count; i++)
            out[i] = (uint8_t)pred[i];
        for (; i < count; i++) {
            int c = (i & stereo) ? 1 : 0;
            pred[c] = (uint8_t)(pred[c] + (int8_t)small_get(&trees[c], &b));
            out[i] = (uint8_t)pred[c];
        }
        *bytes = i;
    }
}

static void audio_raw(const struct smk_track *t, const uint8_t *p, uint32_t n, void *pcm, uint32_t *bytes)
{
    uint32_t i;
    if (n > t->max_bytes)
        n = t->max_bytes;
    if (t->bits == 16) {
        int16_t *out = pcm;
        n &= ~1UL;
        for (i = 0; i < n / 2; i++)
            out[i] = (int16_t)(p[i * 2] | p[i * 2 + 1] << 8);
    } else
        memcpy(pcm, p, n);
    *bytes = n;
}

static uint32_t run_of(unsigned i)
{
    return i < 59 ? i + 1 : 128UL << (i - 59);
}

static void put16(uint8_t *o, unsigned v)
{
    o[0] = (uint8_t)v;
    o[1] = (uint8_t)(v >> 8);
}

static void video(struct smk *s, const uint8_t *p, uint32_t n)
{
    struct bits b;
    uint32_t bw = s->width >> 2, bh = s->height >> 2, blocks = bw * bh, blk = 0, stride = s->width;

    bits_init(&b, p, n);
    big_reset(&s->mmap);
    big_reset(&s->mclr);
    big_reset(&s->full);
    big_reset(&s->types);
    while (blk < blocks) {
        uint32_t type = big_get(&s->types, &b), run = run_of((type >> 2) & 0x3F), mode, i;
        uint8_t *o;
        switch (type & 3) {
        case BLK_MONO:
            while (run-- && blk < blocks) {
                uint32_t clr = big_get(&s->mclr, &b), map = big_get(&s->mmap, &b);
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                o = s->pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
                for (i = 0; i < 4; i++) {
                    o[0] = map & 1 ? hi : lo;
                    o[1] = map & 2 ? hi : lo;
                    o[2] = map & 4 ? hi : lo;
                    o[3] = map & 8 ? hi : lo;
                    map >>= 4;
                    o += stride;
                }
                blk++;
            }
            break;
        case BLK_FULL:
            mode = 0;
            if (s->version == 4) {
                if (bit(&b))
                    mode = 1;
                else if (bit(&b))
                    mode = 2;
            }
            while (run-- && blk < blocks) {
                o = s->pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
                if (mode == 0) {
                    for (i = 0; i < 4; i++) {
                        put16(o + 2, big_get(&s->full, &b));
                        put16(o, big_get(&s->full, &b));
                        o += stride;
                    }
                } else if (mode == 1) {
                    for (i = 0; i < 2; i++) {
                        uint32_t px = big_get(&s->full, &b);
                        o[0] = o[1] = o[stride] = o[stride + 1] = (uint8_t)px;
                        o[2] = o[3] = o[stride + 2] = o[stride + 3] = (uint8_t)(px >> 8);
                        o += stride * 2;
                    }
                } else {
                    for (i = 0; i < 2; i++) {
                        uint32_t px2 = big_get(&s->full, &b), px1 = big_get(&s->full, &b);
                        put16(o, px1);
                        put16(o + 2, px2);
                        put16(o + stride, px1);
                        put16(o + stride + 2, px2);
                        o += stride * 2;
                    }
                }
                blk++;
            }
            break;
        case BLK_SKIP:
            while (run-- && blk < blocks)
                blk++;
            break;
        default:
            while (run-- && blk < blocks) {
                uint8_t c = (uint8_t)(type >> 8);
                o = s->pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
                for (i = 0; i < 4; i++) {
                    memset(o, c, 4);
                    o += stride;
                }
                blk++;
            }
            break;
        }
    }
}

int smk_next(struct smk *s, int track, void *pcm, uint32_t *pcm_bytes)
{
    uint32_t i, size, at = 0, k;
    unsigned type;

    if (pcm_bytes)
        *pcm_bytes = 0;
    s->palette_changed = 0;
    if (s->next >= s->stored) {
        s->next = 0;
        memset(s->pixels, 0, s->width * s->height);
        memset(s->palette, 0, sizeof s->palette);
    }
    i = s->next++;
    size = s->offset[i + 1] - s->offset[i];
    if (size && readat(s->io, s->offset[i], s->buf, size))
        return -1;
    type = s->type[i];
    if (type & 1) {
        uint32_t n;
        if (!size)
            return -1;
        n = s->buf[0] * 4u;
        if (!n || n > size)
            return -1;
        palette(s, s->buf + 1, n - 1);
        at = n;
    }
    for (k = 0; k < SMK_TRACKS; k++) {
        uint32_t len;
        if (!(type & (2u << k)))
            continue;
        if (at + 4 > size || (len = le32(s->buf + at)) < 4 || len > size - at)
            return -1;
        if ((int)k == track && pcm && pcm_bytes && s->track[k].playable) {
            if (s->track[k].packed)
                audio_packed(&s->track[k], s->buf + at + 4, len - 4, pcm, pcm_bytes);
            else
                audio_raw(&s->track[k], s->buf + at + 4, len - 4, pcm, pcm_bytes);
        }
        at += len;
    }
    video(s, s->buf + at, size - at);
    return 0;
}

int smk_seek(struct smk *s, uint32_t i)
{
    if (i >= s->stored)
        return -1;
    if (i < s->next) {
        s->next = s->stored;            /* smk_next starts again at 0 */
        if (!i) {
            s->next = 0;
            memset(s->pixels, 0, s->width * s->height);
            memset(s->palette, 0, sizeof s->palette);
            return 0;
        }
    }
    while (s->next != i)
        if (smk_next(s, -1, NULL, NULL))
            return -1;
    return 0;
}
