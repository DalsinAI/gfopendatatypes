/*
 * webp.datatype's encoder (webp/webpencode.c) on x86 or ARM64 cores, with
 * libwebp built from the same patched tree as on the Amiga: each test
 * picture is encoded lossless at levels 0, -1 (the default) and 9, decoded
 * with libwebp's own decoder (WebPDecodeARGB) and compared pixel for pixel,
 * transparent pixels' colour too. Each file is written to OUTDIR with the
 * picture Pillow should read from it (NAME.rgba), for tests/test-webpenc.sh.
 *
 *     webpenctest OUTDIR
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webpencode.h"
#include "src/webp/decode.h"

struct buf {
    UBYTE *p;
    ULONG n, cap;
    LONG fail_at;
    ULONG calls;
};

static LONG put(void *h, const UBYTE *p, ULONG n)
{
    struct buf *b = h;
    b->calls++;
    if (b->fail_at && (LONG)b->calls == b->fail_at)
        return 225;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 0;
}

static ULONG seed = 4242;
static ULONG rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 8) & 0xFFFFFF;
}

enum { NOISE, GRADIENT, FLAT, ALPHA, CLEAR_COLOUR, PALETTE, KINDS };
static const char *kind_name[] = { "noise", "gradient", "flat", "alpha", "clear-colour", "palette" };

static ULONG pixel(int kind, ULONG x, ULONG y)
{
    ULONG r = rnd();
    switch (kind) {
    case NOISE: return 0xFF000000UL | r;
    case GRADIENT: return 0xFF000000UL | ((x & 255) << 16) | ((y & 255) << 8) | ((x + y) & 255);
    case FLAT: return 0xFF123456UL;
    case ALPHA: return ((r & 0xFF) << 24) | (r >> 8) | ((r & 0xF) << 20);
    /* fully transparent pixels with colour: "exact" must keep it */
    case CLEAR_COLOUR: return ((x + y) & 1) ? (0x00000000UL | (r & 0xFFFFFF)) : (0xFF000000UL | (r & 0xFFFFFF));
    default: return 0xFF000000UL | ((r % 7) * 0x242424UL);
    }
}

int main(int argc, char **argv)
{
    static const ULONG sizes[][2] = { {1, 1}, {3, 5}, {17, 1}, {64, 48}, {200, 131} };
    static const LONG levels[] = { 0, -1, 9 };
    int fails = 0, checks = 0;
    unsigned s, k, l;

    if (argc != 2) {
        fprintf(stderr, "usage: webpenctest OUTDIR\n");
        return 2;
    }
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (k = 0; k < KINDS; k++) {
            ULONG w = sizes[s][0], h = sizes[s][1], i;
            ULONG *src = malloc(w * h * 4), *work = malloc(w * h * 4);
            UBYTE *rgba = malloc(w * h * 4);
            for (i = 0; i < w * h; i++) {
                src[i] = pixel((int)k, i % w, i / w);
                rgba[4 * i] = (UBYTE)(src[i] >> 16);
                rgba[4 * i + 1] = (UBYTE)(src[i] >> 8);
                rgba[4 * i + 2] = (UBYTE)src[i];
                rgba[4 * i + 3] = (UBYTE)(src[i] >> 24);
            }
            for (l = 0; l < 3; l++) {
                struct buf b = { 0, 0, 0, 0, 0 };
                char name[128], path[600];
                int dw = 0, dh = 0, bad = 0;
                UBYTE *got;
                LONG rc;
                FILE *f;
                memcpy(work, src, w * h * 4);    /* the encoder may change its pixels */
                snprintf(name, sizeof name, "%s-%lux%lu-L%ld", kind_name[k], (unsigned long)w, (unsigned long)h,
                         (long)levels[l]);
                rc = webp_encode_lossless(work, w, h, w, levels[l], put, &b);
                checks++;
                if (rc) {
                    printf("FAIL %s: webp_encode_lossless returned %ld\n", name, (long)rc);
                    fails++;
                    free(b.p);
                    continue;
                }
                got = WebPDecodeARGB(b.p, b.n, &dw, &dh);
                checks++;
                if (!got || dw != (int)w || dh != (int)h) {
                    printf("FAIL %s: libwebp's decoder doesn't read it back\n", name);
                    fails++;
                } else {
                    for (i = 0; i < w * h && !bad; i++) {
                        ULONG v = ((ULONG)got[4 * i] << 24) | ((ULONG)got[4 * i + 1] << 16)
                                | ((ULONG)got[4 * i + 2] << 8) | got[4 * i + 3];
                        if (v != src[i]) {
                            printf("FAIL %s: pixel %lu is %08lx, not %08lx\n", name, (unsigned long)i,
                                   (unsigned long)v, (unsigned long)src[i]);
                            bad = 1;
                        }
                    }
                    fails += bad;
                }
                WebPFree(got);
                snprintf(path, sizeof path, "%s/%s.webp", argv[1], name);
                if ((f = fopen(path, "wb"))) {
                    fwrite(b.p, 1, b.n, f);
                    fclose(f);
                }
                snprintf(path, sizeof path, "%s/%s.rgba", argv[1], name);
                if ((f = fopen(path, "wb"))) {
                    fwrite(rgba, 1, w * h * 4, f);
                    fclose(f);
                }
                free(b.p);
            }
            /* a writer that fails stops the encode, and its code comes back */
            if (w * h > 1000) {
                struct buf b = { 0, 0, 0, 1, 0 };
                LONG rc;
                memcpy(work, src, w * h * 4);
                rc = webp_encode_lossless(work, w, h, w, -1, put, &b);
                checks++;
                if (rc != 225) {
                    printf("FAIL %s: a failing write gave %ld\n", kind_name[k], (long)rc);
                    fails++;
                }
                free(b.p);
            }
            free(src);
            free(work);
            free(rgba);
        }
    {
        ULONG px = 0;
        checks += 3;
        if (webp_encode_lossless(NULL, 1, 1, 1, -1, put, NULL) != WEBPE_ERR_ARGS) fails++;
        if (webp_encode_lossless(&px, 0, 1, 1, -1, put, NULL) != WEBPE_ERR_ARGS) fails++;
        if (webp_encode_lossless(&px, 16384, 1, 16384, -1, put, NULL) != WEBPE_ERR_ARGS) fails++;
    }
    printf("webpenctest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
