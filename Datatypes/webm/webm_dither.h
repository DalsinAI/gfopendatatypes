/*
 * webm.datatype: a decoded picture (4:2:0, 4:4:4, or VP9's RGB) to 256
 * colours, the 6x6x6 colour cube with 4x4 ordered dithering. Shared by the
 * datatype and its tests, so both make the same pixels. Integer only
 * (BT.601, limited range).
 *
 * A pixel is, as it always was:
 *   r = clamp((Y[y] + RV[v] + 32) >> 6), g = clamp((Y[y] + GU[u] + GV[v] + 32) >> 6),
 *   b = clamp((Y[y] + BU[u] + 32) >> 6)            (each 0 to 255)
 *   index = ((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8)
 * with d the 4x4 Bayer value times 16. The tables (webm_dither_tables.h,
 * written by common/mkcubedither.py, const) hold each channel's part of the
 * index, clamp and all, for every sum before the clamp: three lookups and
 * two adds a pixel, the chroma once for both pixels of a 4:2:0 pair, four
 * pixels a turn (one table row each). tests/test-webm-dither.sh checks it
 * against the formula on all 2^24 colours with each of the 16 dither
 * values, and pixel for pixel on whole pictures.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WEBM_DITHER_H
#define WEBM_DITHER_H

#include "vpx/vpx_image.h"
#include "webm_dither_tables.h"

static const unsigned char webm_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* One pixel: luma l (webm_y), chroma sums r, g, b (each with the 32 that
 * rounds), through dither row t */
#define WEBM_PIXEL(t, l, r, g, b)                                                                                     \
    (unsigned char)((t)[WEBM_CUBE_R + (((l) + (r)) >> 6)] + (t)[WEBM_CUBE_G + (((l) + (g)) >> 6)] +                  \
                    (t)[WEBM_CUBE_B + (((l) + (b)) >> 6)])

/* Colour index i < 216 is red (i / 36), green (i / 6 % 6), blue (i % 6),
 * each step 51; 216..255 are a grey ramp. out holds size bytes, rows pitch
 * bytes apart: only the pixels that are in the decoded picture (img->d_w x
 * img->d_h), in width x height and in those size bytes are written, so a
 * frame larger than its header said never writes past the buffer. */
static void webm_dither(const vpx_image_t *img, unsigned char *out, unsigned long size, unsigned width,
    unsigned height, unsigned pitch)
{
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
        const unsigned char *bay = webm_bayer[y & 3];
        const unsigned char *t0 = webm_cube[bay[0]], *t1 = webm_cube[bay[1]], *t2 = webm_cube[bay[2]];
        const unsigned char *t3 = webm_cube[bay[3]];
        unsigned char *row = out + y * pitch;
        if (img->cs == VPX_CS_SRGB) {
            /* VP9 from RGB (ffmpeg's gbrp): the planes are G, B and R, and
             * each value is its own sum (0 to 255) */
            const unsigned sh = img->x_chroma_shift;
            for (x = 0; x < width; x++) {
                const unsigned char *t = webm_cube[bay[x & 3]];
                row[x] = (unsigned char)(t[WEBM_CUBE_R + pv[x >> sh]] + t[WEBM_CUBE_G + py[x]] +
                                         t[WEBM_CUBE_B + pu[x >> sh]]);
            }
            continue;
        }
        if (img->x_chroma_shift == 1) {
            /* 4:2:0 and 4:2:2: a chroma pair for two pixels */
            for (x = 0; x + 4 <= width; x += 4, py += 4, row += 4) {
                int u = *pu++, v = *pv++, r = webm_rv[v] + 32, g = webm_gu[u] + webm_gv[v] + 32, b = webm_bu[u] + 32;
                int l0 = webm_y[py[0]], l1 = webm_y[py[1]];
                row[0] = WEBM_PIXEL(t0, l0, r, g, b);
                row[1] = WEBM_PIXEL(t1, l1, r, g, b);
                u = *pu++;
                v = *pv++;
                r = webm_rv[v] + 32;
                g = webm_gu[u] + webm_gv[v] + 32;
                b = webm_bu[u] + 32;
                l0 = webm_y[py[2]];
                l1 = webm_y[py[3]];
                row[2] = WEBM_PIXEL(t2, l0, r, g, b);
                row[3] = WEBM_PIXEL(t3, l1, r, g, b);
            }
            for (; x < width; x++, py++, row++) {
                int u = pu[(x & 3) >> 1], v = pv[(x & 3) >> 1], l = webm_y[*py];
                const unsigned char *t = webm_cube[bay[x & 3]];
                *row = WEBM_PIXEL(t, l, webm_rv[v] + 32, webm_gu[u] + webm_gv[v] + 32, webm_bu[u] + 32);
            }
            continue;
        }
        for (x = 0; x < width; x++) {
            const unsigned sh = img->x_chroma_shift;
            int u = pu[x >> sh], v = pv[x >> sh], l = webm_y[py[x]];
            const unsigned char *t = webm_cube[bay[x & 3]];
            row[x] = WEBM_PIXEL(t, l, webm_rv[v] + 32, webm_gu[u] + webm_gv[v] + 32, webm_bu[u] + 32);
        }
    }
}

#endif
