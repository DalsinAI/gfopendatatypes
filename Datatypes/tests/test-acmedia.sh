#!/bin/sh
# Route-opaque ACMedia feed: copy ownership, bounded high/low water, seek
# generation, per-track reset, remove, explicit EOF and short device writes.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/acmedia-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN \
    -I"$HERE/tests/host" -I"$HERE/acmedia" \
    -o "$OUT/acmediatest" "$HERE/tests/acmediatest.c" "$HERE/acmedia/amcore.c"
"$OUT/acmediatest"
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic \
    -I"$HERE/tests/host" -I"$HERE/acmedia" \
    -c "$HERE/acmedia/amcore.c" -o "$OUT/amcore.o"
data=$(size -A "$OUT/amcore.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL amcore: writable data ($data)"; exit 1; }
echo "acmedia core: C99 warning-clean, no writable data"
