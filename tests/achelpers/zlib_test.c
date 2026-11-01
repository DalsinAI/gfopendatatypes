/*
 * openamigaimage tests: zlib's crc32, crc32_z, adler32, adler32_z and
 * copy_with_crc as achelpers/zlibsums.sh builds them, over the stand-ins in
 * zstub/ (test_achelpers.sh): zlib's results for every call (running values,
 * adler32 halves of 65521 and up, NULL buffers, lengths 0 to 300, long
 * buffers and over a megabyte), and each call where it should go: under 32
 * bytes or NULL to zlib's own, else to AC090's helpers.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "zlib.h"

extern unsigned long zstub_crc_calls, zstub_adler_calls;
void copy_with_crc(z_streamp strm, Bytef *dst, long size);

#define BIG (0x100000u * 2 + 777)
static unsigned char src[BIG], dst[BIG];
static unsigned long long rng = 88172645463325252ull;
static unsigned long fails, checks, own, helped;

static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 16); }
static void check(int ok, const char *what, unsigned long a, unsigned long n)
{
    checks++;
    if (!ok && fails++ < 12)
        printf("FAIL %s (%#lx, %lu)\n", what, a, n);
}

static unsigned long ref_crc32(unsigned long crc, const unsigned char *buf, unsigned long len)
{
    unsigned long c;
    int k;
    if (!buf)
        return 0;
    c = ~crc & 0xffffffffUL;
    while (len--)
        for (c ^= *buf++, k = 0; k < 8; k++)
            c = c & 1 ? c >> 1 ^ 0xedb88320UL : c >> 1;
    return ~c & 0xffffffffUL;
}
static unsigned long ref_adler32(unsigned long adler, const unsigned char *buf, unsigned long len)
{
    unsigned long a = adler & 0xffff, s = adler >> 16 & 0xffff;
    if (len == 1) {
        a += buf[0];
        if (a >= 65521)
            a -= 65521;
        s += a;
        if (s >= 65521)
            s -= 65521;
        return s << 16 | a;
    }
    if (!buf)
        return 1;
    if (len < 16) {
        while (len--) {
            a += *buf++;
            s += a;
        }
        if (a >= 65521)
            a -= 65521;
        return s % 65521 << 16 | a;
    }
    while (len) {
        unsigned long k = len < 5552 ? len : 5552;
        for (len -= k; k; k--) {
            a += *buf++;
            s += a;
        }
        a %= 65521;
        s %= 65521;
    }
    return s << 16 | a;
}

/* where a call went: zlib's own (its stand-in counted it) or the helpers */
static void went(unsigned long before, unsigned long after, const unsigned char *b, unsigned long n, const char *what)
{
    int to_own = n < 32 || !b;
    check((after != before) == to_own, what, (unsigned long)(b != NULL), n);
    if (to_own)
        own++;
    else
        helped++;
}

int main(void)
{
    unsigned i, x = 2463534242u;
    int run;
    for (i = 0; i < BIG; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        src[i] = (unsigned char)(x >> 24);
    }
    memset(src + 200000, 0xff, 100000);
    for (run = 0; run < 4000; run++) {
        unsigned long n = rnd() % 8 ? rnd() % 301 : rnd() % 8 ? rnd() % 70000 : rnd() % 3 ? 5552 + rnd() % 4 : BIG - 8;
        unsigned long off = rnd() % (BIG - n), lo = rnd() % 65521, hi = rnd() % 65521, c = (unsigned long)rnd() << 16 ^ rnd();
        unsigned long before, a, want, got;
        const unsigned char *b = src + off;
        if (rnd() % 8 == 0) {                               /* halves zlib never makes */
            if (rnd() & 1)
                lo = 65521 + rnd() % 15;
            else
                hi = 65521 + rnd() % 15;
        }
        if (rnd() % 8 == 0 && n <= 100000) {                /* the largest sums, over a run of $FF */
            lo = hi = 65520;
            b = src + 200000 + rnd() % 4;
        }
        if (rnd() % 32 == 0 && n != 1)
            b = NULL;
        a = hi << 16 | lo;
        want = ref_crc32(c, b, n);
        before = zstub_crc_calls;
        got = rnd() & 1 ? crc32_z(c, b, n) : crc32(c, b, (uInt)n);
        check(got == want, "crc32", c, n);
        went(before, zstub_crc_calls, b, n, "crc32's way");
        want = ref_adler32(a, b, n);
        before = zstub_adler_calls;
        got = rnd() & 1 ? adler32_z(a, b, n) : adler32(a, b, (uInt)n);
        check(got == want, "adler32", a, n);
        went(before, zstub_adler_calls, b, n, "adler32's way");
        if (b && n <= 70000) {                              /* deflate's gzip path */
            z_stream s;
            memset(&s, 0, sizeof s);
            memset(dst, 0x5a, n + 16);
            s.next_in = (Bytef *)b;
            s.adler = c;
            before = zstub_crc_calls;
            copy_with_crc(&s, dst, (long)n);
            check(!memcmp(dst, b, n) && dst[n] == 0x5a && s.adler == ref_crc32(c, b, n) && s.next_in == b,
                  "copy_with_crc", c, n);
            went(before, zstub_crc_calls, b, n, "copy_with_crc's way");
        }
    }
    /* over a megabyte, so in pieces; from halves zlib never makes too */
    check(crc32_z(7, src, BIG) == ref_crc32(7, src, BIG), "crc32 over 2 MB", 7, BIG);
    check(adler32_z(0xfff3fff0UL, src + 1, BIG - 1) == ref_adler32(0xfff3fff0UL, src + 1, BIG - 1), "adler32 over 2 MB", 0xfff3fff0UL, BIG - 1);
    check(adler32_z(0x1fffaUL, src + 2, BIG - 2) == ref_adler32(0x1fffaUL, src + 2, BIG - 2), "adler32 over 2 MB", 0x1fffaUL, BIG - 2);
    check(crc32(0, (const Bytef *)"123456789", 9) == 0xcbf43926UL, "crc32 of 123456789", 0, 9);
    check(adler32(1, (const Bytef *)"123456789", 9) == 0x091e01deUL, "adler32 of 123456789", 0, 9);
    check(crc32(0, Z_NULL, 0) == 0 && adler32(0, Z_NULL, 0) == 1, "the initial values", 0, 0);
    printf("%s %lu of %lu checks; %lu calls to zlib's own, %lu to the helpers\n", fails ? "FAIL" : "ok  ",
           checks - fails, checks, own, helped);
    return fails != 0;
}
