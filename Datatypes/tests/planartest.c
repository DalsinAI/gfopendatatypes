/*
 * common/include/odt_planar.h against the loop it replaced (one test for
 * every bit of every pixel, as ilbm.datatype, picture.datatype and
 * cdxl.datatype had it), on rows made here: every depth from 0 to 8 and
 * past it, every width from 1 to 80 and some up to 1000, planes missing
 * (NULL), pixels one to four bytes apart, and the bytes around each row
 * left as they were. tests/test-planar.sh runs it under AddressSanitizer
 * and UBSan, and as 68040 code under qemu-m68k.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/include/odt_planar.h"

static unsigned long seed = 2026;

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7fff;
}

/* The old way: each pixel's bit tested in each plane. */
static void old_row(const unsigned char *const *rows, unsigned depth, unsigned n, unsigned char *out, unsigned step)
{
    unsigned x, p;
    for (x = 0; x < n; x++) {
        unsigned v = 0;
        for (p = 0; p < depth && p < 8; p++)
            if (rows[p] && (rows[p][x >> 3] & (0x80 >> (x & 7))))
                v |= 1u << p;
        out[x * step] = (unsigned char)v;
    }
}

int main(void)
{
    unsigned cases = 0, bad = 0, k;
    for (k = 0; k < 20000; k++) {
        unsigned n = k < 8000 ? 1 + k % 80 : 1 + rnd() % 1000;
        unsigned depth = k % 11;                       /* 0 to 10: past 8 reads as 8 */
        unsigned step = 1 + rnd() % 4, bytes = (n + 7) / 8, p, i;
        unsigned char *planes[10], *a, *b;
        const unsigned char *rows[10];
        for (p = 0; p < 10; p++) {
            /* exactly the bytes the row has, so a read past them is seen */
            planes[p] = malloc(bytes);
            for (i = 0; i < bytes; i++)
                planes[p][i] = (unsigned char)rnd();
            rows[p] = rnd() % 7 ? planes[p] : NULL;
        }
        a = malloc(n * step + 8);
        b = malloc(n * step + 8);
        memset(a, 0x5a, n * step + 8);
        memset(b, 0x5a, n * step + 8);
        old_row(rows, depth, n, a, step);
        odt_planar_row(rows, depth, n, b, step);
        if (memcmp(a, b, n * step + 8)) {
            if (bad < 10)
                printf("FAIL depth %u width %u step %u\n", depth, n, step);
            bad++;
        }
        cases++;
        for (p = 0; p < 10; p++)
            free(planes[p]);
        free(a);
        free(b);
    }
    if (bad) {
        printf("planartest: %u of %u rows differ from the old loop\n", bad, cases);
        return 1;
    }
    printf("planartest: %u rows, every byte as the old loop made it\n", cases);
    return 0;
}
