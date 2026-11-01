/*
 * gzip.datatype's encoder (gz_encode() in gzip/gzcore.c) on x86 or ARM64
 * cores, over the same z.library calls as on the Amiga (tests/zlhost.c:
 * GFOpenZlib's core, or the host's zlib standing in). Each input is encoded
 * with each wrapper (gzip, zlib, raw deflate) at levels -1, 0, 1 and 9,
 * through a small work buffer so the output comes in many pieces, and
 * written to OUTDIR as NAME.KIND.LLEVEL with the input beside it (NAME.in),
 * for tests/test-gzenc.sh to inflate with Python's zlib, an outside reader.
 *
 *     gzenctest OUTDIR
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gzcore.h"

struct sink {
    FILE *f;
    ULONG n, calls;
    LONG fail_at;                 /* put() fails on this call (0: never) */
};

static LONG put(void *h, const UBYTE *p, ULONG n)
{
    struct sink *s = h;
    s->calls++;
    if (s->fail_at && (LONG)s->calls == s->fail_at)
        return 225;               /* ERROR_DISK_FULL */
    if (s->f && fwrite(p, 1, n, s->f) != n)
        return 225;
    s->n += n;
    return 0;
}

static ULONG seed = 777;
static ULONG rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 8) & 0xFFFFFF;
}

int main(int argc, char **argv)
{
    static const char *kinds[] = { "", "gzip", "zlib", "raw" };
    static const LONG levels[] = { -1, 0, 1, 9 };
    struct { const char *name; ULONG len; int fill; } ins[] = {
        { "empty", 0, 0 }, { "one", 1, 0 }, { "text", 50000, 1 }, { "zeros", 200000, 2 },
        { "noise", 70000, 3 }, { "mixed", 300001, 4 },
    };
    UBYTE work[257];              /* small, so the output comes in many pieces */
    int fails = 0, made = 0;
    unsigned i, k, l;

    if (argc != 2) {
        fprintf(stderr, "usage: gzenctest OUTDIR\n");
        return 2;
    }
    for (i = 0; i < sizeof ins / sizeof ins[0]; i++) {
        UBYTE *in = malloc(ins[i].len ? ins[i].len : 1);
        char path[512];
        FILE *f;
        ULONG j;
        for (j = 0; j < ins[i].len; j++) {
            switch (ins[i].fill) {
            case 1: in[j] = (UBYTE)("the quick brown fox jumps over the lazy dog. "[j % 45]); break;
            case 2: in[j] = 0; break;
            case 3: in[j] = (UBYTE)rnd(); break;
            case 4: in[j] = (j / 1000) & 1 ? (UBYTE)rnd() : (UBYTE)(j & 7); break;
            default: in[j] = 'A';
            }
        }
        snprintf(path, sizeof path, "%s/%s.in", argv[1], ins[i].name);
        if (!(f = fopen(path, "wb")) || fwrite(in, 1, ins[i].len, f) != ins[i].len) {
            printf("FAIL can't write %s\n", path);
            return 1;
        }
        fclose(f);
        for (k = 1; k <= 3; k++)
            for (l = 0; l < 4; l++) {
                struct sink s = { 0, 0, 0, 0 };
                LONG rc;
                snprintf(path, sizeof path, "%s/%s.%s.L%ld", argv[1], ins[i].name, kinds[k], (long)levels[l]);
                if (!(s.f = fopen(path, "wb"))) {
                    printf("FAIL can't write %s\n", path);
                    return 1;
                }
                rc = gz_encode(NULL, (LONG)k, levels[l], in, ins[i].len, put, &s, work, sizeof work);
                fclose(s.f);
                if (rc) {
                    printf("FAIL %s: gz_encode returned %ld\n", path, (long)rc);
                    fails++;
                } else
                    made++;
            }
        /* a put() that fails stops the encode, and its code comes back */
        if (ins[i].len > 10000) {
            struct sink s = { 0, 0, 0, 1 };
            LONG rc = gz_encode(NULL, GZ_GZIP, 9, in, ins[i].len, put, &s, work, sizeof work);
            if (rc != 225 || s.calls != 1) {
                printf("FAIL %s: a failing write gave %ld after %lu calls\n", ins[i].name, (long)rc,
                       (unsigned long)s.calls);
                fails++;
            }
        }
        free(in);
    }
    printf("gzenctest: %d files written, %d failed\n", made, fails);
    return fails ? 1 : 0;
}
