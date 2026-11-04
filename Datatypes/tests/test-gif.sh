#!/bin/sh
# gfopengif.datatype's decoder on x86 or ARM64 cores: makes the test files
# (tests/make-gif.py; with Pillow installed it also checks Pillow reads the
# one-frame ones the same way), decodes every frame of each with
# gif/gifdecode.c built for this computer (tests/giftest.c) and compares the
# screens with what make-gif.py worked out. Then every file cut short at
# every length, and with each of its first 64 bytes changed, under
# AddressSanitizer and UBSan: nothing is read or written outside the file
# or the screen.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-gif.sh [OUTDIR]       (default work/gif-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/gif-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
if python3 -c 'import PIL' 2>/dev/null; then
    python3 "$HERE/tests/make-gif.py" "$OUT" --pillow
else
    python3 "$HERE/tests/make-gif.py" "$OUT"
fi
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/gif" \
    -o "$OUT/giftest" "$HERE/tests/giftest.c" "$HERE/gif/gifdecode.c"
cd "$OUT"
# shellcheck disable=SC2046
./giftest $(grep -v '#' expected.txt | cut -d' ' -f1) > got.txt
if diff -u expected.txt got.txt; then
    echo "giftest: all $(grep -vc '#' expected.txt) files, $(grep -c '#' expected.txt) frames as expected"
else
    echo "giftest: differs from expected.txt (above)"
    exit 1
fi
./giftest -cuts ./*.gif
