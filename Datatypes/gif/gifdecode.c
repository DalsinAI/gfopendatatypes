/*
 * gfopengif.datatype's decoder (gifdecode.h). Written from the GIF89a
 * specification: the screen descriptor, the blocks after it (extensions,
 * image descriptors and the trailer), and variable-length LZW with its
 * clear and end codes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "gifdecode.h"

#define LZW_CODES 4096

/* The work area: the LZW tables, the string stack and the frame's palette. */
struct gif_work {
    ULONG palette[256];                  /* 1024 */
    UWORD prefix[LZW_CODES];             /* 8192 */
    UBYTE suffix[LZW_CODES];             /* 4096 */
    UBYTE stack[LZW_CODES + 4];          /* 4100 */
};

typedef char gif_work_fits[sizeof(struct gif_work) <= GIF_WORKSIZE ? 1 : -1];

static UWORD le16(const UBYTE *p)
{
    return (UWORD)(p[0] | p[1] << 8);
}

static int same(const UBYTE *a, const char *b, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; i++)
        if (a[i] != (UBYTE)b[i])
            return 0;
    return 1;
}

/* The offset after the data sub-blocks that start at pos (it may pass len). */
static ULONG skip_blocks(const UBYTE *file, ULONG len, ULONG pos)
{
    while (pos < len) {
        ULONG n = file[pos++];
        if (!n)
            break;
        pos += n;
    }
    return pos;
}

LONG gif_scan(const UBYTE *file, ULONG len, struct gif_info *g, struct gif_frame *frame, ULONG max)
{
    ULONG pos = 13, count = 0, global = 0, gsize = 0, right = 0, bottom = 0;
    UWORD delay = 0;
    UBYTE disposal = 0, transparent = 0, trans = 0;

    g->width = g->height = 0;
    g->loops = -1;
    g->frames = 0;
    g->restores = 0;
    if (len < 13 || !same(file, "GIF8", 4) || (file[4] != '7' && file[4] != '9') || file[5] != 'a')
        return -1;
    g->width = le16(file + 6);
    g->height = le16(file + 8);
    if (file[10] & 0x80) {
        global = 13;
        gsize = 2UL << (file[10] & 7);
        pos += 3 * gsize;
        if (pos > len)
            return -1;
    }
    while (pos < len) {
        UBYTE b = file[pos];
        if (b == 0x3b)                                  /* trailer */
            break;
        if (b == 0x21) {                                /* extension */
            UBYTE label;
            if (pos + 2 > len)
                break;
            label = file[pos + 1];
            pos += 2;
            if (label == 0xf9 && pos + 5 <= len && file[pos] >= 4) {   /* graphic control */
                UBYTE packed = file[pos + 1];
                delay = le16(file + pos + 2);
                trans = file[pos + 4];
                transparent = packed & 1;
                disposal = (packed >> 2) & 7;
                if (disposal > 3)
                    disposal = 0;
            } else if (label == 0xff && pos + 12 <= len && file[pos] == 11
                && (same(file + pos + 1, "NETSCAPE2.0", 11) || same(file + pos + 1, "ANIMEXTS1.0", 11))) {
                ULONG sub = pos + 12;
                if (g->loops < 0 && sub + 4 <= len && file[sub] >= 3 && (file[sub + 1] & 7) == 1)
                    g->loops = le16(file + sub + 2);
            }
            pos = skip_blocks(file, len, pos);
            continue;
        }
        if (b != 0x2c)                                  /* not a block: the file ends here */
            break;
        if (pos + 10 > len)
            break;
        {
            struct gif_frame f;
            UBYTE flags = file[pos + 9];
            f.x = le16(file + pos + 1);
            f.y = le16(file + pos + 3);
            f.w = le16(file + pos + 5);
            f.h = le16(file + pos + 7);
            f.interlaced = (flags & 0x40) ? 1 : 0;
            pos += 10;
            if (flags & 0x80) {
                f.palette = pos;
                f.colours = (UWORD)(2U << (flags & 7));
                pos += 3UL * f.colours;
                if (pos > len)
                    break;
            } else {
                f.palette = global;
                f.colours = (UWORD)gsize;
            }
            if (pos >= len)
                break;
            f.data = pos;
            f.delay = delay;
            f.disposal = disposal;
            f.transparent = transparent;
            f.trans = trans;
            if (f.disposal == 3)
                g->restores = 1;
            if ((ULONG)f.x + f.w > right)
                right = (ULONG)f.x + f.w;
            if ((ULONG)f.y + f.h > bottom)
                bottom = (ULONG)f.y + f.h;
            if (frame && count < max)
                frame[count] = f;
            count++;
            delay = 0;
            disposal = transparent = trans = 0;
            pos = skip_blocks(file, len, pos + 1);
        }
    }
    if (!count)
        return -1;
    if (!g->width || !g->height) {       /* no screen size: the frames' extent */
        g->width = right;
        g->height = bottom;
    }
    if (!g->width || !g->height || g->width > GIF_MAXSIDE || g->height > GIF_MAXSIDE
        || g->width * g->height > GIF_MAXPIXELS)
        return -1;
    g->frames = count;
    return (LONG)count;
}

