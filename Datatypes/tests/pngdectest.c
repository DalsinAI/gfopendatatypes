/*
 * gfopenpng.datatype's decoder (png/pngdecode.c) on the host, and built for
 * the 68040 under qemu-m68k: reads each file named and prints a line in the
 * form tests/make-png.py's expected.txt has, with an FNV-1a hash of the
 * picture (every pixel's A, R, G, B bytes, rows top to bottom):
 *
 *     NAME WIDTH HEIGHT alpha=0|1 fnv=HASH      (or NAME invalid)
 *
 *   pngdectest DIR NAME...            DIR/NAME.png for each NAME
 *   pngdectest -damage N DIR NAME...  each file damaged N ways: the file
 *                                     cut short at lengths across it, the
 *                                     same with the IDAT it ends in made
 *                                     to fit (the deflate stream cut
 *                                     short), a byte changed and a bit
 *                                     flipped (the CRCs left as they
 *                                     were); one line a file
 *                                     with a hash over every outcome. A
 *                                     plain picture cut short must keep
 *                                     the rows it reached and clear the
 *                                     rest. Each copy is exactly its
 *                                     length, so under AddressSanitizer a
 *                                     read past it stops the test.
 *   pngdectest -bench N FILE          decodes FILE N times (0: only reads
 *                                     it) as the class does, in strips of
 *                                     32 rows, for counting instructions
 *
 * Every picture is decoded whole (png_decode) and in strips of 3 and of 32
 * rows (png_unpack, png_strip), which must agree.
 *
 * The same answers on x86 and on the 68040 (big-endian) are the point of
 * running it twice; tests/test-pngdec.sh compares both with make-png.py.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pngdecode.h"

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

static ULONG fnv_bytes(ULONG h, const ULONG *argb, ULONG n)
{
    ULONG i;
    for (i = 0; i < n; i++) {
        int s;
        for (s = 24; s >= 0; s -= 8)
            h = (h ^ ((argb[i] >> s) & 255)) * 0x01000193UL;
    }
    return h;
}

static int bench;

/* The decode as gfopenpng.datatype does it (png/pngclass.c): the head, the
 * IDAT data gathered, the stream unpacked, then the rows handed on in
 * strips of rows rows (into one strip's buffer, as the class does; copied
 * into the picture at full unless it is NULL). 0, or -1. */
static int strips(const UBYTE *file, ULONG len, struct png_head *h, ULONG rows, ULONG *full)
{
    UBYTE *idat = NULL, *raw = NULL;
    ULONG *strip = NULL, y, n;
    void *work = NULL;
    struct png_rows r;
    int rc = -1;
    if (png_read_head(file, len, h))
        return -1;
    if ((idat = malloc(h->idat_len ? h->idat_len : 1)) && (raw = malloc(h->raw_len ? h->raw_len : 1))
        && (strip = malloc((size_t)h->width * rows * 4)) && (work = malloc(PNG_WORKSIZE))) {
        png_gather(file, len, idat);
        if (png_unpack(h, idat, raw, work, &r) == 0) {
            for (y = 0; y < h->height; y += n) {
                n = h->height - y < rows ? h->height - y : rows;
                if (!bench)
                    memset(strip, 0xA5, (size_t)h->width * n * 4);
                png_strip(h, raw, &r, strip, n);
                if (full)
                    memcpy(full + (size_t)y * h->width, strip, (size_t)h->width * n * 4);
            }
            rc = 0;
        }
    }
    free(work);
    free(strip);
    free(raw);
    free(idat);
    return rc;
}

/* The whole picture with png_decode(), into a picture filled with garbage
 * first (what a row never written would show); then again in strips of 3
 * and of 32 rows, which must give the same. 0 and the picture, or -1. */
static int decode(const UBYTE *file, ULONG len, struct png_head *h, ULONG **argb)
{
    UBYTE *idat = NULL, *raw = NULL;
    void *work = NULL;
    size_t size;
    int rc = -1;
    *argb = NULL;
    if (png_read_head(file, len, h))
        return -1;
    size = (size_t)h->width * h->height * 4;
    if ((idat = malloc(h->idat_len ? h->idat_len : 1)) && (raw = malloc(h->raw_len ? h->raw_len : 1))
        && (*argb = malloc(size)) && (work = malloc(PNG_WORKSIZE))) {
        png_gather(file, len, idat);
        memset(*argb, 0xA5, size);
        rc = png_decode(h, idat, raw, *argb, work) == 0 ? 0 : -1;
    }
    free(work);
    free(raw);
    free(idat);
    if (!rc) {
        static const ULONG sizes[2] = { 3, 32 };
        ULONG *again = malloc(size), k;
        for (k = 0; k < 2; k++) {
            memset(again, 0x5A, size);
            if (!again || strips(file, len, h, sizes[k], again) || memcmp(again, *argb, size)) {
                printf("strips of %lu rows differ from the whole picture\n", (unsigned long)sizes[k]);
                exit(1);
            }
        }
        free(again);
    }
    if (rc) {
        free(*argb);
        *argb = NULL;
    }
    return rc;
}

static ULONG seed;

static ULONG rnd(ULONG n)
{
    seed = seed * 1103515245UL + 12345UL;
    return (seed >> 8) % n;
}

