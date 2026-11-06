/*
 * webp.datatype's lossless WebP encoder (webp/webpencode.c) as an Amiga
 * program, built with the same compiler and flags as the datatype but with
 * libnix's start-up and malloc: it encodes test pictures at each effort,
 * says how long each took (in 50ths of a second), decodes each file back
 * with libwebp's decoder and checks every pixel. It tells a hang in the
 * encoder's code apart from one in the datatype's surroundings. Built
 * twice: webpencamiga with libnix's malloc, webpencamiga-av with
 * AllocVec's, as common/dtlib.c gives the datatype (-DAV_MALLOC).
 *
 * Each is built with WEBPENC_TRACE, so libwebp's progress (the percent it
 * reports as it goes) is printed as it comes, and a hang shows where.
 * Variants (build-dtsave.sh): -nd with libwebp and the encoder built
 * -fno-tree-loop-distribute-patterns (GCC 16.2.0b can turn a loop that
 * shifts an array up into a forward copy), -o1 at -O1, -qs with a qsort of
 * its own. Each first checks memmove (libnix's __bcopz, both ways) and qsort.
 *
 *     webpencamiga [MAXLEVEL]      (default 9)
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include "webpencode.h"
#include "webp/decode.h"

#ifdef AV_MALLOC
#include <exec/memory.h>
#include <proto/exec.h>
/* common/dtlib.c's, without AC090's helpers */
void *malloc(size_t size)
{
    ULONG *block = AllocVec(size + 8, MEMF_ANY);
    if (!block)
        return NULL;
    block[0] = size;
    return block + 2;
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    ULONG *block;
    if (size && total / size != count)
        return NULL;
    if (!(block = AllocVec(total + 8, MEMF_ANY | MEMF_CLEAR)))
        return NULL;
    block[0] = total;
    return block + 2;
}

void free(void *p)
{
    if (p)
        FreeVec((ULONG *)p - 2);
}

void *realloc(void *p, size_t size)
{
    void *n;
    size_t old;
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    old = ((ULONG *)p)[-2];
    if (size <= old)
        return p;
    if ((n = malloc(size)) != NULL) {
        memcpy(n, p, old);
        free(p);
    }
    return n;
}
#endif

#ifdef OWN_QSORT
/* -qs: a qsort of our own in place of libnix's (libwebp sorts its Huffman
 * trees and palettes with it) */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    UBYTE *b = base, t;
    size_t i, j, k;
    for (i = 1; i < n; i++)                     /* insertion sort: the arrays here are small */
        for (j = i; j > 0 && cmp(b + (j - 1) * size, b + j * size) > 0; j--)
            for (k = 0; k < size; k++) {
                t = b[(j - 1) * size + k];
                b[(j - 1) * size + k] = b[j * size + k];
                b[j * size + k] = t;
            }
}
#endif

/* what the encoder leans on from the C library, checked before it runs */
static unsigned rnd_seed_free(int i)
{
    return (unsigned)(i * 2654435761u) >> 7;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static void libc_check(void)
{
    static UBYTE m[64];
    static int v[200];
    int i, ok = 1;
    for (i = 0; i < 64; i++) m[i] = (UBYTE)i;
    memmove(m + 1, m, 40);                      /* overlapping, up: as libwebp's Huffman tree does */
    for (i = 0; i < 64; i++)
        if (m[i] != (UBYTE)(i == 0 ? 0 : i <= 40 ? i - 1 : i)) ok = 0;
    printf("memmove up: %s\n", ok ? "ok" : "BROKEN");
    for (i = 0; i < 64; i++) m[i] = (UBYTE)i;
    memmove(m, m + 1, 40);
    ok = 1;
    for (i = 0; i < 40; i++) if (m[i] != (UBYTE)(i + 1)) ok = 0;
    printf("memmove down: %s\n", ok ? "ok" : "BROKEN");
    for (i = 0; i < 200; i++) v[i] = (int)((rnd_seed_free(i) % 50));
    qsort(v, 200, sizeof v[0], cmp_int);
    ok = 1;
    for (i = 1; i < 200; i++) if (v[i - 1] > v[i]) ok = 0;
    printf("qsort: %s\n", ok ? "ok" : "BROKEN");
    fflush(stdout);
}

struct buf {
    UBYTE *p;
    ULONG n, cap;
};

static LONG put(void *h, const UBYTE *p, ULONG n)
{
    struct buf *b = h;
    if (b->n + n > b->cap) {
        ULONG cap = (b->n + n) * 2;
        UBYTE *q = realloc(b->p, cap);
        if (!q)
            return 103;
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 0;
}

extern int (*webp_encode_trace)(int percent);
static int trace(int percent)
{
    printf(" %d", percent);
    fflush(stdout);
    return 1;
}

static ULONG ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (ULONG)d.ds_Minute * 3000UL + (ULONG)d.ds_Tick;
}

static ULONG seed = 7;
static ULONG rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return seed >> 8;
}

