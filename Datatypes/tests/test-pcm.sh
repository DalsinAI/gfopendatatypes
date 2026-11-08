#!/bin/sh
# gfopenaiff and gfopenwave.datatype's core (pcm/pcmcore.c) on x86 or ARM64
# cores: tests/make-pcm.py writes AIFF, AIFC (NONE, sowt, fl32) and WAV
# (8, 16, 24 and 32 bits, float, WAVE_FORMAT_EXTENSIBLE, chunks in either
# order, odd lengths) with the samples each must give; tests/pcmtest.c
# reads every one, writes it again as AIFF and WAV in 16 and 8 bits and
# reads those back, under AddressSanitizer and UBSan. Where sox or file
# is installed, the files written are shown to it. Last, the core is
# compiled as C99 with -Wall -Wextra -Werror at -Os and must hold no
# writable data (it runs inside a library).
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   CC   the host compiler (default cc)    SAN  sanitizer flags (SAN= for none)
#
# usage: sh tests/test-pcm.sh [OUTDIR]       (default work/pcm-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/pcm-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT/written"
python3 "$HERE/tests/make-pcm.py" "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g $SAN -Wall -I"$HERE/pcm" -o "$OUT/pcmtest" "$HERE/tests/pcmtest.c" "$HERE/pcm/pcmcore.c"
fail=0
while read -r f ch n rate; do
    "$OUT/pcmtest" "$OUT/$f" "$OUT/$f.s16" "$ch" "$n" "$rate" "$OUT/written" || fail=1
done < "$OUT/list"
if command -v file >/dev/null; then
    for f in "$OUT"/written/a16.aif.out.aiff "$OUT"/written/w16.wav.out.wav; do
        echo "file: $(file -b "$f")"
    done
fi
if command -v sox >/dev/null; then
    sox --i "$OUT/written/a16.aif.out.aiff" >/dev/null && echo "sox reads the AIFF written" || fail=1
fi
$CC -std=c99 -Os -Wall -Wextra -Werror -c "$HERE/pcm/pcmcore.c" -o "$OUT/pcmcore.o"
data=$(size -A "$OUT/pcmcore.o" | awk '$1 == ".data" || $1 == ".bss" { s += $2 } END { print s + 0 }')
[ "$data" = 0 ] && echo "ok   pcmcore.c: no writable data" || { echo "FAIL pcmcore.c holds $data bytes of writable data"; fail=1; }
[ $fail = 0 ] && echo "all passed" || { echo "FAILED"; exit 1; }
