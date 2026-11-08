#!/bin/sh
# webm.datatype's dither (webm/webm_dither.h) and CDXL's chunky films
# (cdxl_rgb_to_cube) from const tables (common/mkcubedither.py), against
# the formula they replaced (tests/webmdithertest.c): every colour (2^24)
# with each of the 16 dither values, for YUV, sRGB and CDXL's RGB; then
# random whole pictures (4:2:0, 4:2:2, 4:4:4, 4:4:0, sRGB, odd sizes and
# strides, buffers that cut rows off) byte for byte, under AddressSanitizer
# and UBSan; and the tables in the tree are what the script writes. With
# QEMU_M68K set (qemu-m68k), the pictures again as 68040 code, and with
# BENCH=1 too the 68k instructions a pixel, old and new, for 320x240 4:2:0.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-webm-dither.sh [OUTDIR]       (default work/webm-dither-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webm-dither-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT/gen/webm" "$OUT/gen/cdxl"
python3 "$HERE/common/mkcubedither.py" "$OUT/gen"
cmp "$OUT/gen/webm/webm_dither_tables.h" "$HERE/webm/webm_dither_tables.h"
cmp "$OUT/gen/cdxl/cdxl_cube_tables.h" "$HERE/cdxl/cdxl_cube_tables.h"
echo "tables: as common/mkcubedither.py writes them"
SRC="$HERE/tests/webmdithertest.c $HERE/cdxl/cdxl_core.c"
INC="-I$HERE/tests/host -I$HERE/webm -I$HERE/cdxl"
# shellcheck disable=SC2086
$CC -O2 -Wall -Wextra -Werror $INC -o "$OUT/webmdithertest" $SRC
"$OUT/webmdithertest" all
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN $INC -o "$OUT/webmdithertest-san" $SRC
"$OUT/webmdithertest-san" pictures 6000 2026
if [ -n "${QEMU_M68K:-}" ]; then
    # shellcheck disable=SC2086
    m68k-linux-gnu-gcc -m68040 -O2 -static -Wall -Wextra -Werror $INC -o "$OUT/webmdithertest-m68k" $SRC
    "$QEMU_M68K" -cpu m68040 "$OUT/webmdithertest-m68k" pictures 1500 1985
    echo "68040 under qemu-m68k: the same pictures"
    if [ "${BENCH:-0}" = 1 ]; then
        for b in bench benchcdxl; do
            for v in OLD NEW; do
                for r in 0 1; do
                    "$QEMU_M68K" -cpu m68040 -one-insn-per-tb -d exec,nochain -D "$OUT/log" \
                        "$OUT/webmdithertest-m68k" $b $v 320 240 $r > "$OUT/sum.$v"
                    eval "n$r=$(grep -c '^Trace' "$OUT/log")"
                done
                # shellcheck disable=SC2154
                echo "$b $v: $((n1 - n0)) instructions for 320x240, $(( (n1 - n0) * 10 / 76800 )) tenths a pixel ($(cat "$OUT/sum.$v"))"
            done
        done
    fi
fi
echo "webm dither: as the formula, every colour and every picture"
