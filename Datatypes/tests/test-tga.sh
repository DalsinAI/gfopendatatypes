#!/bin/sh
# gfopentga.datatype's decoder on x86 or ARM64 cores: makes the test pictures
# (tests/make-tga.py; with Pillow installed it also checks Pillow reads them
# the same way), decodes each with tga/tgadecode.c built for this computer
# (tests/tgatest.c) and compares every picture with what make-tga.py says it
# decodes to. Then every file cut short at every length, and with each
# header byte changed, under AddressSanitizer and UBSan: nothing is read or
# written outside the file or the picture.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-tga.sh [OUTDIR]       (default work/tga-test)
# Then, for the Amiga: copy OUTDIR's .tga files and tests/dtpic over, run
# dtpic on them and compare with OUTDIR/expected.txt.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/tga-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
python3 "$HERE/tests/make-tga.py" "$OUT" --pillow
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/tga" \
    -o "$OUT/tgatest" "$HERE/tests/tgatest.c" "$HERE/tga/tgadecode.c"
cd "$OUT"
# shellcheck disable=SC2046
./tgatest $(cut -d' ' -f1 expected-host.txt) > got-host.txt
if diff -u expected-host.txt got-host.txt; then
    echo "tgatest: all $(wc -l < expected-host.txt) files decode as expected"
else
    echo "tgatest: differs from expected-host.txt (above)"
    exit 1
fi
# shellcheck disable=SC2046
./tgatest -cuts $(cut -d' ' -f1 expected-host.txt)
