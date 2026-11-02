/*
 * gfopenimage: zlib's crc32, crc32_z, adler32 and adler32_z when build.sh
 * runs with AC_HELPERS=1, in front of zlib's own, which build.sh compiles
 * as oai_zlib_crc32, oai_zlib_crc32_z, oai_zlib_adler32 and
 * oai_zlib_adler32_z. A buffer of AC_SUM_MIN bytes (32) or more goes to
 * AC090's native checksums through ac_helpers.h's front doors, a megabyte
 * at a time; a shorter one, or none (NULL), to zlib's own. Every result is
 * zlib's: the helpers' C bodies follow zlib's paths, and AC090 leaves to
 * them the calls it would answer otherwise (an adler32 half of 65521 or
 * more). zlib's API and ABI stay as they were; only these four functions
 * move. With OAI_COPY_WITH_CRC (build.sh found Chromium's copy_with_crc,
 * deflate's gzip path, as the plain copy and crc32 below) that moves here
 * too, since inside crc32.c it would call zlib's own crc32.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "zlib.h"
#include "ac_helpers.h"

uLong oai_zlib_crc32(uLong crc, const Bytef *buf, uInt len);
uLong oai_zlib_crc32_z(uLong crc, const Bytef *buf, z_size_t len);
uLong oai_zlib_adler32(uLong adler, const Bytef *buf, uInt len);
uLong oai_zlib_adler32_z(uLong adler, const Bytef *buf, z_size_t len);

uLong ZEXPORT crc32_z(uLong crc, const Bytef *buf, z_size_t len)
{
    if (len < AC_SUM_MIN || !buf)
        return oai_zlib_crc32_z(crc, buf, len);
    return ac_crc32_auto(crc, buf, len);
}

uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len)
{
    if (len < AC_SUM_MIN || !buf)
        return oai_zlib_crc32(crc, buf, len);
    return ac_crc32_auto(crc, buf, len);
}

uLong ZEXPORT adler32_z(uLong adler, const Bytef *buf, z_size_t len)
{
    if (len < AC_SUM_MIN || !buf)
        return oai_zlib_adler32_z(adler, buf, len);
    return ac_adler32_auto(adler, buf, len);
}

uLong ZEXPORT adler32(uLong adler, const Bytef *buf, uInt len)
{
    if (len < AC_SUM_MIN || !buf)
        return oai_zlib_adler32(adler, buf, len);
    return ac_adler32_auto(adler, buf, len);
}

#ifdef OAI_COPY_WITH_CRC
/* deflate.h's declaration, which this file does not include */
void copy_with_crc(z_streamp strm, Bytef *dst, long size);

void copy_with_crc(z_streamp strm, Bytef *dst, long size)
{
    ac_memcpy_auto(dst, strm->next_in, (size_t)size);
    strm->adler = crc32(strm->adler, dst, (uInt)size);
}
#endif