static int one(const char *name, ULONG w, ULONG h, int kind, LONG level)
{
    ULONG *px = malloc(w * h * 4), *keep = malloc(w * h * 4), x, y, t0, t1;
    struct buf b = { NULL, 0, 0 };
    int fw = 0, fh = 0, ok = 1;
    UBYTE *back;
    LONG err;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ULONG v;
            if (kind == 0)
                v = 0xFF000000UL | ((x * 255 / w) << 16) | ((y * 255 / h) << 8) | ((x + y) & 0xFF);
            else if (kind == 1)
                v = rnd() | ((rnd() & 1) ? 0xFF000000UL : 0x40000000UL);
            else
                v = ((x / 8 + y / 8) & 1) ? 0xFFFFFFFFUL : 0x80000000UL;
            px[y * w + x] = keep[y * w + x] = v;
        }
    printf("%s %lux%lu level %ld: encoding...", name, (unsigned long)w, (unsigned long)h, (long)level);
    fflush(stdout);
    webp_encode_trace = trace;
    t0 = ticks();
    err = webp_encode_lossless(px, w, h, w, level, put, &b);
    t1 = ticks();
    printf("\n");
    if (err) {
        printf("  ERROR %ld after %lu ticks\n", (long)err, (unsigned long)(t1 - t0));
        ok = 0;
    } else {
        back = WebPDecodeBGRA(b.p, b.n, &fw, &fh);   /* B G R A bytes = 0xAARRGGBB words, little-endian */
        if (!back || fw != (int)w || fh != (int)h)
            ok = 0;
        else
            for (x = 0; x < w * h && ok; x++) {
                const UBYTE *q = back + 4 * x;
                ULONG v = ((ULONG)q[3] << 24) | ((ULONG)q[2] << 16) | ((ULONG)q[1] << 8) | q[0];
                if (v != keep[x])
                    ok = 0;
            }
        if (back)
            WebPFree(back);
        printf("  %lu bytes in %lu ticks (1/50 s): %s\n", (unsigned long)b.n, (unsigned long)(t1 - t0),
               ok ? "SAME" : "DIFFERENT");
    }
    fflush(stdout);
    free(b.p);
    free(px);
    free(keep);
    return ok;
}

int main(int argc, char **argv)
{
    LONG max = argc > 1 ? atol(argv[1]) : 9, l;
    int fails = 0;
    static const LONG levels[] = { 0, 3, 6, 9 };
    unsigned i;
    libc_check();
    for (i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        l = levels[i];
        if (l > max)
            break;
        fails += !one("gradient", 64, 48, 0, l);
        fails += !one("noise", 64, 48, 1, l);
        fails += !one("checks", 320, 256, 2, l);
        fails += !one("gradient", 320, 256, 0, l);
    }
    printf("webpencamiga: %d failed\n", fails);
    return fails ? 10 : 0;
}
