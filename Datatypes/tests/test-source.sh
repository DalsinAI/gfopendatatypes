#!/bin/sh
# common/dtsource.h, which source the datatypes' dt_read_source() reads: a
# file or memory (DTST_MEMORY), tests/sourcetest.c, under AddressSanitizer
# and UBSan.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC   the host compiler (default cc)
#   SAN  sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-source.sh [OUTDIR]       (default work/source-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/source-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -o "$OUT/sourcetest" "$HERE/tests/sourcetest.c"
"$OUT/sourcetest"
