#!/bin/sh
# zlib's checksums for build.sh with AC_HELPERS=1, run in zlib's source
# folder: crc32.c and adler32.c with zlib's crc32, crc32_z, adler32 and
# adler32_z renamed oai_zlib_*, and ac_zlib.c, which takes the public names
# and sends long buffers to AC090's native checksums, compiled into OUTDIR
# (crc32.o, adler32.o, ac_zlib.o). Chromium's copy_with_crc (deflate's gzip
# path) calls crc32 inside crc32.c, so it moves to ac_zlib.c too when it is
# the plain copy and crc32 that ac_zlib.c does.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC, CFLAGS   the compiler, and zlib's flags with the helpers' -I and
#                the forced include
#
# usage: sh achelpers/zlibsums.sh OUTDIR
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:?usage: zlibsums.sh OUTDIR}
mkdir -p "$out"
crc="-Dcrc32=oai_zlib_crc32 -Dcrc32_z=oai_zlib_crc32_z" cwc=
if grep -q 'copy_with_crc' crc32.c && grep -q 'zmemcpy(dst, strm->next_in, size);' crc32.c \
        && grep -q 'strm->adler = crc32(strm->adler, dst, size);' crc32.c; then
    crc="$crc -Dcopy_with_crc=oai_zlib_copy_with_crc" cwc=-DOAI_COPY_WITH_CRC
    echo "zlib: crc32 and adler32 through AC090's helpers, deflate's gzip crc too (copy_with_crc)"
else
    echo "zlib: crc32 and adler32 through AC090's helpers; deflate's gzip crc stays zlib's own (no plain copy_with_crc in crc32.c)"
fi
# shellcheck disable=SC2086
$CC $CFLAGS $crc -c crc32.c -o "$out/crc32.o"
# shellcheck disable=SC2086
$CC $CFLAGS -Dadler32=oai_zlib_adler32 -Dadler32_z=oai_zlib_adler32_z -c adler32.c -o "$out/adler32.o"
# shellcheck disable=SC2086
$CC $CFLAGS $cwc -c "$here/ac_zlib.c" -o "$out/ac_zlib.o"
