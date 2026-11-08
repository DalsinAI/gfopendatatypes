/*
 * webm.datatype's dither (webm/webm_dither.h, its tables const in
 * webm/webm_dither_tables.h) against the formula it replaced, and CDXL's
 * chunky films (cdxl_rgb_to_cube) likewise (tests/test-webm-dither.sh):
 *
 *   webmdithertest all      every colour: the YUV pixel for all 2^24 (Y, U,
 *                           V) with each of the 16 dither values, the sRGB
 *                           pixel for all 2^24 (R, G, B) with each, and
 *                           CDXL's RGB pixel likewise
 *   webmdithertest pictures N SEED
 *                           N random pictures (4:2:0, 4:2:2, 4:4:4, 4:4:0,
 *                           sRGB; odd sizes, strides, pitches, buffers that
 *                           cut rows off), each dithered by both: every byte
 *                           of the buffer the same, written or not
 *   webmdithertest bench OLD|NEW W H REPS
 *                           a W x H 4:2:0 picture REPS times (instruction
 *                           counts under qemu-m68k); benchcdxl: a W x H
 *                           CDXL chunky RGB frame
 *
 * The old code is here as it was (webm_dither_old, cdxl_cube_old).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webm_dither.h"
#include "cdxl_core.h"

/* ---- as it was ---------------------------------------------------------------- */

typedef struct {
    short y[256], rv[256], gu[256], gv[256], bu[256];
} OldTables;

static OldTables old;

static void old_init(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        old.y[i] = (short)((298 * (i - 16)) >> 2);
        old.rv[i] = (short)((409 * (i - 128)) >> 2);
        old.gu[i] = (short)((-100 * (i - 128)) >> 2);
        old.gv[i] = (short)((-208 * (i - 128)) >> 2);
        old.bu[i] = (short)((516 * (i - 128)) >> 2);
    }
}

static inline int old_clamp8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static inline unsigned char old_yuv(int yy, int u, int v, int d)
{
    int luma = old.y[yy];
    int r = old_clamp8((luma + old.rv[v] + 32) >> 6);
    int g = old_clamp8((luma + old.gu[u] + old.gv[v] + 32) >> 6);
    int b = old_clamp8((luma + old.bu[u] + 32) >> 6);
    return (unsigned char)(((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8));
}

static void webm_dither_old(const vpx_image_t *img, unsigned char *out, unsigned long size, unsigned width,
    unsigned height, unsigned pitch)
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
            for (x = 0; x < width; x++) {
                int g = py[x], b = pu[x >> img->x_chroma_shift], r = pv[x >> img->x_chroma_shift];
                int d = dither[x & 3] * 16;
                row[x] = (unsigned char)(((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8));
            }
            continue;
        }
        for (x = 0; x < width; x++) {
            int luma = old.y[py[x]];
            int u = pu[x >> img->x_chroma_shift], v = pv[x >> img->x_chroma_shift];
            int r = old_clamp8((luma + old.rv[v] + 32) >> 6);
            int g = old_clamp8((luma + old.gu[u] + old.gv[v] + 32) >> 6);
            int b = old_clamp8((luma + old.bu[u] + 32) >> 6);
            int d = dither[x & 3] * 16;
            row[x] = (unsigned char)(((r * 5 + d) >> 8) * 36 + ((g * 5 + d) >> 8) * 6 + ((b * 5 + d) >> 8));
        }
    }
}

static void cdxl_cube_old(const uint8_t *frame, const struct cdxl_frame *f, uint8_t *out)
{
    static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    const uint8_t *s = frame + cdxl_image_at(f);
    unsigned x, y;
    for (y = 0; y < f->height; y++)
        for (x = 0; x < f->width; x++, s += 3) {
            unsigned d = bayer[y & 3][x & 3] * 16;
            *out++ = (uint8_t)(((s[0] * 5 + d) >> 8) * 36 + ((s[1] * 5 + d) >> 8) * 6 + ((s[2] * 5 + d) >> 8));
        }
}

/* ---- every colour ------------------------------------------------------------- */

