/*
 * webm.datatype's picture sizes on the host (tests/test-webm.sh), under
 * AddressSanitizer and UBSan, with no libvpx (tests/host/vpx/vpx_image.h):
 *   - WebM files made here with a crafted track header: a track wider or
 *     taller than WEBM_MAX_SIDE (65537 x 65536, whose product wraps to
 *     65536 in 32 bits) is refused by webm_parse, as are 8193 either way;
 *     8192 x 8192 and 160 x 120 are read;
 *   - the same file cut short at every length and with each byte changed:
 *     nothing is read outside it;
 *   - webm_dither into buffers smaller than the decoded picture, or than
 *     width x height: nothing is written outside the buffer, and every
 *     pixel that fits is written.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webm_demux.h"
#include "webm_dither.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/* ---- a WebM file in memory ------------------------------------------------ */

typedef struct {
    unsigned char *d;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *p, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->d = realloc(b->d, b->cap);
        if (!b->d)
            abort();
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

static void putId(Buf *b, uint32_t id)
{
    unsigned char c[4];
    int n = id > 0xffffff ? 4 : id > 0xffff ? 3 : id > 0xff ? 2 : 1, i;
    for (i = 0; i < n; i++)
        c[i] = (unsigned char)(id >> (8 * (n - 1 - i)));
    put(b, c, n);
}

/* An element: its ID, a 4-byte size and the body. */
static void element(Buf *b, uint32_t id, const Buf *body)
{
    unsigned char s[4] = { (unsigned char)(0x10 | (body->n >> 24)), (unsigned char)(body->n >> 16),
        (unsigned char)(body->n >> 8), (unsigned char)body->n };
    putId(b, id);
    put(b, s, 4);
    put(b, body->d, body->n);
}

static void uintElement(Buf *b, uint32_t id, uint32_t v)
{
    unsigned char c[5] = { 0x84, (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    putId(b, id);
    put(b, c, 5);
}

static void stringElement(Buf *b, uint32_t id, const char *s)
{
    unsigned char c = (unsigned char)(0x80 | strlen(s));
    putId(b, id);
    put(b, &c, 1);
    put(b, s, strlen(s));
}

/* EBML header, then a segment of unknown size with one VP8 track of
 * width x height and one cluster holding a key frame. */
static Buf makeWebM(uint32_t width, uint32_t height)
{
    Buf file = { 0 }, ebml = { 0 }, video = { 0 }, entry = { 0 }, tracks = { 0 }, block = { 0 }, cluster = { 0 };
    static const unsigned char unknown[8] = { 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const unsigned char blockHead[4] = { 0x81, 0x00, 0x00, 0x80 };   /* track 1, time 0, key frame */

    stringElement(&ebml, 0x4282, "webm");
    element(&file, 0x1A45DFA3, &ebml);
    putId(&file, 0x18538067);
    put(&file, unknown, sizeof unknown);
    uintElement(&video, 0xB0, width);
    uintElement(&video, 0xBA, height);
    uintElement(&entry, 0xD7, 1);
    uintElement(&entry, 0x83, 1);
    stringElement(&entry, 0x86, "V_VP8");
    element(&entry, 0xE0, &video);
    element(&tracks, 0xAE, &entry);
    element(&file, 0x1654AE6B, &tracks);
    uintElement(&cluster, 0xE7, 0);
    put(&block, blockHead, sizeof blockHead);
    put(&block, "a frame", 7);
    element(&cluster, 0xA3, &block);
    element(&file, 0x1F43B675, &cluster);
    free(ebml.d);
    free(video.d);
    free(entry.d);
    free(tracks.d);
    free(block.d);
    free(cluster.d);
    return file;
}

/* A copy of exactly n bytes, so AddressSanitizer sees a read past it. */
typedef struct {
    unsigned char *d;
    uint32_t n;
} Mem;

static long readMem(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    Mem *m = handle;
    if (offset > m->n || length > m->n - offset)
        return -1;
    memcpy(buffer, m->d + offset, length);
    return (long)length;
}

static int parseBytes(const unsigned char *d, size_t n, WebMInfo *info)
{
    Mem m;
    int rc;
    m.n = (uint32_t)n;
    m.d = malloc(n ? n : 1);
    memcpy(m.d, d, n);
    rc = webm_parse(readMem, &m, m.n, info);
    free(m.d);
    return rc;
}

static void parseCase(uint32_t w, uint32_t h, int want)
{
    char what[160];
    WebMInfo info;
    Buf f = makeWebM(w, h);
    int rc = parseBytes(f.d, f.n, &info);
    if (want == 0)
        snprintf(what, sizeof what, "track %lux%lu: read, %ux%u, %u frame", (unsigned long)w, (unsigned long)h,
            (unsigned)info.width, (unsigned)info.height, (unsigned)info.frameCount);
    else
        snprintf(what, sizeof what, "track %lux%lu: refused (%d), width x height in 32 bits would be %lu", (unsigned long)w,
            (unsigned long)h, rc, (unsigned long)(uint32_t)(w * h));
    check(rc == want && (want || (info.width == w && info.height == h && info.frameCount == 1)), what);
    if (!rc)
        webm_free(&info);
    free(f.d);
}

/* ---- the dither ------------------------------------------------------------ */

#define UNTOUCHED 0xEE   /* the dither writes cube indices, never above 215 */

static vpx_image_t *makeImage(unsigned w, unsigned h, int srgb)
{
    vpx_image_t *img = calloc(1, sizeof *img);
    unsigned p, x, y;
    img->cs = srgb ? VPX_CS_SRGB : VPX_CS_BT_601;
    img->d_w = w;
    img->d_h = h;
    img->x_chroma_shift = img->y_chroma_shift = srgb ? 0 : 1;
    for (p = 0; p < 3; p++) {
        unsigned pw = p ? (w + img->x_chroma_shift) >> img->x_chroma_shift : w;
        unsigned ph = p ? (h + img->y_chroma_shift) >> img->y_chroma_shift : h;
        img->stride[p] = (int)pw;
        img->planes[p] = malloc((size_t)pw * ph);
        for (y = 0; y < ph; y++)
            for (x = 0; x < pw; x++)
                img->planes[p][(size_t)y * pw + x] = (unsigned char)(x * 7 + y * 13 + p * 50);
    }
    return img;
}

static void freeImage(vpx_image_t *img)
{
    free(img->planes[0]);
    free(img->planes[1]);
    free(img->planes[2]);
    free(img);
}

/* Dithers an iw x ih picture into a buffer of size bytes taken as width x
 * height, pitch apart; checks the written pixels are exactly the rows x
 * cols expected and nothing else. */
static void ditherCase(const WebMDitherTables *t, unsigned iw, unsigned ih, int srgb, unsigned long size, unsigned width,
    unsigned height, unsigned pitch, unsigned rows, unsigned cols, const char *why)
{
    char what[200];
    vpx_image_t *img = makeImage(iw, ih, srgb);
    unsigned char *out = malloc(size ? size : 1);
    unsigned long i, wrong = 0;
    memset(out, UNTOUCHED, size);
    webm_dither(t, img, out, size, width, height, pitch);
    for (i = 0; i < size; i++) {
        int inside = i / pitch < rows && i % pitch < cols;
        if ((out[i] != UNTOUCHED) != inside)
            wrong++;
    }
    snprintf(what, sizeof what, "dither %ux%u%s into %lu bytes as %ux%u (pitch %u): %ux%u written (%s)", iw, ih,
        srgb ? " sRGB" : "", size, width, height, pitch, cols, rows, why);
    check(!wrong, what);
    free(out);
    freeImage(img);
}

int main(void)
{
    WebMDitherTables t;
    Buf f;
    size_t n, i;
    int v;
    unsigned cases = 0;

    /* The file the review describes: 65537 x 65536 wraps to 65536. */
    parseCase(65537, 65536, -2);
    parseCase(8193, 120, -2);
    parseCase(160, 8193, -2);
    parseCase(0xffffffffUL, 0xffffffffUL, -2);
    parseCase(8192, 8192, 0);
    parseCase(160, 120, 0);
    parseCase(1, 1, 0);

    /* Cut short at every length, and each byte changed. */
    f = makeWebM(160, 120);
    for (n = 0; n <= f.n; n++) {
        WebMInfo info;
        if (!parseBytes(f.d, n, &info))
            webm_free(&info);
        cases++;
    }
    for (i = 0; i < f.n; i++)
        for (v = 0; v < 256; v += 17) {
            WebMInfo info;
            unsigned char keep = f.d[i];
            f.d[i] = (unsigned char)v;
            if (!parseBytes(f.d, f.n, &info)) {
                if (info.width > WEBM_MAX_SIDE || info.height > WEBM_MAX_SIDE)
                    check(0, "a changed file read with a side over WEBM_MAX_SIDE");
                webm_free(&info);
            }
            f.d[i] = keep;
            cases++;
        }
    free(f.d);
    printf("ok   %u cut or changed files: nothing read outside them, no side over %d\n", cases, WEBM_MAX_SIDE);

    webm_dither_init(&t);
    /* What the old class did with that file: a 65536-byte buffer taken as
     * 65537 x 65536, pitch 65537. Not one row fits. */
    ditherCase(&t, 65537, 2, 0, 65536, 65537, 65536, 65537, 0, 0, "the wrapped buffer: nothing fits");
    /* A frame bigger than its header: only the header's part. */
    ditherCase(&t, 320, 240, 0, 160UL * 120, 160, 120, 160, 120, 160, "frame larger than the track");
    ditherCase(&t, 100, 50, 0, 160UL * 120, 160, 120, 160, 50, 100, "frame smaller than the track");
    /* A buffer shorter than width x height: the rows that fit. */
    ditherCase(&t, 160, 120, 0, 1000, 160, 120, 160, 6, 160, "buffer of 6.25 rows");
    ditherCase(&t, 64, 48, 1, 64UL * 48, 64, 48, 64, 48, 64, "whole picture");
    ditherCase(&t, 64, 48, 0, 64UL * 48, 64, 48, 64, 48, 64, "whole picture");
    if (failures) {
        printf("webmsizetest: %d failed\n", failures);
        return 1;
    }
    printf("webmsizetest: all passed\n");
    return 0;
}
