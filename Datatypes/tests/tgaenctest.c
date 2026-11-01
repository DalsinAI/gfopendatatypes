/*
 * gfopentga.datatype's encoder (tga/tgaencode.c) on x86 or ARM64 cores,
 * checked against its own decoder (tga/tgadecode.c): each test picture is
 * encoded four ways (run-length or not, alpha kept or not), read back with
 * tga_read_head() and tga_decode(), and compared pixel for pixel. Each file
 * is also written to OUTDIR with the picture Pillow should read from it
 * (NAME.rgba, R G B A bytes), for tests/test-tgaenc.sh to check with an
 * outside reader. Then the calls it must refuse.
 *
 *     tgaenctest OUTDIR
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tgadecode.h"
#include "tgaencode.h"

static int fails, checks;

static void *e_alloc(struct tgae_env *e, ULONG n) { (void)e; return malloc(n); }
static void e_free(struct tgae_env *e, void *p) { (void)e; free(p); }
static struct tgae_env env = { e_alloc, e_free };

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

/* Pictures: A R G B bytes, top row first */
enum { NOISE, FLAT, RUNS, STRIPES, GRADIENT, ALPHA, ALPHA_STEPS, OPAQUE_BLACK, KINDS };
static const char *kind_name[] = { "noise", "flat", "runs", "stripes", "gradient", "alpha", "alpha-steps", "black" };

