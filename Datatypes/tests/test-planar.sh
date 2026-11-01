#!/bin/sh
# common/include/odt_planar.h, the bitplanes-to-pixels helper ilbm.datatype,
# picture.datatype and cdxl.datatype share, against the bit-at-a-time loop
# it replaced (tests/planartest.c), under AddressSanitizer and UBSan; with
# QEMU_M68K set (qemu-m68k), also built for the 68040 with
# m68k-linux-gnu-gcc and run there.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC          the host compiler (default cc)
#   SAN         sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   QEMU_M68K   qemu-m68k, to run it as 68k code too
#
# usage: sh tests/test-planar.sh [OUTDIR]       (default work/planar-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/planar-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -o "$OUT/planartest" "$HERE/tests/planartest.c"
"$OUT/planartest"
if [ -n "${QEMU_M68K:-}" ]; then
    m68k-linux-gnu-gcc -m68040 -O2 -static -Wall -Wextra -Werror -o "$OUT/planartest-m68k" "$HERE/tests/planartest.c"
    printf '68040 under qemu-m68k: '
    "$QEMU_M68K" -cpu m68040 "$OUT/planartest-m68k"
fi
