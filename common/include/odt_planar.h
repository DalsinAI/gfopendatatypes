/* odt_planar.h: bitplanes to one byte a pixel, eight pixels at a time, for
 * the datatypes that read planar pictures: ilbm.datatype's BODY,
 * picture.datatype's V42 bitmaps and cdxl.datatype's frames.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
 *
 * Plain C (and C++), no Amiga headers, no tables, nothing to link: the
 * cores that include it still call nothing and keep no data. Eight plane
 * bytes are one 8 x 8 matrix of bits; turned over, its rows are the eight
 * pixels' values, so a row costs a few operations a byte rather than one
 * test for every bit of every pixel.
 */
#ifndef ODT_PLANAR_H
#define ODT_PLANAR_H

/* An 8 x 8 matrix of bits turned over. hi holds rows 0 to 3 (row 0 in its
 * top byte), lo rows 4 to 7; a row's top bit is column 0. Afterwards row j
 * holds what column j held, row 0's bit at the top. (unsigned int is 32
 * bits on the 68k and on the hosts the tests run on.) */
static inline void odt_turn8(unsigned int *hi, unsigned int *lo)
{
    unsigned int x = *hi, y = *lo, t;
    t = (x ^ (x >> 7)) & 0x00aa00aaU;
    x ^= t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00aa00aaU;
    y ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000ccccU;
    x ^= t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000ccccU;
    y ^= t ^ (t << 14);
    t = (x & 0xf0f0f0f0U) | ((y >> 4) & 0x0f0f0f0fU);
    *lo = ((x << 4) & 0xf0f0f0f0U) | (y & 0x0f0f0f0fU);
    *hi = t;
}

/* n pixels of one row of up to 8 bitplanes. rows[p] is plane p's row, its
 * first byte's top bit the first pixel; a NULL plane, or one past 8, reads
 * as 0. Each pixel's value (plane p's bit as bit p) goes to out, one byte
 * every step bytes. Reads (n + 7) / 8 bytes of each plane's row. */
static inline void odt_planar_row(const unsigned char *const *rows, unsigned depth, unsigned n, unsigned char *out,
                                  unsigned step)
{
    const unsigned char *r[8], *any = 0;
    unsigned int himask = 0, lomask = 0;
    unsigned b, p, i;
    if (depth > 8)
        depth = 8;
    /* a NULL plane reads another plane's row and its byte is masked off,
     * so the loop below has no test for it */
    for (p = 0; p < depth && !any; p++)
        any = rows[p];
    if (!any)
        depth = 0;
    for (p = 0; p < depth; p++) {
        r[p] = rows[p] ? rows[p] : any;
        if (rows[p] && p < 4)
            lomask |= 0xffU << (8 * p);
        else if (rows[p])
            himask |= 0xffU << (8 * (p - 4));
    }
    for (b = 0; b < n / 8 + (n & 7 ? 1 : 0); b++) {
        unsigned int hi = 0, lo = 0;
        /* the matrix's rows are the planes from 7 down to 0 */
        switch (depth) {
        case 8: hi |= (unsigned int)r[7][b] << 24; /* fall through */
        case 7: hi |= (unsigned int)r[6][b] << 16; /* fall through */
        case 6: hi |= (unsigned int)r[5][b] << 8;  /* fall through */
        case 5: hi |= r[4][b];                     /* fall through */
        case 4: lo |= (unsigned int)r[3][b] << 24; /* fall through */
        case 3: lo |= (unsigned int)r[2][b] << 16; /* fall through */
        case 2: lo |= (unsigned int)r[1][b] << 8;  /* fall through */
        case 1: lo |= r[0][b];                     /* fall through */
        default: break;
        }
        hi &= himask;
        lo &= lomask;
        odt_turn8(&hi, &lo);
        if (n - b * 8 >= 8) {
            out[0] = (unsigned char)(hi >> 24);
            out[step] = (unsigned char)(hi >> 16);
            out[2 * step] = (unsigned char)(hi >> 8);
            out[3 * step] = (unsigned char)hi;
            out[4 * step] = (unsigned char)(lo >> 24);
            out[5 * step] = (unsigned char)(lo >> 16);
            out[6 * step] = (unsigned char)(lo >> 8);
            out[7 * step] = (unsigned char)lo;
            out += 8 * step;
        } else {
            for (i = 0; i < n - b * 8; i++, out += step)
                *out = (unsigned char)(i < 4 ? hi >> (24 - 8 * i) : lo >> (56 - 8 * i));
        }
    }
}

#endif
