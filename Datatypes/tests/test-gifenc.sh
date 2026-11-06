#!/bin/sh
# gfopengif.datatype's encoder on x86 or ARM64 cores, under AddressSanitizer
# and UBSan: tests/gifenctest.c encodes the test pictures and reads each back
# with our own decoder (gif/gifdecode.c); then, with Pillow installed,
# Pillow reads every file it wrote and must see the same picture as our
# decoder did (an outside reader: what other programs will make of a GIF
# saved on the Amiga). A transparent pixel is the same whatever its colour.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-gifenc.sh [OUTDIR]    (default work/gifenc-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/gifenc-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
rm -rf "$OUT"
mkdir -p "$OUT/files"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/gif" \
    -o "$OUT/gifenctest" "$HERE/tests/gifenctest.c" "$HERE/gif/gifencode.c" "$HERE/gif/gifdecode.c"
"$OUT/gifenctest" "$OUT/files"
python3 -I - "$OUT/files" <<'PY'
import sys
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    print("Pillow: not installed, its check skipped")
    sys.exit(0)
d = Path(sys.argv[1])
bad = 0
files = sorted(d.glob("*.gif"))
for g in files:
    with Image.open(g) as im:
        got = im.convert("RGBA").tobytes()
    want = (d / (g.stem + ".rgba")).read_bytes()
    same = len(got) == len(want) and all(
        got[i + 3] == want[i + 3] and (got[i + 3] == 0 or got[i:i + 3] == want[i:i + 3])
        for i in range(0, len(want), 4))
    if not same:
        print("FAIL Pillow reads", g.name, "differently")
        bad += 1
print(f"Pillow reads {len(files) - bad} of {len(files)} files as our decoder does")
sys.exit(1 if bad else 0)
PY

# The encoder is built to run as a job on another core later: plain C99, no writable data
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic -I"$HERE/tests/host" -I"$HERE/gif" \
    -c "$HERE/gif/gifencode.c" -o "$OUT/gifencode.o"
data=$(size -A "$OUT/gifencode.o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL gifencode.c: writable data ($data)"; exit 1; }
echo "gifencode.c: C99 -Wall -Wextra -Werror -pedantic, no writable data"