static void make(UBYTE *p, ULONG w, ULONG h, int kind)
{
    ULONG x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            UBYTE *q = p + 4 * (y * w + x);
            ULONG r = rnd();
            switch (kind) {
            case NOISE:    q[0] = 0xFF; q[1] = (UBYTE)r; q[2] = (UBYTE)(r >> 8); q[3] = (UBYTE)(r >> 16); break;
            case FLAT:     q[0] = 0xFF; q[1] = 0x12; q[2] = 0x34; q[3] = 0x56; break;
            /* runs of 1 to 300 equal pixels, so packets of 128 and the ones after them come up */
            case RUNS: {
                ULONG k = (x * 7 + y * 3) % 301, c = (x / (k + 1) + y) & 3;
                q[0] = 0xFF; q[1] = (UBYTE)(c * 60); q[2] = (UBYTE)(c * 20); q[3] = 0x80;
                break;
            }
            case STRIPES:  q[0] = 0xFF; q[1] = q[2] = q[3] = (x & 1) ? 0xFF : 0x00; break;
            case GRADIENT: q[0] = 0xFF; q[1] = (UBYTE)x; q[2] = (UBYTE)y; q[3] = (UBYTE)(x + y); break;
            case ALPHA:    q[0] = (UBYTE)(r >> 4); q[1] = (UBYTE)r; q[2] = (UBYTE)(r >> 8); q[3] = (UBYTE)(r >> 16); break;
            case ALPHA_STEPS: q[0] = (UBYTE)(x < w / 2 ? 0xFF : 0x40); q[1] = 0x10; q[2] = 0x20; q[3] = 0x30; break;
            default:       q[0] = 0xFF; q[1] = q[2] = q[3] = 0; break;
            }
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

static void one(const char *dir, ULONG w, ULONG h, int kind, ULONG flags)
{
    char name[128];
    UBYTE *src = malloc(w * h * 4), *tga = NULL, *rgba = malloc(w * h * 4), alpha = 0;
    ULONG *got = malloc(w * h * 4), size = 0, i;
    struct tga_head hd;
    int hasalpha = 0, ok = 1;

    make(src, w, h, kind);
    for (i = 0; i < w * h; i++)
        if (src[4 * i] != 0xFF)
            hasalpha = 1;
    if (flags & TGAE_OPAQUE)
        hasalpha = 0;
    snprintf(name, sizeof name, "%s-%lux%lu%s%s", kind_name[kind], (unsigned long)w, (unsigned long)h,
             (flags & TGAE_RAW) ? "-raw" : "-rle", (flags & TGAE_OPAQUE) ? "-opaque" : "");

    check(tga_encode(&env, src, w, h, w * 4, flags, &tga, &size) == 0, "encodes", name);
    if (!tga)
        goto out;
    check(tga[2] == ((flags & TGAE_RAW) ? 2 : 10), "image type", name);
    check(tga[16] == (hasalpha ? 32 : 24) && (tga[17] & 0x0F) == (hasalpha ? 8 : 0) && (tga[17] & 0x20),
          "depth, alpha bits and top row first", name);
    check(size >= 26 && !memcmp(tga + size - 18, "TRUEVISION-XFILE.", 18), "the TGA 2.0 footer", name);
    check(tga_read_head(tga, size, &hd) == 0, "its own decoder reads the header", name);
    check(tga_decode(&hd, tga, size, got, NULL, &alpha) == 0, "and decodes it", name);
    check(alpha == hasalpha, "alpha as written", name);
    for (i = 0; i < w * h; i++) {
        const UBYTE *s = src + 4 * i;
        ULONG want = ((ULONG)(hasalpha ? s[0] : 0xFF) << 24) | ((ULONG)s[1] << 16) | ((ULONG)s[2] << 8) | s[3];
        if (got[i] != want) {
            if (ok)
                printf("  %s: pixel %lu is %08lx, not %08lx\n", name, (unsigned long)i,
                       (unsigned long)got[i], (unsigned long)want);
            ok = 0;
        }
        rgba[4 * i] = s[1];
        rgba[4 * i + 1] = s[2];
        rgba[4 * i + 2] = s[3];
        rgba[4 * i + 3] = hasalpha ? s[0] : 0xFF;
    }
    check(ok, "every pixel read back as written", name);
    if (!(flags & TGAE_RAW) && kind == FLAT)
        check(size <= 18 + 26 + h * ((w + 127) / 128) * 4, "a flat picture packs into runs", name);
    if (!(flags & TGAE_RAW) && kind == NOISE)
        check(size <= 18 + 26 + h * (w * 3 + (w + 127) / 128), "noise costs one byte in 128 over raw", name);
    put(dir, name, "tga", tga, size);
    put(dir, name, "rgba", rgba, w * h * 4);
out:
    free(tga);
    free(src);
    free(rgba);
    free(got);
}

int main(int argc, char **argv)
{
    static const ULONG sizes[][2] = { {1, 1}, {1, 7}, {7, 1}, {127, 3}, {128, 2}, {129, 2}, {255, 2}, {256, 2},
                                      {257, 3}, {300, 5}, {64, 64}, {333, 77} };
    static const ULONG flagsets[] = { 0, TGAE_RAW, TGAE_OPAQUE, TGAE_RAW | TGAE_OPAQUE };
    UBYTE px[4] = { 0xFF, 1, 2, 3 }, *t = NULL;
    ULONG s, k, f, size;

    if (argc != 2) {
        fprintf(stderr, "usage: tgaenctest OUTDIR\n");
        return 2;
    }
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (k = 0; k < KINDS; k++)
            for (f = 0; f < 4; f++)
                one(argv[1], sizes[s][0], sizes[s][1], (int)k, flagsets[f]);

    check(tga_encode(&env, NULL, 1, 1, 4, 0, &t, &size) == TGAE_ERR_ARGS && !t, "no pixels", "refuse");
    check(tga_encode(&env, px, 0, 1, 4, 0, &t, &size) == TGAE_ERR_ARGS, "width 0", "refuse");
    check(tga_encode(&env, px, 1, 0, 4, 0, &t, &size) == TGAE_ERR_ARGS, "height 0", "refuse");
    check(tga_encode(&env, px, 65536, 1, 65536 * 4, 0, &t, &size) == TGAE_ERR_ARGS, "width 65536", "refuse");
    check(tga_encode(&env, px, 2, 1, 4, 0, &t, &size) == TGAE_ERR_ARGS, "a stride shorter than a row", "refuse");

    printf("tgaenctest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
