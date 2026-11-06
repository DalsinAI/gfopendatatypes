/*
 * gfopengif.datatype's encoder (gif/gifencode.c) on x86 or ARM64 cores,
 * checked against its own decoder (gif/gifdecode.c): each test picture is
 * encoded, read back with gif_scan() and gif_show(), and compared.
 *
 * - Pictures of 256 colours or fewer (255 with transparent pixels) must come
 *   back pixel for pixel: opaque pixels as they were, those with alpha
 *   under 128 as 0x00000000 (what the decoder gives a transparent pixel).
 * - Pictures of more colours must come back opaque where they were, with
 *   at most 256 colours, each pixel near its own (median cut).
 * - gif_encode_lut8() pictures must give back their table's colours.
 *
 * Small tables, large ones, runs, noise that fills the LZW table again and
 * again (its clear codes), and sizes from 1x1 up, so every code size and
 * its change come up. Each file is also written to OUTDIR with the picture
 * our decoder made of it (NAME.rgba, R G B A bytes), for tests/test-gifenc.sh
 * to check with an outside reader. Then the calls it must refuse.
 *
 *     gifenctest OUTDIR
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gifdecode.h"
#include "gifencode.h"

static int fails, checks;

static void *e_alloc(struct gife_env *e, ULONG n) { (void)e; return malloc(n); }
static void e_free(struct gife_env *e, void *p) { (void)e; free(p); }
static struct gife_env env = { e_alloc, e_free };

static ULONG seed = 12345;
static ULONG rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 8) & 0xFFFFFF;
}

static void check(int ok, const char *what, const char *name)
{
    checks++;
    if (!ok) {
        fails++;
        printf("FAIL %s: %s\n", name, what);
    }
}

static void put(const char *dir, const char *name, const char *ext, const UBYTE *p, ULONG n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.%s", dir, name, ext);
    if (!(f = fopen(path, "wb")) || fwrite(p, 1, n, f) != n) {
        printf("FAIL can't write %s\n", path);
        exit(1);
    }
    fclose(f);
}

/* Decodes a one-frame GIF to ARGB words; NULL when it can't. */
static ULONG *decode(const UBYTE *gif, ULONG size, ULONG w, ULONG h, const char *name)
{
    struct gif_info g;
    struct gif_frame fr;
    struct gif_canvas c;
    LONG n = gif_scan(gif, size, &g, &fr, 1);
    check(n == 1, "one frame", name);
    if (n != 1)
        return NULL;
    check(g.width == w && g.height == h, "its size", name);
    c.argb = malloc((size_t)w * h * 4);
    c.saved = NULL;
    c.work = malloc(GIF_WORKSIZE);
    c.shown = -1;
    check(gif_show(gif, size, &g, &fr, &c, 0) == 0, "our decoder shows it", name);
    free(c.work);
    return c.argb;
}

static void save(const char *dir, const char *name, const UBYTE *gif, ULONG size, const ULONG *got,
                 ULONG w, ULONG h)
{
    UBYTE *rgba = malloc((size_t)w * h * 4);
    ULONG i;
    for (i = 0; i < w * h; i++) {
        rgba[4 * i] = (UBYTE)(got[i] >> 16);
        rgba[4 * i + 1] = (UBYTE)(got[i] >> 8);
        rgba[4 * i + 2] = (UBYTE)got[i];
        rgba[4 * i + 3] = (UBYTE)(got[i] >> 24);
    }
    put(dir, name, "gif", gif, size);
    put(dir, name, "rgba", rgba, w * h * 4);
    free(rgba);
}

/* Pictures: A R G B bytes */
enum { FEW, FEW_ALPHA, FLAT, RUNS, GRADIENT, NOISE, NOISE_ALPHA, KINDS };
static const char *kind_name[] = { "few", "few-alpha", "flat", "runs", "gradient", "noise", "noise-alpha" };