/* The part of frame f on the screen: columns x0..x1, rows y0..y1 (ends
 * excluded). 0 when none of it is. */
static int clip(const struct gif_info *g, const struct gif_frame *f, ULONG *x0, ULONG *y0, ULONG *x1, ULONG *y1)
{
    *x0 = f->x;
    *y0 = f->y;
    *x1 = (ULONG)f->x + f->w;
    *y1 = (ULONG)f->y + f->h;
    if (*x1 > g->width)
        *x1 = g->width;
    if (*y1 > g->height)
        *y1 = g->height;
    return *x0 < *x1 && *y0 < *y1;
}

static void fill_rect(const struct gif_info *g, const struct gif_frame *f, ULONG *argb, ULONG value)
{
    ULONG x0, y0, x1, y1, x, y;
    if (!clip(g, f, &x0, &y0, &x1, &y1))
        return;
    for (y = y0; y < y1; y++) {
        ULONG *row = argb + y * g->width;
        for (x = x0; x < x1; x++)
            row[x] = value;
    }
}

static void copy_rect(const struct gif_info *g, const struct gif_frame *f, ULONG *to, const ULONG *from)
{
    ULONG x0, y0, x1, y1, x, y;
    if (!clip(g, f, &x0, &y0, &x1, &y1))
        return;
    for (y = y0; y < y1; y++) {
        ULONG at = y * g->width;
        for (x = x0; x < x1; x++)
            to[at + x] = from[at + x];
    }
}

/* Where the pixels go as they come out of the LZW stream. */
struct out {
    ULONG *argb;
    const ULONG *palette;
    ULONG width;                         /* the screen's */
    ULONG fx, fw, fh;                    /* the frame's left, width and height */
    ULONG x1, y1, fy;                    /* the screen's limits, the frame's top */
    ULONG col, row;                      /* in the frame */
    UWORD colours;
    UBYTE interlaced, pass;
    WORD trans;                          /* -1 none */
    ULONG left;                          /* pixels still wanted */
};

static void next_row(struct out *o)
{
    static const UBYTE start[4] = { 0, 4, 2, 1 }, step[4] = { 8, 8, 4, 2 };
    o->col = 0;
    if (!o->interlaced) {
        o->row++;
        return;
    }
    o->row += step[o->pass];
    while (o->row >= o->fh && o->pass < 3) {
        o->pass++;
        o->row = start[o->pass];
    }
}

static void put(struct out *o, UBYTE index)
{
    ULONG x = o->fx + o->col, y = o->fy + o->row;
    if (index < o->colours && (WORD)index != o->trans && x < o->x1 && y < o->y1)
        o->argb[y * o->width + x] = o->palette[index];
    if (++o->col == o->fw)
        next_row(o);
    o->left--;
}