/* Rows that came must be the picture's; past the first that differs (the
 * row the stream ended in) all must be clear. */
static int prefix_ok(const ULONG *full, const ULONG *got, ULONG w, ULONG h)
{
    ULONG y = 0, i;
    while (y < h && !memcmp(full + y * w, got + y * w, w * 4))
        y++;
    for (y++; y < h; y++)
        for (i = 0; i < w; i++)
            if (got[y * w + i])
                return 0;
    return 1;
}

static int damage(const char *name, const UBYTE *file, ULONG len, ULONG count)
{
    struct png_head h0, h;
    ULONG *full, *argb, k, i, hash = 0x811C9DC5UL, decoded = 0, refused = 0;
    if (decode(file, len, &h0, &full)) {
        printf("%s invalid\n", name);
        return 0;
    }
    seed = len;
    for (k = 0; k < count; k++) {
        ULONG n = len, kind = k % 4, size, pos;
        UBYTE *copy;
        if (kind <= 1)
            n = 8 + (ULONG)((unsigned long long)(len - 8) * (k + 1) / (count + 1));
        size = n;
        if (kind == 1) {                /* the stream cut short: the IDAT it ends in shortened to fit */
            for (pos = 8; pos + 8 <= n; pos += 12 + ((ULONG)file[pos] << 24 | (ULONG)file[pos + 1] << 16
                                                        | (ULONG)file[pos + 2] << 8 | file[pos + 3]))
                if (n < pos + 12 + ((ULONG)file[pos] << 24 | (ULONG)file[pos + 1] << 16
                                    | (ULONG)file[pos + 2] << 8 | file[pos + 3]))
                    break;
            if (pos + 8 <= n && !memcmp(file + pos + 4, "IDAT", 4))
                size = n + 4;           /* and a CRC (not checked) after */
            else
                pos = 0;
        }
        if (!(copy = calloc(size, 1)))
            return 0;
        memcpy(copy, file, n);
        if (size > n) {
            ULONG d = n - pos - 8;
            copy[pos] = (UBYTE)(d >> 24);
            copy[pos + 1] = (UBYTE)(d >> 16);
            copy[pos + 2] = (UBYTE)(d >> 8);
            copy[pos + 3] = (UBYTE)d;
        }
        n = size;
        if (kind == 2)
            copy[33 + rnd(len - 33 - 12)] = (UBYTE)rnd(256);       /* past IHDR, before IEND */
        else if (kind == 3)
            copy[33 + rnd(len - 33 - 12)] ^= (UBYTE)(1 << rnd(8));
        if (decode(copy, n, &h, &argb) == 0) {
            hash = (hash ^ h.width ^ (h.height << 16) ^ h.alpha) * 0x01000193UL;
            hash = fnv_bytes(hash, argb, h.width * h.height);
            for (i = 0; i < h.width * h.height; i++)
                if (argb[i] == 0xA5A5A5A5UL) {
                    printf("%s damaged %lu: pixel %lu never written\n", name, (unsigned long)k, (unsigned long)i);
                    return 0;
                }
            if (kind <= 1 && !h0.interlace && !prefix_ok(full, argb, h.width, h.height)) {
                printf("%s cut at %lu: rows past the end are not clear\n", name, (unsigned long)n);
                return 0;
            }
            free(argb);
            decoded++;
        } else {
            hash = (hash ^ 0xFFu) * 0x01000193UL;
            refused++;
        }
        free(copy);
    }
    free(full);
    printf("%s damaged=%lu decoded=%lu refused=%lu fnv=%08lx\n", name, (unsigned long)count,
           (unsigned long)decoded, (unsigned long)refused, (unsigned long)hash);
    return 1;
}

int main(int argc, char **argv)
{
    static char path[4096];
    int i = 1, ok = 1;
    ULONG count = 0;
    if (argc == 4 && !strcmp(argv[1], "-bench")) {
        struct png_head h;
        ULONG len = 0, n = strtoul(argv[2], NULL, 10);
        UBYTE *file = slurp(argv[3], &len);
        bench = 1;
        if (!file || png_read_head(file, len, &h))
            return 1;
        while (n--)                     /* as the class does it: strips of 32 rows */
            if (strips(file, len, &h, 32, NULL))
                return 1;
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-damage")) {
        count = strtoul(argv[2], NULL, 10);
        i = 3;
    }
    if (i >= argc)
        return 2;
    for (i++; i < argc; i++) {
        struct png_head h;
        ULONG len = 0, *argb;
        UBYTE *file;
        snprintf(path, sizeof(path), "%s/%s.png", argv[count ? 3 : 1], argv[i]);
        if (!(file = slurp(path, &len))) {
            printf("%s unreadable\n", argv[i]);
            ok = 0;
            continue;
        }
        if (count)
            ok &= damage(argv[i], file, len, count);
        else if (decode(file, len, &h, &argb))
            printf("%s invalid\n", argv[i]);
        else {
            printf("%s %lu %lu alpha=%d fnv=%08lx\n", argv[i], (unsigned long)h.width, (unsigned long)h.height,
                   h.alpha ? 1 : 0, (unsigned long)fnv_bytes(0x811C9DC5UL, argb, h.width * h.height));
            free(argb);
        }
        free(file);
    }
    return ok ? 0 : 1;
}