static int all_colours(void)
{
    unsigned long differ = 0, n = 0;
    int d, a, b, c;
    for (d = 0; d < 16; d++) {
        const unsigned char *t = webm_cube[d];
        for (a = 0; a < 256; a++)
            for (b = 0; b < 256; b++) {
                int l = webm_y[a];
                for (c = 0; c < 256; c++) {
                    /* (Y, U, V) = (a, b, c) */
                    unsigned char want = old_yuv(a, b, c, d * 16);
                    unsigned char got = WEBM_PIXEL(t, l, webm_rv[c] + 32, webm_gu[b] + webm_gv[c] + 32, webm_bu[b] + 32);
                    /* sRGB (R, G, B) = (a, b, c), and CDXL's RGB */
                    unsigned char want2 = (unsigned char)(((a * 5 + d * 16) >> 8) * 36 + ((b * 5 + d * 16) >> 8) * 6 +
                                                          ((c * 5 + d * 16) >> 8));
                    unsigned char got2 = (unsigned char)(t[WEBM_CUBE_R + a] + t[WEBM_CUBE_G + b] + t[WEBM_CUBE_B + c]);
                    unsigned char got3 = cdxl_cube_pixel((unsigned)d, (unsigned)a, (unsigned)b, (unsigned)c);
                    if (want != got || want2 != got2 || want2 != got3) {
                        if (differ++ < 5)
                            printf("differs: dither %d, %d %d %d: yuv %u/%u rgb %u/%u/%u\n", d, a, b, c, want, got, want2,
                                   got2, got3);
                    }
                    n++;
                }
            }
    }
    printf("every colour: %lu YUV, %lu sRGB and %lu CDXL RGB pixels (2^24 colours x 16 dither values), %lu differ\n",
           n, n, n, differ);
    return differ != 0;
}

/* ---- whole pictures ------------------------------------------------------------- */

static unsigned long seed;
static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)((seed >> 16) & 0x7fff) % n;
}

static vpx_image_t *picture(unsigned w, unsigned h, unsigned kind)
{
    vpx_image_t *img = calloc(1, sizeof *img);
    unsigned p, i;
    static const unsigned shx[5] = { 1, 1, 0, 0, 0 }, shy[5] = { 1, 0, 0, 1, 0 };
    img->cs = kind == 4 ? VPX_CS_SRGB : VPX_CS_BT_601;
    img->d_w = w;
    img->d_h = h;
    img->x_chroma_shift = shx[kind];
    img->y_chroma_shift = shy[kind];
    for (p = 0; p < 3; p++) {
        unsigned pw = p ? (w + img->x_chroma_shift) >> img->x_chroma_shift : w;
        unsigned ph = p ? (h + img->y_chroma_shift) >> img->y_chroma_shift : h;
        img->stride[p] = (int)(pw + rnd(5));
        img->planes[p] = malloc((size_t)img->stride[p] * ph + 1);
        for (i = 0; i < (unsigned)img->stride[p] * ph + 1; i++)
            /* now and then the extremes, where the clamps are */
            img->planes[p][i] = (unsigned char)(rnd(6) ? rnd(256) : rnd(2) * 255);
    }
    return img;
}

