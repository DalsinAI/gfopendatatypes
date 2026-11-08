#!/bin/sh
# webp.datatype's encoder (webp/webpencode.c) on x86 or ARM64 cores, under
# AddressSanitizer and UBSan, with libwebp 1.6.0 patched as for the Amiga:
# tests/webpenctest.c encodes the test pictures lossless at three levels and
# reads each back with libwebp's own decoder, pixel for pixel; Pillow, an
# outside reader, must see the same picture in every file.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC        the host compiler (default cc)
#   SAN       sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   TARBALLS  folder holding libwebp-1.6.0.tar.gz (default ./tarballs, else ../tarballs);
#             fetched from SOURCES's URL, SHA-256 checked, when it is not there
#
# usage: sh tests/test-webpenc.sh [OUTDIR]   (default work/webpenc-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webpenc-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
T="$TARBALLS/libwebp-1.6.0.tar.gz"
mkdir -p "$TARBALLS"
[ -f "$T" ] || sh "$HERE/../tools/fetch-tarball.sh" libwebp-1.6.0.tar.gz "$TARBALLS"
echo "e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564  $T" | sha256sum -c - >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/files"
tar -xzf "$T" -C "$OUT"
W="$OUT/libwebp-1.6.0"
(cd "$W" && for p in no-float demux-null-chunk encoder-no-float m68060; do
    patch -p1 -s < "$HERE/patches/libwebp-1.6.0-$p.patch"; done)
# libwebp's decoder and the lossless encoder's files, as an archive so only
# what is used is linked; picture_enc.c, picture_csp_enc.c and
# picture_rescale_enc.c stay out, as on the Amiga (webp/webpencode.c)
for f in "$W"/src/dec/*.c "$W"/src/demux/demux.c "$W"/src/dsp/*.c "$W"/src/utils/*.c \
         "$W"/src/enc/vp8l_enc.c "$W"/src/enc/histogram_enc.c "$W"/src/enc/backward_references_enc.c \
         "$W"/src/enc/backward_references_cost_enc.c "$W"/src/enc/predictor_enc.c \
         "$W"/src/enc/near_lossless_enc.c; do
    # shellcheck disable=SC2086
    $CC -O1 -g $SAN -I"$W" -I"$W/src" -c "$f" -o "$OUT/obj/$(echo "${f#"$W"/}" | tr / _ | sed 's/\.c$/.o/')"
done
ar rcs "$OUT/libwebp.a" "$OUT"/obj/*.o
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" \
    -o "$OUT/webpenctest" "$HERE/tests/webpenctest.c" "$HERE/webp/webpencode.c" "$OUT/libwebp.a" -lm
"$OUT/webpenctest" "$OUT/files"
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
files = sorted(d.glob("*.webp"))
for f in files:
    with Image.open(f) as im:
        got = im.convert("RGBA").tobytes()
    if got != (d / (f.stem + ".rgba")).read_bytes():
        print("FAIL Pillow reads", f.name, "differently")
        bad += 1
print(f"Pillow reads {len(files) - bad} of {len(files)} files as written")
sys.exit(1 if bad else 0)
EOF
