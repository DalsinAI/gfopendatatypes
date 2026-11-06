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
    printf("%s %lux%lu level %ld: encoding...\n", name, (unsigned long)w, (unsigned long)h, (long)level);
    fflush(stdout);
    t0 = ticks();
    err = webp_encode_lossless(px, w, h, w, level, put, &b);
    t1 = ticks();
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
