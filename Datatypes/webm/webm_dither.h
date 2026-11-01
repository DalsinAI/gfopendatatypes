/*
 * webm.datatype: a decoded picture (4:2:0, 4:4:4, or VP9's RGB) to 256
 * colours, the 6x6x6 colour cube with 4x4 ordered dithering. Shared by the
 * datatype and its tests, so both make the same pixels. Integer only (BT.601, limited range).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef WEBM_DITHER_H
#define WEBM_DITHER_H

#include "vpx/vpx_image.h"

typedef struct {
    short y[256], rv[256], gu[256], gv[256], bu[256];
} WebMDitherTables;

static void webm_dither_init(WebMDitherTables *t)
{
    int i;
    /* Coefficients scaled by 64 (8.6 fixed point). */
    for (i = 0; i < 256; i++) {
        t->y[i] = (short)((298 * (i - 16)) >> 2);
        t->rv[i] = (short)((409 * (i - 128)) >> 2);
        t->gu[i] = (short)((-100 * (i - 128)) >> 2);
        t->gv[i] = (short)((-208 * (i - 128)) >> 2);
        t->bu[i] = (short)((516 * (i - 128)) >> 2);
    }
}

static inline int webm_clamp8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* Colour index i < 216 is red (i / 36), green (i / 6 % 6), blue (i % 6),
 * each step 51; 216..255 are a grey ramp. out holds size bytes, rows pitch
 * bytes apart: only the pixels that are in the decoded picture (img->d_w x
 * img->d_h), in width x height and in those size bytes are written, so a
 * frame larger than its header said never writes past the buffer. */
static void webm_dither(const WebMDitherTables *t, const vpx_image_t *img, unsigned char *out, unsigned long size,
    unsigned width, unsigned height, unsigned pitch)
{
    static const unsigned char bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    unsigned x, y;
    if (img->d_w < width)
        width = img->d_w;
    if (img->d_h < height)
        height = img->d_h;
    if (width > pitch)
        width = pitch;
    if (!width || size < width)
        return;
    if (height > (size - width) / pitch + 1)
        height = (unsigned)((size - width) / pitch + 1);
    for (y = 0; y < height; y++) {
        const unsigned char *py = img->planes[VPX_PLANE_Y] + y * img->stride[VPX_PLANE_Y];
        const unsigned char *pu = img->planes[VPX_PLANE_U] + (y >> img->y_chroma_shift) * img->stride[VPX_PLANE_U];
        const unsigned char *pv = img->planes[VPX_PLANE_V] + (y >> img->y_chroma_shift) * img->stride[VPX_PLANE_V];
        const unsigned char *dither = bayer[y & 3];
        unsigned char *row = out + y * pitch;
        if (img->cs == VPX_CS_SRGB) {
            /* VP9 from RGB (ffmpeg's gbrp): the planes are G, B and R. */
            for (x = 0; x < width; x++) {
                int g = py[x], b = pu[x >> img->x_chroma_shift], r = pv[x >> img->x_chroma_shift];
                int d = dither[x & 3] * 16;
                row[x] = (unsigned char)(((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8));
            }
            continue;
        }
        for (x = 0; x < width; x++) {
            int luma = t->y[py[x]];
            int u = pu[x >> img->x_chroma_shift], v = pv[x >> img->x_chroma_shift];
            int r = webm_clamp8((luma + t->rv[v] + 32) >> 6);
            int g = webm_clamp8((luma + t->gu[u] + t->gv[v] + 32) >> 6);
            int b = webm_clamp8((luma + t->bu[u] + 32) >> 6);
            int d = dither[x & 3] * 16;
            row[x] = (unsigned char)(((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8));
        }
    }
}

#endif
