/*
 * gfopentga.datatype's decoder (tgadecode.h): Targa pictures to ARGB, from
 * Truevision's TGA File Format Specification 2.0.
 *
 * The file: an 18-byte header (little-endian words), the image ID, the
 * colour map, then the pixels, left to right from the bottom row unless the
 * descriptor's bits 4 and 5 say otherwise. An RLE picture's pixels come in
 * packets: a byte whose top bit says a run (one pixel, repeated) or raw
 * (that many pixels), and whose low seven bits are the count less one.
 * A TGA 2.0 file ends with a 26-byte footer ("TRUEVISION-XFILE.") that
 * points to an extension area; its byte 494 says what the alpha is.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "tgadecode.h"

#define LE16(p) ((ULONG)(p)[0] | ((ULONG)(p)[1] << 8))
#define LE32(p) (LE16(p) | (LE16((p) + 2) << 16))

#define OPAQUE_BLACK 0xFF000000UL
#define EXT_SIZE     495                 /* a TGA 2.0 extension area */
#define EXT_ATTR     494                 /* its attributes type */
#define FOOTER       26

static const UBYTE signature[18] = { 'T','R','U','E','V','I','S','I','O','N','-','X','F','I','L','E','.',0 };

/* The attributes type in a TGA 2.0 extension area, or -1 when there is none. */
static LONG ext_attributes(const UBYTE *f, ULONG len, ULONG data_off)
{
    ULONG ext, i;
    if (len < data_off + FOOTER)
        return -1;
    for (i = 0; i < sizeof(signature); i++)
        if (f[len - sizeof(signature) + i] != signature[i])
            return -1;
    ext = LE32(f + len - FOOTER);
    if (!ext || ext < data_off || ext > len - FOOTER || len - FOOTER - ext < EXT_SIZE || LE16(f + ext) < EXT_SIZE)
        return -1;
    return f[ext + EXT_ATTR];
}

LONG tga_read_head(const UBYTE *f, ULONG len, struct tga_head *h)
{
    ULONG idlen, cmtype, type, map_bytes = 0, bits;
    UBYTE desc;
    LONG attr;

    if (len < 18)
        return -1;
    idlen = f[0];
    cmtype = f[1];
    type = f[2];
    h->map_first = (UWORD)LE16(f + 3);
    h->map_len = (UWORD)LE16(f + 5);
    h->map_depth = f[7];
    h->width = LE16(f + 12);
    h->height = LE16(f + 14);
    h->depth = f[16];
    desc = f[17];
    h->alpha_bits = desc & 15;
    h->right_left = (desc >> 4) & 1;
    h->top_down = (desc >> 5) & 1;
    h->rle = type >= 9;
    h->type = (UBYTE)(h->rle ? type - 8 : type);
    h->alpha = h->premultiplied = 0;
    if (cmtype > 1 || (desc & 0xC0))    /* no colour map kinds past 1; no interleaving (never used) */
        return -1;
    if (type < 1 || (type > 3 && type < 9) || type > 11)
        return -1;                       /* 0 (no picture) and the Huffman kinds, 32 and 33 */
    if (!h->width || !h->height || h->width * h->height > TGA_MAXPIXELS)
        return -1;
    if (cmtype) {
        if (h->map_depth != 15 && h->map_depth != 16 && h->map_depth != 24 && h->map_depth != 32)
            return -1;
        map_bytes = (ULONG)h->map_len * ((h->map_depth + 7) >> 3);
    }
    switch (h->type) {
    case 1:                              /* colour-mapped: 8 or 16-bit indexes */
        if (!cmtype || !h->map_len || (h->depth != 8 && h->depth != 16))
            return -1;
        bits = h->map_depth;
        break;
    case 2:                              /* true-colour (a colour map there is skipped) */
        if (h->depth != 15 && h->depth != 16 && h->depth != 24 && h->depth != 32)
            return -1;
        bits = h->depth;
        break;
    default:                             /* greyscale, with alpha at 16 bits */
        if (h->depth != 8 && h->depth != 16)
            return -1;
        bits = h->depth;
        break;
    }
    h->map_off = 18 + idlen;
    h->data_off = h->map_off + map_bytes;
    if (h->data_off > len)
        return -1;
    /* Alpha: a 32-bit value's fourth byte, a 16-bit one's top bit, a 16-bit
     * grey's second byte, when the descriptor gives attribute bits. */
    if (bits == 32 || bits == 16)
        h->alpha = h->alpha_bits ? 1 : 0;
    attr = ext_attributes(f, len, h->data_off);
    if (attr >= 0 && attr <= 2)          /* none, undefined (ignore), undefined (keep) */
        h->alpha = 0;
    else if ((attr == 3 || attr == 4) && (bits == 32 || bits == 16)) {
        h->alpha = 1;                    /* useful alpha; 4: premultiplied */
        h->premultiplied = attr == 4;
    }
    return 0;
}

