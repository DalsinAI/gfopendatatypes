/*
 * gfopenimage tests: a stand-in for zlib's adler32.c (test_achelpers.sh).
 * adler32_z a byte at a time on zlib's paths (its results for halves of
 * 65521 and up depend on them), counting its calls; adler32 through it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "zlib.h"

unsigned long zstub_adler_calls;

uLong ZEXPORT adler32_z(uLong adler, const Bytef *buf, z_size_t len)
{
    unsigned long a = adler & 0xffff, s = adler >> 16 & 0xffff;
    zstub_adler_calls++;
    if (len == 1) {
        a += buf[0];
        if (a >= 65521)
            a -= 65521;
        s += a;
        if (s >= 65521)
            s -= 65521;
        return s << 16 | a;
    }
    if (buf == Z_NULL)
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
        z_size_t k = len < 5552 ? len : 5552;
        for (len -= k; k; k--) {
            a += *buf++;
            s += a;
        }
        a %= 65521;
        s %= 65521;
    }
    return s << 16 | a;
}

uLong ZEXPORT adler32(uLong adler, const Bytef *buf, uInt len)
{
    return adler32_z(adler, buf, len);
}