static int pictures(unsigned long n, unsigned long s)
{
    unsigned long k, differ = 0, pixels = 0;
    seed = s;
    for (k = 0; k < n; k++) {
        unsigned kind = rnd(5), w = 1 + rnd(k & 1 ? 200 : 23), h = 1 + rnd(13);
        vpx_image_t *img = picture(w, h, kind);
        unsigned width = rnd(4) ? w : 1 + rnd(w + 8), height = rnd(4) ? h : 1 + rnd(h + 4);
        unsigned pitch = width + (rnd(3) ? 0 : rnd(9));
        unsigned long size = (unsigned long)pitch * height + 3, i;
        unsigned char *a, *b;
        if (!rnd(6)) size = rnd((unsigned)size + 1);   /* a buffer that cuts rows off */
        a = malloc(size + 1);
        b = malloc(size + 1);
        for (i = 0; i <= size; i++) a[i] = b[i] = (unsigned char)(0xEE ^ (i & 1));
        webm_dither_old(img, a, size, width, height, pitch);
        webm_dither(img, b, size, width, height, pitch);
        if (memcmp(a, b, size + 1) && differ++ < 5)
            printf("picture %lu differs: %ux%u kind %u as %ux%u pitch %u size %lu\n", k, w, h, kind, width, height,
                   pitch, size);
        pixels += (unsigned long)w * h;
        for (i = 0; i < 3; i++) free(img->planes[i]);
        free(img);
        free(a);
        free(b);
    }
    /* CDXL's chunky films */
    for (k = 0; k < n / 4; k++) {
        struct cdxl_frame f;
        uint8_t *frame, *a, *b;
        unsigned long i, len;
        memset(&f, 0, sizeof f);
        f.width = (uint16_t)(1 + rnd(k & 1 ? 150 : 9));
        f.height = (uint16_t)(1 + rnd(11));
        f.planes = 24;
        f.layout = CDXL_CHUNKY;
        len = cdxl_image_at(&f) + (unsigned long)f.width * f.height * 3;
        frame = malloc(len);
        for (i = 0; i < len; i++) frame[i] = (uint8_t)(rnd(6) ? rnd(256) : rnd(2) * 255);
        a = malloc((size_t)f.width * f.height);
        b = malloc((size_t)f.width * f.height);
        cdxl_cube_old(frame, &f, a);
        cdxl_rgb_to_cube(frame, &f, b);
        if (memcmp(a, b, (size_t)f.width * f.height) && differ++ < 5)
            printf("cdxl picture %lu differs: %ux%u\n", k, f.width, f.height);
        pixels += (unsigned long)f.width * f.height;
        free(frame);
        free(a);
        free(b);
    }
    printf("%lu pictures and %lu CDXL frames (%lu pixels): %lu differ from the old code\n", n, n / 4, pixels, differ);
    return differ != 0;
}

int main(int argc, char **argv)
{
    old_init();
    if (argc > 1 && !strcmp(argv[1], "all"))
        return all_colours();
    if (argc > 3 && !strcmp(argv[1], "pictures"))
        return pictures(strtoul(argv[2], 0, 10), strtoul(argv[3], 0, 10));
    if (argc > 5 && !strcmp(argv[1], "bench")) {
        unsigned w = (unsigned)atoi(argv[3]), h = (unsigned)atoi(argv[4]), r, reps = (unsigned)atoi(argv[5]), i;
        unsigned long sum = 0;
        vpx_image_t *img;
        unsigned char *out = malloc((size_t)w * h);
        seed = 2026;
        img = picture(w, h, 0);
        for (r = 0; r < reps; r++) {
            if (argv[2][0] == 'O')
                webm_dither_old(img, out, (unsigned long)w * h, w, h, w);
            else
                webm_dither(img, out, (unsigned long)w * h, w, h, w);
        }
        for (i = 0; i < w * h; i++) sum = sum * 31 + out[i];
        printf("%s %08lx\n", argv[2], sum & 0xffffffffUL);
        return 0;
    }
    if (argc > 5 && !strcmp(argv[1], "benchcdxl")) {
        struct cdxl_frame f;
        unsigned r, reps = (unsigned)atoi(argv[5]), i;
        unsigned long sum = 0, len;
        uint8_t *frame, *out;
        memset(&f, 0, sizeof f);
        f.width = (uint16_t)atoi(argv[3]);
        f.height = (uint16_t)atoi(argv[4]);
        f.planes = 24;
        f.layout = CDXL_CHUNKY;
        len = cdxl_image_at(&f) + (unsigned long)f.width * f.height * 3;
        frame = malloc(len);
        out = malloc((size_t)f.width * f.height);
        seed = 2026;
        for (i = 0; i < len; i++) frame[i] = (uint8_t)rnd(256);
        for (r = 0; r < reps; r++) {
            if (argv[2][0] == 'O')
                cdxl_cube_old(frame, &f, out);
            else
                cdxl_rgb_to_cube(frame, &f, out);
        }
        for (i = 0; i < (unsigned)f.width * f.height; i++) sum = sum * 31 + out[i];
        printf("%s %08lx\n", argv[2], sum & 0xffffffffUL);
        return 0;
    }
    printf("usage: webmdithertest all | pictures N SEED | bench[cdxl] OLD|NEW W H REPS\n");
    return 2;
}
