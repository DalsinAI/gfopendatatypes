/*
 * openamigaimage tests: a stand-in for zlib's crc32.c (test_achelpers.sh).
 * crc32_z a bit at a time, counting its calls; crc32 through it, as zlib's
 * does; and Chromium's copy_with_crc as it reads on a 68k, its SIMD branch
 * compiled out, which achelpers/zlibsums.sh looks for.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include "zlib.h"
#define zmemcpy memcpy

unsigned long zstub_crc_calls;

uLong ZEXPORT crc32_z(uLong crc, const Bytef *buf, z_size_t len)
{
    int k;
    zstub_crc_calls++;
    if (buf == Z_NULL)
        return 0;
    crc = ~crc & 0xffffffffUL;
    while (len--)
        for (crc ^= *buf++, k = 0; k < 8; k++)
            crc = crc & 1 ? crc >> 1 ^ 0xedb88320UL : crc >> 1;
    return ~crc & 0xffffffffUL;
}

uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len)
{
    return crc32_z(crc, buf, len);
}

void copy_with_crc(z_streamp strm, Bytef *dst, long size);

void copy_with_crc(z_streamp strm, Bytef *dst, long size)
{
    zmemcpy(dst, strm->next_in, size);
    strm->adler = crc32(strm->adler, dst, size);
}
