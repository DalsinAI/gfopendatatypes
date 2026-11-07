#!/bin/sh
# gfopenpng.datatype's decoder (png/pngdecode.c) on x86 or ARM64 cores and on
# the 68040: makes the test pictures (tests/make-png.py: every colour type
# and bit depth, Adam7, tRNS, stored, fixed and dynamic deflate blocks; with
# Pillow installed it also checks Pillow reads them the same way), decodes
# each with tests/pngdectest.c and compares every picture with what
# make-png.py says it decodes to. Then every small file damaged 13 ways
# (the file cut short across its length, the deflate stream cut short, a
# byte changed, a bit flipped: 1,222 streams) under AddressSanitizer and
# UBSan: nothing is read or written outside the file or the picture, and a
# plain picture cut short keeps the rows it reached and clears the rest.
# With QEMU_M68K set (qemu-m68k), the decoder is built for the 68040 with
# m68k-linux-gnu-gcc (big-endian) and must give the same pictures and the
# same damaged-stream hashes there.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC          the host compiler (default cc)
#   SAN         sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   QEMU_M68K   qemu-m68k, to run the decoder as 68k code too
#   BIG         1: the 1024x768 pictures too (the speed numbers' pictures)
#
# usage: sh tests/test-pngdec.sh [OUTDIR]       (default work/pngdec-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/pngdec-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
DAMAGE=13
mkdir -p "$OUT"
# shellcheck disable=SC2046
python3 "$HERE/tests/make-png.py" "$OUT" --pillow $([ "${BIG:-0}" = 1 ] && echo --big)
# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/png" \
    -o "$OUT/pngdectest" "$HERE/tests/pngdectest.c" "$HERE/png/pngdecode.c"
cd "$OUT"
names=$(cut -d' ' -f1 expected.txt)
# shellcheck disable=SC2086
./pngdectest . $names > got.txt
if diff -u expected.txt got.txt; then
    echo "pngdectest: all $(wc -l < expected.txt) files decode as make-png.py says"
else
    echo "pngdectest: differs from expected.txt (above)"
    exit 1
fi
# shellcheck disable=SC2046
./pngdectest -damage $DAMAGE . $(cat damage.txt) > damage-got.txt || { cat damage-got.txt; exit 1; }
echo "pngdectest: $(($(wc -l < damage.txt) * DAMAGE)) damaged streams decoded or refused, nothing outside them; cut pictures keep their rows"
if [ -n "${QEMU_M68K:-}" ]; then
    m68k-linux-gnu-gcc -std=c99 -m68040 -O2 -static -Wall -Wextra -Werror -I"$HERE/tests/host" -I"$HERE/png" \
        -o pngdectest-m68k "$HERE/tests/pngdectest.c" "$HERE/png/pngdecode.c"
    # shellcheck disable=SC2086
    "$QEMU_M68K" -cpu m68040 ./pngdectest-m68k . $names > got-m68k.txt
    diff -u expected.txt got-m68k.txt
    # shellcheck disable=SC2046
    "$QEMU_M68K" -cpu m68040 ./pngdectest-m68k -damage $DAMAGE . $(cat damage.txt) > damage-m68k.txt
    diff -u damage-got.txt damage-m68k.txt
    echo "68040 under qemu-m68k: the same pictures, and the same from every damaged stream"
fi
echo "gfopenpng decoder: all pictures as made"