static void make(UBYTE *p, ULONG w, ULONG h, int kind, ULONG ncol, const ULONG *pal)
{
    ULONG x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            UBYTE *q = p + 4 * (y * w + x);
            ULONG r = rnd(), c;
            switch (kind) {
            case FEW:
            case FEW_ALPHA:
                c = pal[r % ncol];
                q[0] = (kind == FEW_ALPHA && (r >> 12) % 5 == 0) ? (UBYTE)((r >> 4) & 0x7F) : (UBYTE)(0x80 | (r >> 17));
                q[1] = (UBYTE)(c >> 16); q[2] = (UBYTE)(c >> 8); q[3] = (UBYTE)c;
                break;
            case FLAT:     q[0] = 0xFF; q[1] = 0x12; q[2] = 0x34; q[3] = 0x56; break;
            case RUNS: {
                ULONG k = (x * 7 + y * 3) % 301;
                c = pal[(x / (k + 1) + y) % ncol];
                q[0] = 0xFF; q[1] = (UBYTE)(c >> 16); q[2] = (UBYTE)(c >> 8); q[3] = (UBYTE)c;
                break;
            }
            case GRADIENT: q[0] = 0xFF; q[1] = (UBYTE)(x * 255 / (w > 1 ? w - 1 : 1)); q[2] = (UBYTE)(y * 255 / (h > 1 ? h - 1 : 1)); q[3] = (UBYTE)((x + y) & 0xFF); break;
            case NOISE:    q[0] = 0xFF; q[1] = (UBYTE)r; q[2] = (UBYTE)(r >> 8); q[3] = (UBYTE)(r >> 16); break;
            default:       q[0] = (UBYTE)(r >> 20 << 4); q[1] = (UBYTE)r; q[2] = (UBYTE)(r >> 8); q[3] = (UBYTE)(r >> 16); break;
            }
        }
}

static void one(const char *dir, ULONG w, ULONG h, int kind, ULONG ncol, ULONG flags)
{
    char name[128];
    ULONG pal[256], i, size = 0, *got, n = 0, seen[257], nseen = 0;
    UBYTE *src = malloc((size_t)w * h * 4), *gif = NULL;
    int exactly, ok = 1;
    unsigned long long err = 0;

    for (i = 0; i < 256; i++)
        pal[i] = rnd();
    make(src, w, h, kind, ncol, pal);
    /* how many colours the opaque pixels have: up to 257 counted */
    for (i = 0; i < w * h && nseen <= 256; i++) {
        const UBYTE *s = src + 4 * i;
        ULONG c = ((ULONG)s[1] << 16) | ((ULONG)s[2] << 8) | s[3], j;
        if (!(flags & GIFE_OPAQUE) && s[0] < 0x80) {
            n = 1;
            continue;
        }
        for (j = 0; j < nseen && seen[j] != c; j++)
            ;
        if (j == nseen)
            seen[nseen++] = c;
    }
    exactly = nseen + n <= 256;
    snprintf(name, sizeof name, "%s%lu-%lux%lu%s", kind_name[kind], (unsigned long)ncol, (unsigned long)w,
             (unsigned long)h, (flags & GIFE_OPAQUE) ? "-opaque" : "");

    check(gif_encode(&env, src, w, h, w * 4, flags, &gif, &size) == 0, "encodes", name);
    if (!gif || !(got = decode(gif, size, w, h, name)))
        goto out;
    for (i = 0; i < w * h; i++) {
        const UBYTE *s = src + 4 * i;
        int clear = !(flags & GIFE_OPAQUE) && s[0] < 0x80;
        ULONG want = clear ? 0 : 0xFF000000UL | ((ULONG)s[1] << 16) | ((ULONG)s[2] << 8) | s[3];
        if (exactly || clear) {
            if (got[i] != want) {
                if (ok)
                    printf("  %s: pixel %lu is %08lx, not %08lx\n", name, (unsigned long)i,
                           (unsigned long)got[i], (unsigned long)want);
                ok = 0;
            }
        } else {
            int a;
            if ((got[i] >> 24) != 0xFF)
                ok = 0;
            for (a = 0; a < 3; a++) {
                int d = (int)((got[i] >> (16 - 8 * a)) & 0xFF) - (int)s[1 + a];
                err += (unsigned long long)(d < 0 ? -d : d);
            }
        }
    }
    check(ok, exactly ? "every pixel read back as written" : "opaque where it was, transparent where it was", name);
    if (!exactly && getenv("GIFENC_VERBOSE"))
        printf("  %s: average error %llu a channel\n", name, err / ((unsigned long long)w * h * 3));
    if (!exactly)   /* the average error a channel: median cut, no dithering */
        check(err / ((unsigned long long)w * h * 3) <= (kind == GRADIENT ? 8 : 12), "colours near their own", name);
    if (kind == FLAT)
        check(size < 64 + w * h / 64 + 800, "a flat picture packs small", name);
    save(dir, name, gif, size, got, w, h);
    free(got);
out:
    free(gif);
    free(src);
}