/* A stored colour (bits 15, 16, 24 or 32, as in the file) to ARGB. */
static ULONG colour(const UBYTE *p, UBYTE bits, UBYTE alpha)
{
    ULONG v, r, g, b, a;
    if (bits == 24)
        return OPAQUE_BLACK | ((ULONG)p[2] << 16) | ((ULONG)p[1] << 8) | p[0];
    if (bits == 32)
        return ((alpha ? (ULONG)p[3] : 255UL) << 24) | ((ULONG)p[2] << 16) | ((ULONG)p[1] << 8) | p[0];
    v = LE16(p);                         /* 15 or 16 bits: A1 R5 G5 B5 */
    r = (v >> 10) & 31;
    g = (v >> 5) & 31;
    b = v & 31;
    a = (alpha && bits == 16) ? ((v & 0x8000) ? 255 : 0) : 255;
    return (a << 24) | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

/* One pixel as stored to ARGB. */
static ULONG pixel(const struct tga_head *h, const UBYTE *p, const ULONG *pal)
{
    ULONG i;
    switch (h->type) {
    case 1:
        i = h->depth == 8 ? p[0] : LE16(p);
        i -= h->map_first;               /* the map's first entry is index map_first */
        return i < h->map_len ? pal[i] : OPAQUE_BLACK;
    case 2:
        return colour(p, h->depth, h->alpha);
    default:
        return ((h->depth == 16 && h->alpha ? (ULONG)p[1] : 255UL) << 24) | ((ULONG)p[0] * 0x010101UL);
    }
}

LONG tga_decode(const struct tga_head *h, const UBYTE *f, ULONG len, ULONG *argb, ULONG *pal, UBYTE *alpha)
{
    const UBYTE *p = f + h->data_off, *end = f + len;
    ULONG bpp = (h->depth + 7) >> 3, w = h->width, n = h->width * h->height;
    ULONG x, y, i, run = 0, raw = 0, value = 0, seen;
    BOOL short_file = FALSE;

    if (h->data_off > len)
        return -1;
    if (h->type == 1) {
        ULONG mb = (h->map_depth + 7) >> 3;
        const UBYTE *m = f + h->map_off;
        if (!pal || h->map_off + (ULONG)h->map_len * mb > len)
            return -1;
        for (i = 0; i < h->map_len; i++, m += mb)
            pal[i] = colour(m, h->map_depth, h->alpha);
    }
    for (y = 0; y < h->height; y++) {
        ULONG *q = argb + (h->top_down ? y : h->height - 1 - y) * w;
        LONG step = 1;
        if (h->right_left) {
            q += w - 1;
            step = -1;
        }
        for (x = 0; x < w; x++, q += step) {
            if (short_file) {
                *q = OPAQUE_BLACK;       /* past the end of a short file */
                continue;
            }
            if (h->rle && !run && !raw) {
                if (p >= end) {
                    short_file = TRUE;
                    *q = OPAQUE_BLACK;
                    continue;
                }
                if (*p & 0x80) {         /* a run: one pixel, count times */
                    run = (*p++ & 0x7F) + 1;
                    if ((ULONG)(end - p) < bpp) {
                        short_file = TRUE;
                        *q = OPAQUE_BLACK;
                        continue;
                    }
                    value = pixel(h, p, pal);
                    p += bpp;
                } else                   /* raw: count pixels follow */
                    raw = (*p++ & 0x7F) + 1;
            }
            if (run) {
                *q = value;
                run--;
                continue;
            }
            if ((ULONG)(end - p) < bpp) {
                short_file = TRUE;
                *q = OPAQUE_BLACK;
                continue;
            }
            *q = pixel(h, p, pal);
            p += bpp;
            if (raw)
                raw--;
        }
    }
    *alpha = h->alpha;
    if (!h->alpha)
        return 0;
    for (i = 0, seen = 0; i < n; i++)
        seen |= argb[i];
    if (!(seen & 0xFF000000UL)) {        /* alpha 0 everywhere: an empty channel, not a clear picture */
        for (i = 0; i < n; i++)
            argb[i] |= OPAQUE_BLACK;
        *alpha = 0;
    } else if (h->premultiplied) {
        for (i = 0; i < n; i++) {
            ULONG a = argb[i] >> 24, r, g, b;
            if (!a || a == 255)
                continue;
            r = ((argb[i] >> 16) & 255) * 255 / a;
            g = ((argb[i] >> 8) & 255) * 255 / a;
            b = (argb[i] & 255) * 255 / a;
            argb[i] = (a << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
        }
    }
    return 0;
}
