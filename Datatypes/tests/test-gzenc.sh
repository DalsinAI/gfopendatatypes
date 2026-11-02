#!/bin/sh
# gzip.datatype's encoder (gz_encode() in gzip/gzcore.c, what DTM_WRITE with
# SDTA_WriteCoded runs) on x86 or ARM64 cores, under AddressSanitizer and
# UBSan, over GFOpenZlib's core as z.library (the host's zlib stands in when
# there is no amicradle-guest checkout, as in tests/test-stream.sh).
# tests/gzenctest.c writes every input in gzip, zlib and raw deflate at four
# levels; Python's zlib, an outside reader, must inflate each to the input.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC, CXX, SAN, AMIGACHROME_GUEST   as tests/test-stream.sh
#
# usage: sh tests/test-gzenc.sh [OUTDIR]     (default work/gzenc-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/gzenc-test"}
CC=${CC:-cc}
CXX=${CXX:-c++}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
rm -rf "$OUT"
mkdir -p "$OUT/files"

G=${AMIGACHROME_GUEST:-}
for g in "$HERE/../../amicradle-guest" "$HERE/../../guest"; do
    [ -n "$G" ] || { [ -f "$g/libraries/openzlib/zlib_core.cpp" ] && G=$g; } || true
done
if [ -n "$G" ] && [ -f "$G/libraries/openzlib/zlib_core.cpp" ]; then
    OZ="$G/libraries/openzlib"
    # shellcheck disable=SC2086
    $CXX -std=gnu++23 -fno-exceptions -fno-rtti -O1 -g $SAN -c "$OZ/zlib_core.cpp" -o "$OUT/zlib_core.o"
    ZL="-I$OZ $OUT/zlib_core.o -lstdc++"
    echo "deflate: GFOpenZlib's core (z.library), $OZ"
else
    ZL="-DZLHOST_SYSTEM -lz"
    echo "deflate: the host's zlib, standing in for z.library (no amicradle-guest checkout)"
fi
# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/common" -I"$HERE/stream" \
    -I"$HERE/gzip" -o "$OUT/gzenctest" "$HERE/tests/gzenctest.c" "$HERE/tests/zlhost.c" \
    "$HERE/stream/sdtcore.c" "$HERE/gzip/gzcore.c" $ZL
"$OUT/gzenctest" "$OUT/files"
python3 -I - "$OUT/files" <<'EOF'
import sys, zlib
from pathlib import Path
d = Path(sys.argv[1])
wbits = {"gzip": 31, "zlib": 15, "raw": -15}
bad = n = 0
for f in sorted(d.glob("*.L*")):
    name, kind, level = f.name.split(".")
    want = (d / (name + ".in")).read_bytes()
    z = zlib.decompressobj(wbits[kind])
    try:
        got = z.decompress(f.read_bytes()) + z.flush()
        ok = got == want and z.eof and not z.unused_data
    except zlib.error as e:
        ok, got = False, str(e).encode()
    if level == "L0" and ok and len(want) > 1000 and f.stat().st_size < len(want):
        ok = False                     # level 0 stores: never smaller than the input
    if not ok:
        print("FAIL Python's zlib reads", f.name, "differently")
        bad += 1
    n += 1
print(f"Python's zlib inflates {n - bad} of {n} files to their input")
sys.exit(1 if bad else 0)
EOF
