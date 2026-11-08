#!/bin/sh
# ByteRun1 unpacked a row at a time matches unpacking the whole buffer.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/ilbmbr1-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O2 -Wall -Wextra -Werror $SAN -o "$OUT/ilbmbr1test" "$HERE/tests/ilbmbr1test.c"
"$OUT/ilbmbr1test"
echo "ilbm byterun: ok"
