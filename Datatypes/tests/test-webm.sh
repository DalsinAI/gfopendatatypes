#!/bin/sh
# webm.datatype's picture sizes on x86 or ARM64 cores, under AddressSanitizer
# and UBSan (tests/webmsizetest.c, no libvpx needed): WebM files with a
# crafted track header (65537 x 65536, which wraps to 65536 bytes in 32
# bits, and 8193 either way) are refused by the reader (webm/webm_demux.c);
# 8192 x 8192 and smaller are read; every file cut short and with each byte
# changed reads nothing outside it; and the dither (webm/webm_dither.h)
# writes nothing outside a buffer smaller than the decoded frame.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-webm.sh [OUTDIR]       (default work/webm-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webm-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/webm" \
    -o "$OUT/webmsizetest" "$HERE/tests/webmsizetest.c" "$HERE/webm/webm_demux.c"
"$OUT/webmsizetest"
