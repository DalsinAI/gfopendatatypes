#!/bin/sh
# gfopentga.datatype's encoder on x86 or ARM64 cores, under AddressSanitizer
# and UBSan: tests/tgaenctest.c encodes the test pictures four ways and
# reads each back with our own decoder (tga/tgadecode.c), pixel for pixel;
# then, with Pillow installed, Pillow reads every file it wrote and must see
# the same picture (an outside reader: what other programs will make of a
# TGA saved on the Amiga).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-tgaenc.sh [OUTDIR]    (default work/tgaenc-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/tgaenc-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
rm -rf "$OUT"
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/tga" \
    -o "$OUT/tgaenctest" "$HERE/tests/tgaenctest.c" "$HERE/tga/tgaencode.c" "$HERE/tga/tgadecode.c"
mkdir -p "$OUT/files"
"$OUT/tgaenctest" "$OUT/files"
python3 -I - "$OUT/files" <<'EOF'
import sys
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    print("Pillow: not installed, its check skipped")
    sys.exit(0)
d = Path(sys.argv[1])
bad = 0
files = sorted(d.glob("*.tga"))
for t in files:
    with Image.open(t) as im:
        got = im.convert("RGBA").tobytes()
    if got != (d / (t.stem + ".rgba")).read_bytes():
        print("FAIL Pillow reads", t.name, "differently")
        bad += 1
print(f"Pillow reads {len(files) - bad} of {len(files)} files as written")
sys.exit(1 if bad else 0)
EOF

# The encoder is built to run as a job on another core later: plain C99, no writable data
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic -I"$HERE/tests/host" -I"$HERE/tga" \
    -c "$HERE/tga/tgaencode.c" -o "$OUT/tgaencode.o"
data=$(size -A "$OUT/tgaencode.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL tgaencode.c: writable data ($data)"; exit 1; }
echo "tgaencode.c: C99 -Wall -Wextra -Werror -pedantic, no writable data"