static void lut8(const char *dir, ULONG w, ULONG h, ULONG ncol, LONG trans)
{
    char name[128];
    UBYTE *idx = malloc((size_t)w * h), rgb[768], *gif = NULL;
    ULONG i, size = 0, *got;
    int ok = 1;
    for (i = 0; i < 768; i++)
        rgb[i] = (UBYTE)rnd();
    for (i = 0; i < w * h; i++)
        idx[i] = (UBYTE)(rnd() % ncol);
    snprintf(name, sizeof name, "lut8-%lu-%lux%lu-t%ld", (unsigned long)ncol, (unsigned long)w,
             (unsigned long)h, (long)trans);
    check(gif_encode_lut8(&env, idx, w, h, w, rgb, ncol, trans, &gif, &size) == 0, "encodes", name);
    if (!gif || !(got = decode(gif, size, w, h, name)))
        goto out;
    for (i = 0; i < w * h; i++) {
        ULONG want = (LONG)idx[i] == trans ? 0
            : 0xFF000000UL | ((ULONG)rgb[3 * idx[i]] << 16) | ((ULONG)rgb[3 * idx[i] + 1] << 8) | rgb[3 * idx[i] + 2];
        if (got[i] != want)
            ok = 0;
    }
    check(ok, "every index read back as its colour", name);
    save(dir, name, gif, size, got, w, h);
    free(got);
out:
    free(gif);
    free(idx);
}

int main(int argc, char **argv)
{
    static const ULONG sizes[][2] = { {1, 1}, {1, 7}, {7, 1}, {3, 3}, {64, 64}, {255, 2}, {256, 3},
                                      {333, 77}, {640, 480} };
    static const ULONG ncols[] = { 1, 2, 3, 4, 5, 16, 17, 129, 200, 255, 256 };
    UBYTE px[4] = { 0xFF, 1, 2, 3 }, rgb[6] = { 0 }, *t = NULL;
    ULONG s, k, c, size;
    int kind;

    if (argc != 2) {
        fprintf(stderr, "usage: gifenctest OUTDIR\n");
        return 2;
    }
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        for (k = 0; k < sizeof ncols / sizeof ncols[0]; k++) {
            one(argv[1], sizes[s][0], sizes[s][1], FEW, ncols[k], 0);
            one(argv[1], sizes[s][0], sizes[s][1], FEW_ALPHA, ncols[k], 0);
        }
        for (kind = FLAT; kind < KINDS; kind++) {
            one(argv[1], sizes[s][0], sizes[s][1], kind, 7, 0);
            one(argv[1], sizes[s][0], sizes[s][1], kind, 7, GIFE_OPAQUE);
        }
        for (c = 1; c <= 256; c = c * 2 + (c == 1)) {
            lut8(argv[1], sizes[s][0], sizes[s][1], c, -1);
            lut8(argv[1], sizes[s][0], sizes[s][1], c, (LONG)(c - 1));
        }
    }
    /* the LZW table filled and cleared many times, at every minimum code size */
    for (k = 0; k < sizeof ncols / sizeof ncols[0]; k++)
        one(argv[1], 1500, 700, FEW, ncols[k], GIFE_OPAQUE);
    one(argv[1], 4000, 1, NOISE, 0, 0);
    one(argv[1], 1, 4000, RUNS, 3, 0);

    check(gif_encode(&env, NULL, 1, 1, 4, 0, &t, &size) == GIFE_ERR_ARGS && !t, "no pixels", "refuse");
    check(gif_encode(&env, px, 0, 1, 4, 0, &t, &size) == GIFE_ERR_ARGS, "width 0", "refuse");
    check(gif_encode(&env, px, 1, 0, 4, 0, &t, &size) == GIFE_ERR_ARGS, "height 0", "refuse");
    check(gif_encode(&env, px, 65536, 1, 65536 * 4, 0, &t, &size) == GIFE_ERR_ARGS, "width 65536", "refuse");
    check(gif_encode(&env, px, 2, 1, 4, 0, &t, &size) == GIFE_ERR_ARGS, "a stride shorter than a row", "refuse");
    check(gif_encode_lut8(&env, px, 1, 1, 1, rgb, 0, -1, &t, &size) == GIFE_ERR_ARGS, "no colours", "refuse");
    check(gif_encode_lut8(&env, px, 1, 1, 1, rgb, 257, -1, &t, &size) == GIFE_ERR_ARGS, "257 colours", "refuse");
    check(gif_encode_lut8(&env, px, 1, 1, 1, rgb, 2, 2, &t, &size) == GIFE_ERR_ARGS, "a transparent index past the table", "refuse");
    check(gif_encode_lut8(&env, px, 1, 1, 1, rgb, 2, -1, &t, &size) == GIFE_ERR_ARGS, "an index past the table", "refuse");

    printf("gifenctest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
