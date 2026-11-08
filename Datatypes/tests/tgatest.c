/*
 * gfopentga.datatype's decoder (tga/tgadecode.c) on x86 or ARM64 cores: reads
 * each file named and prints what tests/dtpic.c prints on the Amiga, with
 * an FNV-1a hash of every byte of the picture after it, for
 * tests/test-tga.sh to compare with tests/make-tga.py's expected-host.txt:
 *
 *     NAME WIDTH HEIGHT alpha=0|1 CHECKSUM fnv=HASH      (or NAME invalid)
 *
 * With -cuts first, it also decodes every file cut short at every length
 * and with every byte of its header changed, and prints only a count: run
 * under AddressSanitizer, a read or write outside the file or the picture
 * stops it.
 *
 * Every picture (the cut ones too) is also decoded as gfopentga.datatype
 * hands it on, in strips of 3 and of 32 rows with tga_start() and
 * tga_strip() (again, opaque, when the alpha comes out empty), which must
 * give the same picture.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tgadecode.h"

static UBYTE *slurp(const char *path, ULONG *len)
{
    FILE *f = fopen(path, "rb");
    UBYTE *buf = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
        && (buf = malloc(n ? (size_t)n : 1)) && fread(buf, 1, (size_t)n, f) == (size_t)n)
        *len = (ULONG)n;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

/* As tga/tgaclass.c does it: strips of rows rows, each put where it goes,
 * and all again, opaque, when the alpha comes out empty. 0, or -1. */
static int strips(const UBYTE *file, ULONG len, const struct tga_head *h, ULONG rows, ULONG *argb, UBYTE *alpha)
{
    struct tga_rows r;
    ULONG *pal = NULL, *strip = malloc((size_t)h->width * rows * 4), y, n, pass;
    int rc = -1;
    if (h->type == 1)
        pal = malloc((size_t)h->map_len * 4);
    *alpha = h->alpha;
    for (pass = 0; pass < 2 && strip && (h->type != 1 || pal); pass++) {
        if (tga_start(h, file, len, pal, &r, (UBYTE)pass))
            break;
        for (y = 0; y < h->height; y += n) {
            ULONG top;
            n = h->height - y < rows ? h->height - y : rows;
            top = h->top_down ? y : h->height - y - n;
            memset(strip, 0xA5, (size_t)h->width * n * 4);
            tga_strip(h, file, len, pal, &r, strip, n);
            memcpy(argb + (size_t)top * h->width, strip, (size_t)h->width * n * 4);
        }
        rc = 0;
        if (pass || !h->alpha || (r.seen & 0xFF000000UL))
            break;
        *alpha = 0;                      /* an empty alpha channel: again, opaque */
    }
    free(pal);
    free(strip);
    return rc;
}

/* The whole picture, then in strips (which must agree). */
static int decode_whole(const UBYTE *file, ULONG len, struct tga_head *h, ULONG **argb, UBYTE *alpha);

/* 0 and the picture, or -1 when the decoder refuses the file. */
static int decode(const UBYTE *file, ULONG len, struct tga_head *h, ULONG **argb, UBYTE *alpha)
{
    static const ULONG sizes[2] = { 3, 32 };
    size_t size;
    ULONG *again, k;
    UBYTE a2;
    if (decode_whole(file, len, h, argb, alpha))
        return -1;
    size = (size_t)h->width * h->height * 4;
    if (!(again = malloc(size)))
        exit(20);
    for (k = 0; k < 2; k++) {
        memset(again, 0x5A, size);
        if (strips(file, len, h, sizes[k], again, &a2) || a2 != *alpha || memcmp(again, *argb, size)) {
            printf("strips of %lu rows differ from the whole picture\n", (unsigned long)sizes[k]);
            exit(1);
        }
    }
    free(again);
    return 0;
}

static int decode_whole(const UBYTE *file, ULONG len, struct tga_head *h, ULONG **argb, UBYTE *alpha)
{
    ULONG *pal = NULL;
    int rc = -1;
    *argb = NULL;
    if (tga_read_head(file, len, h))
        return -1;
    *argb = malloc((size_t)h->width * h->height * 4);
    if (h->type == 1)
        pal = malloc((size_t)h->map_len * 4);
    if (*argb && (h->type != 1 || pal) && tga_decode(h, file, len, *argb, pal, alpha) == 0)
        rc = 0;
    free(pal);
    if (rc) {
        free(*argb);
        *argb = NULL;
    }
    return rc;
}

/* A copy of exactly len bytes, so a read past its end is caught. */
static void try_copy(const UBYTE *src, ULONG len, ULONG *tries)
{
    struct tga_head h;
    ULONG *argb;
    UBYTE alpha, *copy = malloc(len ? len : 1);
    if (!copy)
        exit(20);
    memcpy(copy, src, len);
    if (tga_read_head(copy, len, &h) == 0 && h.width * h.height <= (4UL << 20)) {
        if (decode(copy, len, &h, &argb, &alpha) == 0)
            free(argb);
    }
    free(copy);
    (*tries)++;
}

static void cuts(const UBYTE *file, ULONG len, ULONG *tries)
{
    static const UBYTE values[] = { 0, 1, 2, 3, 8, 9, 10, 11, 15, 16, 24, 32, 0x30, 0x7F, 0x80, 0xFF };
    UBYTE *copy = malloc(len);
    ULONG n, i, v;
    if (!copy)
        exit(20);
    for (n = 0; n <= len; n++)
        try_copy(file, n, tries);
    for (i = 0; i < 18 && i < len; i++)
        for (v = 0; v < sizeof(values); v++) {
            memcpy(copy, file, len);
            copy[i] = values[v];
            try_copy(copy, len, tries);
        }
    free(copy);
}

int main(int argc, char **argv)
{
    int i = 1, check_cuts = 0;
    ULONG tries = 0;
    if (argc > 1 && !strcmp(argv[1], "-cuts")) {
        check_cuts = 1;
        i++;
    }
    for (; i < argc; i++) {
        struct tga_head h;
        ULONG len = 0, p, n, sum = 0, fnv = 0x811C9DC5UL, *argb;
        UBYTE alpha = 0, *file = slurp(argv[i], &len);
        if (!file) {
            printf("%s unreadable\n", argv[i]);
            continue;
        }
        if (check_cuts) {
            cuts(file, len, &tries);
            free(file);
            continue;
        }
        if (decode(file, len, &h, &argb, &alpha)) {
            printf("%s invalid\n", argv[i]);
            free(file);
            continue;
        }
        for (p = 0, n = h.width * h.height; p < n; p++) {
            ULONG v = alpha ? argb[p] : (argb[p] | 0xFF000000UL);
            int s;
            sum = sum * 31 + v;
            for (s = 24; s >= 0; s -= 8)
                fnv = (fnv ^ ((v >> s) & 255)) * 0x01000193UL;
        }
        printf("%s %lu %lu alpha=%d %08lx fnv=%08lx\n", argv[i], (unsigned long)h.width, (unsigned long)h.height,
               alpha ? 1 : 0, (unsigned long)sum, (unsigned long)fnv);
        free(argb);
        free(file);
    }
    if (check_cuts)
        printf("tgatest -cuts: %lu cut or changed files decoded or refused\n", (unsigned long)tries);
    return 0;
}
