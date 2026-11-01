/*
 * gfopenpng.datatype's encoder (png/pngencode.c) on x86 or ARM64 cores
 * (tests/test-pngenc.sh): encodes test pictures of many kinds and sizes,
 * each through z.library's deflate (tests/zlhost.c), and writes for each
 *
 *     OUT/NAME.png    the file
 *     OUT/NAME.rgba   what it must decode to: RGBA, 8 bits, rows one after another
 *
 * and a line "NAME WIDTH HEIGHT ALPHA" to stdout, for the script to have
 * Python read every file back. Each is also read back here by gfopenpng's own
 * decoder (png/pngdecode.c), which must give the same pixels.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pngencode.h"
#include "pngdecode.h"

static void *e_alloc(struct pnge_env *e, ULONG n) { (void)e; return malloc(n ? n : 1); }
static void e_free(struct pnge_env *e, void *p) { (void)e; free(p); }
static struct pnge_env env = { e_alloc, e_free };

static ULONG seed = 1;
static ULONG rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 16) & 0x7fff;
}

static void save(const char *path, const void *p, ULONG n)
{
    FILE *f = fopen(path, "wb");
    if (!f || (n && fwrite(p, 1, n, f) != n)) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fclose(f);
}

static int fails;

/* kind: 0 opaque gradient, 1 gradient with alpha, 2 noise with alpha,
 * 3 premultiplied, 4 flat colour, 5 noise, opaque */
static void one(const char *out, const char *name, ULONG w, ULONG h, ULONG pad, int kind, LONG level)
{
    ULONG stride = w * 4 + pad, x, y, size, alpha = 0;
    UBYTE *argb = calloc(stride, h), *rgba = malloc(w * h * 4), *png;
    char path[512];
    LONG err;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            UBYTE *p = argb + y * stride + 4 * x, *q = rgba + 4 * (y * w + x);
            ULONG a = 255, r = (x * 255) / (w > 1 ? w - 1 : 1), g = (y * 255) / (h > 1 ? h - 1 : 1),
                  b = (x ^ y) & 255;
            if (kind == 1)
                a = (x + y) & 255;
            if (kind == 2 || kind == 5) {
                r = rnd() & 255;
                g = rnd() & 255;
                b = rnd() & 255;
                if (kind == 2)
                    a = rnd() & 255;
            }
            if (kind == 4) {
                r = 0x12;
                g = 0x34;
                b = 0x56;
            }
            if (kind == 3) {
                a = (x * 7 + y * 3) & 255;
                /* store premultiplied; expect what unpremultiplying gives */
                r = r * a / 255;
                g = g * a / 255;
                b = b * a / 255;
                p[0] = (UBYTE)a;
                p[1] = (UBYTE)r;
                p[2] = (UBYTE)g;
                p[3] = (UBYTE)b;
                if (a) {
                    r = (r * 255 + a / 2) / a;
                    g = (g * 255 + a / 2) / a;
                    b = (b * 255 + a / 2) / a;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                } else
                    r = g = b = 0;
            } else {
                p[0] = (UBYTE)a;
                p[1] = (UBYTE)r;
                p[2] = (UBYTE)g;
                p[3] = (UBYTE)b;
            }
            if (a != 255)
                alpha = 1;
            q[0] = (UBYTE)r;
            q[1] = (UBYTE)g;
            q[2] = (UBYTE)b;
            q[3] = (UBYTE)a;
        }
    err = png_encode(NULL, &env, argb, w, h, stride, kind == 3 ? PNGE_PREMULTIPLIED : 0, level, &png, &size);
    if (err) {
        printf("%s encode error %ld\n", name, (long)err);
        fails++;
        goto done;
    }
    snprintf(path, sizeof path, "%s/%s.png", out, name);
    save(path, png, size);
    snprintf(path, sizeof path, "%s/%s.rgba", out, name);
    save(path, rgba, w * h * 4);
    {
        /* back through gfopenpng's decoder */
        struct png_head *hd = malloc(sizeof(*hd));
        UBYTE *idat, *raw;
        ULONG *dec, i;
        void *work = malloc(PNG_WORKSIZE);
        int bad = 0;
        if (png_read_head(png, size, hd)) {
            printf("%s: gfopenpng's decoder refuses it\n", name);
            fails++;
        } else {
            idat = malloc(hd->idat_len ? hd->idat_len : 1);
            raw = malloc(hd->raw_len);
            dec = malloc(w * h * 4);
            png_gather(png, size, idat);
            if (png_decode(hd, idat, raw, dec, work))
                bad = 1;
            for (i = 0; i < w * h && !bad; i++) {
                const UBYTE *q = rgba + 4 * i;
                ULONG want = ((ULONG)q[3] << 24) | ((ULONG)q[0] << 16) | ((ULONG)q[1] << 8) | q[2];
                if (dec[i] != want)
                    bad = 1;
            }
            if (bad || hd->alpha != alpha) {
                printf("%s: gfopenpng's decoder reads it otherwise\n", name);
                fails++;
            }
            free(idat);
            free(raw);
            free(dec);
        }
        free(hd);
        free(work);
    }
    printf("%s %lu %lu %lu %lu\n", name, (unsigned long)w, (unsigned long)h, (unsigned long)alpha,
           (unsigned long)size);
    free(png);
done:
    free(argb);
    free(rgba);
}

int main(int argc, char **argv)
{
    static const ULONG sizes[][2] = { { 1, 1 }, { 2, 3 }, { 7, 5 }, { 64, 48 }, { 300, 200 }, { 1023, 3 },
                                      { 3, 700 }, { 640, 480 } };
    char name[64];
    ULONG s;
    int kind;
    if (argc != 2)
        return 2;
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (kind = 0; kind < 6; kind++) {
            snprintf(name, sizeof name, "k%d-%lux%lu", kind, (unsigned long)sizes[s][0], (unsigned long)sizes[s][1]);
            one(argv[1], name, sizes[s][0], sizes[s][1], s & 1 ? 12 : 0, kind, -1);
        }
    one(argv[1], "stored", 100, 50, 0, 1, 0);
    one(argv[1], "fast", 400, 300, 4, 2, 1);
    one(argv[1], "best", 400, 300, 0, 0, 9);
    {
        UBYTE *png;
        ULONG size;
        UBYTE px[4] = { 255, 1, 2, 3 };
        if (png_encode(NULL, &env, NULL, 1, 1, 4, 0, -1, &png, &size) != PNGE_ERR_ARGS
            || png_encode(NULL, &env, px, 0, 1, 4, 0, -1, &png, &size) != PNGE_ERR_ARGS
            || png_encode(NULL, &env, px, 2, 1, 4, 0, -1, &png, &size) != PNGE_ERR_ARGS) {
            printf("bad arguments are not refused\n");
            fails++;
        }
    }
    return fails ? 1 : 0;
}
