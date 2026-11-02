#!/bin/sh
# gfopenpng.datatype's encoder (png/pngencode.c) on x86 or ARM64 cores: encodes
# the pictures tests/pngenctest.c makes (opaque and with alpha, premultiplied,
# noise, flat, sizes from 1x1 to 1023 wide and 700 high, padded rows, levels
# 0, 1, 6 and 9) through z.library's deflate, reads each back with gfopenpng's
# own decoder there and then with Python: its zlib module and a reading of
# the chunks written here from the PNG specification (every CRC, IHDR, the
# filters), and Pillow too when it is installed. Every pixel must come back.
# Under AddressSanitizer and UBSan; then the encoder as C99 -Wall -Wextra
# -Werror -pedantic at -Os, with no writable data.
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   CC, CXX, SAN, AMIGACHROME_GUEST   as tests/test-stream.sh
#
# usage: sh tests/test-pngenc.sh [OUTDIR]       (default work/pngenc-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/pngenc-test"}
CC=${CC:-cc}
CXX=${CXX:-c++}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"

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
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/common" -I"$HERE/png" \
    -o "$OUT/pngenctest" "$HERE/tests/pngenctest.c" "$HERE/png/pngencode.c" "$HERE/png/pngdecode.c" \
    "$HERE/tests/zlhost.c" $ZL
"$OUT/pngenctest" "$OUT" > "$OUT/list.txt" || { cat "$OUT/list.txt"; echo "pngenctest: failed"; exit 1; }
echo "pngenctest: $(wc -l < "$OUT/list.txt") pictures encoded, and read back the same by gfopenpng's decoder"

python3 - "$OUT" <<'PY'
import os, struct, sys, zlib
out = sys.argv[1]
try:
    from PIL import Image
except ImportError:
    Image = None

def read(png):
    """PNG 5 to 9, for colour types 2 and 6 at 8 bits: the pixels as RGBA."""
    assert png[:8] == b"\x89PNG\r\n\x1a\n", "signature"
    pos, idat, ihdr, seen = 8, b"", None, []
    while pos < len(png):
        n, t = struct.unpack(">I4s", png[pos:pos + 8])
        data = png[pos + 8:pos + 8 + n]
        crc, = struct.unpack(">I", png[pos + 8 + n:pos + 12 + n])
        assert crc == zlib.crc32(t + data), f"CRC of {t}"
        seen.append(t)
        if t == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", data)
        elif t == b"IDAT":
            idat += data
        pos += 12 + n
    assert seen[0] == b"IHDR" and seen[-1] == b"IEND" and pos == len(png), "chunk order"
    w, h, depth, ctype, comp, filt, inter = ihdr
    assert depth == 8 and ctype in (2, 6) and comp == filt == inter == 0, "IHDR"
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    assert len(raw) == h * (w * bpp + 1), "inflated size"
    rows, prev = [], bytearray(w * bpp)
    for y in range(h):
        f = raw[y * (w * bpp + 1)]
        line = bytearray(raw[y * (w * bpp + 1) + 1:(y + 1) * (w * bpp + 1)])
        for i in range(len(line)):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            else: assert f == 0, "filter type"
        rows.append(bytes(line))
        prev = line
    px = b"".join(rows)
    if bpp == 3:
        px = b"".join(px[i:i + 3] + b"\xff" for i in range(0, len(px), 3))
    return w, h, ctype, px

n = pil = 0
for line in open(os.path.join(out, "list.txt")):
    name, w, h, alpha, size = line.split()
    png = open(os.path.join(out, name + ".png"), "rb").read()
    want = open(os.path.join(out, name + ".rgba"), "rb").read()
    rw, rh, ctype, px = read(png)
    assert (rw, rh) == (int(w), int(h)) and (ctype == 6) == (alpha == "1"), name
    assert px == want, f"{name}: pixels differ"
    if Image:
        im = Image.open(os.path.join(out, name + ".png"))
        im.load()
        assert im.convert("RGBA").tobytes() == want, f"{name}: Pillow reads other pixels"
        pil += 1
    n += 1
print(f"python: {n} files read back pixel for pixel (zlib and the chunks, every CRC)"
      + (f"; Pillow reads all {pil} the same" if Image else "; Pillow not installed"))
PY

o="$OUT/pngencode.o"
$CC -std=c99 -Os -Wall -Wextra -Werror -pedantic -I"$HERE/tests/host" -I"$HERE/common" -I"$HERE/png" \
    -c "$HERE/png/pngencode.c" -o "$o"
data=$(size -A "$o" | awk '$1 ~ /^\.(data|bss)/ && $2 > 0 {print $1}' | tr '\n' ' ')
[ -z "$data" ] || { echo "FAIL pngencode.c: writable data ($data)"; exit 1; }
echo "pngencode.c: C99 -Wall -Wextra -Werror -pedantic, no writable data"
