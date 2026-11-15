#!/bin/sh
# Route-opaque ACMedia object lifetime and public-message adapter.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/acmedia-runtime-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN \
    -I"$HERE/tests/host" -I"$HERE/include" -I"$HERE/acmedia" \
    -o "$OUT/acmediaruntimetest" "$HERE/tests/acmediaruntimetest.c" \
    "$HERE/acmedia/amruntime.c" "$HERE/acmedia/amcore.c"
"$OUT/acmediaruntimetest"
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic \
    -I"$HERE/tests/host" -I"$HERE/include" -I"$HERE/acmedia" \
    -c "$HERE/acmedia/amruntime.c" -o "$OUT/amruntime.o"
data=$(size -A "$OUT/amruntime.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL amruntime: writable data ($data)"; exit 1; }
echo "acmedia runtime: C99 warning-clean, no writable data"
