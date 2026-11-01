/*
 * gfopentga.datatype's encoder (tgaencode.h).
 *
 * The header (18 bytes): no image ID, no colour map, image type 2 or 10,
 * origin 0,0, width and height (little-endian, as every TGA number is),
 * 24 or 32 bits a pixel, and the image descriptor: the alpha bits (0 or 8)
 * and bit 5, top row first. Each pixel is B, G, R and, at 32 bits, A.
 *
 * Run-length encoding (type 10): a packet's first byte is its count less
 * one in bits 0-6 (1 to 128 pixels), with bit 7 set for a run packet (one
 * pixel, repeated) and clear for a raw packet (that many pixels). A run is
 * taken for two or more equal pixels; packets end at the end of each row.
 *
 * The footer (26 bytes): extension area offset 0, developer directory
 * offset 0, and "TRUEVISION-XFILE." with its NUL.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include "tgaencode.h"

#define HEAD 18UL
#define FOOT 26UL

static void put16(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)v;
    p[1] = (UBYTE)(v >> 8);
}

/* One pixel, B G R (A), from A R G B */
static UBYTE *pixel(UBYTE *o, const UBYTE *p, ULONG bpp)
{
    o[0] = p[3];
    o[1] = p[2];
    o[2] = p[1];
    if (bpp == 4)
        o[3] = p[0];
    return o + bpp;
}

/* Two pixels the file would hold the same: at 24 bits the alpha is not written */
static int same(const UBYTE *a, const UBYTE *b, ULONG bpp)
{
    return a[1] == b[1] && a[2] == b[2] && a[3] == b[3] && (bpp == 3 || a[0] == b[0]);
}

static UBYTE *rle_row(UBYTE *o, const UBYTE *row, ULONG w, ULONG bpp)
{
    ULONG x = 0;
    while (x < w) {
        ULONG n = 1;
        while (x + n < w && n < 128 && same(row + 4 * (x + n), row + 4 * x, bpp))
            n++;
        if (n >= 2) {                      /* a run packet */
            *o++ = (UBYTE)(0x80 | (n - 1));
            o = pixel(o, row + 4 * x, bpp);
            x += n;
            continue;
        }
        /* a raw packet: up to where the next run of two starts */
        n = 1;
        while (x + n < w && n < 128
               && !(x + n + 1 < w && same(row + 4 * (x + n), row + 4 * (x + n + 1), bpp)))
            n++;
        *o++ = (UBYTE)(n - 1);
        while (n--) {
            o = pixel(o, row + 4 * x, bpp);
            x++;
        }
    }
    return o;
}

LONG tga_encode(struct tgae_env *env, const UBYTE *argb, ULONG width, ULONG height, ULONG stride,
                ULONG flags, UBYTE **tga, ULONG *size)
{
    ULONG bpp = 3, x, y, cap, per_row;
    UBYTE *buf, *o;

    *tga = 0;
    *size = 0;
    if (!argb || !width || !height || width > 65535UL || height > 65535UL || stride < width * 4)
        return TGAE_ERR_ARGS;
    if (!(flags & TGAE_OPAQUE)) {
        for (y = 0; y < height && bpp == 3; y++) {
            const UBYTE *row = argb + y * stride;
            for (x = 0; x < width; x++)
                if (row[4 * x] != 0xFF) {
                    bpp = 4;
                    break;
                }
        }
    }
    /* the most the pixels can take: raw packets of one pixel each are the worst case */
    per_row = width * bpp + ((flags & TGAE_RAW) ? 0 : width);
    if (per_row > (0xFFFFFFFFUL - HEAD - FOOT) / height)   /* more than 32 bits can count */
        return TGAE_ERR_ARGS;
    cap = HEAD + height * per_row + FOOT;
    if (!(buf = env->alloc(env, cap)))
        return TGAE_ERR_MEMORY;

    o = buf;
    *o++ = 0;                                /* no image ID */
    *o++ = 0;                                /* no colour map */
    *o++ = (flags & TGAE_RAW) ? 2 : 10;
    for (x = 0; x < 5; x++)                  /* colour map specification */
        *o++ = 0;
    put16(o, 0);                             /* x origin */
    put16(o + 2, 0);                         /* y origin */
    put16(o + 4, width);
    put16(o + 6, height);
    o += 8;
    *o++ = (UBYTE)(bpp * 8);
    *o++ = (UBYTE)(0x20 | (bpp == 4 ? 8 : 0)); /* top row first; the alpha bits */

    for (y = 0; y < height; y++) {
        const UBYTE *row = argb + y * stride;
        if (flags & TGAE_RAW) {
            for (x = 0; x < width; x++)
                o = pixel(o, row + 4 * x, bpp);
        } else
            o = rle_row(o, row, width, bpp);
    }

    for (x = 0; x < 8; x++)                  /* no extension area, no developer directory */
        *o++ = 0;
    {
        static const char sig[] = "TRUEVISION-XFILE.";
        for (x = 0; x < sizeof sig; x++)     /* with its NUL */
            *o++ = (UBYTE)sig[x];
    }
    *tga = buf;
    *size = (ULONG)(o - buf);
    return 0;
}