/* Decodes frame f's LZW stream onto the screen. */
static void draw(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *f,
    ULONG *argb, struct gif_work *w)
{
    struct out o;
    ULONG pos = f->data, bits = 0, block = 0, i;
    ULONG acc = 0;
    UWORD clear, end, next, size, limit, oldcode = 0;
    UBYTE min, first = 0;
    int have_old = 0;

    if (!f->w || !f->h || !f->colours || pos >= len)
        return;
    min = file[pos++];
    if (min < 1 || min > 11)
        return;
    for (i = 0; i < f->colours; i++) {
        const UBYTE *c = file + f->palette + 3 * i;
        w->palette[i] = 0xff000000UL | (ULONG)c[0] << 16 | (ULONG)c[1] << 8 | c[2];
    }
    o.argb = argb;
    o.palette = w->palette;
    o.width = g->width;
    o.fx = f->x;
    o.fy = f->y;
    o.fw = f->w;
    o.fh = f->h;
    o.x1 = g->width;
    o.y1 = g->height;
    o.col = o.row = 0;
    o.colours = f->colours;
    o.interlaced = f->interlaced;
    o.pass = 0;
    o.trans = f->transparent ? f->trans : -1;
    o.left = (ULONG)f->w * f->h;

    clear = (UWORD)(1U << min);
    end = clear + 1;
    next = clear + 2;
    size = min + 1;
    limit = (UWORD)(1U << size);
    for (i = 0; i < clear; i++) {
        w->prefix[i] = 0;
        w->suffix[i] = (UBYTE)i;
    }
    while (o.left) {
        UWORD code, in;
        UBYTE *sp;
        while (bits < size) {                           /* the next byte, across sub-blocks */
            if (!block) {
                if (pos >= len || !(block = file[pos++]))
                    return;
            }
            if (pos >= len)
                return;
            acc |= (ULONG)file[pos++] << bits;
            bits += 8;
            block--;
        }
        code = (UWORD)(acc & (limit - 1));
        acc >>= size;
        bits -= size;
        if (code == clear) {
            next = clear + 2;
            size = min + 1;
            limit = (UWORD)(1U << size);
            have_old = 0;
            continue;
        }
        if (code == end)
            return;
        if (!have_old) {
            if (code >= clear)
                return;                                  /* damaged */
            put(&o, (UBYTE)code);
            first = (UBYTE)code;
            oldcode = code;
            have_old = 1;
            continue;
        }
        sp = w->stack;
        if (code < next)
            in = code;
        else if (code == next) {
            *sp++ = first;                               /* the code being made: old + its first */
            in = oldcode;
        } else
            return;                                      /* damaged */
        while (in >= clear) {
            *sp++ = w->suffix[in];
            in = w->prefix[in];
        }
        *sp++ = (UBYTE)in;
        first = (UBYTE)in;
        while (sp > w->stack && o.left)
            put(&o, *--sp);
        if (next < LZW_CODES) {
            w->prefix[next] = oldcode;
            w->suffix[next] = first;
            next++;
            if (next == limit && size < 12) {
                size++;
                limit = (UWORD)(1U << size);
            }
        }
        oldcode = code;
    }
}

LONG gif_show(const UBYTE *file, ULONG len, const struct gif_info *g, const struct gif_frame *frame,
    struct gif_canvas *c, ULONG index)
{
    ULONG n, count = g->width * g->height;

    if (index >= g->frames)
        return -1;
    if (c->shown >= 0 && index < (ULONG)c->shown)
        c->shown = -1;
    if (c->shown < 0) {
        for (n = 0; n < count; n++)
            c->argb[n] = 0;
    }
    while (c->shown < (LONG)index) {
        const struct gif_frame *f;
        if (c->shown >= 0) {                             /* the frame shown goes as it says */
            const struct gif_frame *before = frame + c->shown;
            if (before->disposal == 2)
                fill_rect(g, before, c->argb, 0);
            else if (before->disposal == 3 && c->saved)
                copy_rect(g, before, c->argb, c->saved);
        }
        f = frame + c->shown + 1;
        if (f->disposal == 3 && c->saved)
            copy_rect(g, f, c->saved, c->argb);
        draw(file, len, g, f, c->argb, (struct gif_work *)c->work);
        c->shown++;
    }
    return 0;
}

LONG gif_has_alpha(const struct gif_info *g, const struct gif_canvas *c)
{
    ULONG n, count = g->width * g->height;
    for (n = 0; n < count; n++)
        if (!(c->argb[n] & 0xff000000UL))
            return 1;
    return 0;
}
