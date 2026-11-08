#!/bin/sh
# gfopensound.datatype's decoders on this CPU (opensound/snddec.c:
# dr_flac, stb_vorbis, dr_mp3) on x86 or ARM64 cores: tests/make-sound.py
# writes FLAC, Ogg Vorbis and MP3 tones through the system's libsndfile,
# with libsndfile's own decode of each; tests/soundtest.c decodes every one
# in pieces, rewinds, and compares: FLAC exactly, Vorbis and MP3 within a
# few LSB (two decoders of a lossy format round differently). Then each file
# cut short and with bytes changed, under AddressSanitizer and UBSan:
# nothing read or written outside the buffers (built -fwrapv, as the
# datatype is: dr_flac's prediction overflows on damaged data). Last, how much writable
# data the decoders hold (stb_vorbis fills its CRC table once, with the same
# values for every opener).
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   CC   the host compiler (default cc)    SAN  sanitizer flags (SAN= for none)
#   TARBALLS  where SOURCES's files are (default ../tarballs); fetched when missing
#
# usage: sh tests/test-sound.sh [OUTDIR]       (default work/sound-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/sound-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
TARBALLS=${TARBALLS:-"$HERE/../tarballs"}
mkdir -p "$OUT/single"
for f in stb_vorbis.c:4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636 \
         dr_mp3.h:f5c8020eeebb79877184432efdd2651f24869f5bb8f379d45450fd0aed1ee915 \
         dr_flac.h:b8078eeb5d523502eb7964508cd7ec8450bc79d383bec90aa17fd9e2fefd9c24; do
    n=${f%%:*} sha=${f#*:}
    [ -f "$TARBALLS/$n" ] || sh "$HERE/../tools/fetch-tarball.sh" "$n" "$TARBALLS"
    echo "$sha  $TARBALLS/$n" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $n"; exit 2; }
    cp "$TARBALLS/$n" "$OUT/single/$n"
done
(cd "$OUT/single" && patch -p1 -s < "$HERE/patches/stb_vorbis-1.22-comment-list-null.patch")
python3 "$HERE/tests/make-sound.py" "$OUT"
[ -s "$OUT/list" ] || { echo "skipped: no test files (libsndfile)"; exit 0; }
# shellcheck disable=SC2086
$CC -O1 -g $SAN -w -fwrapv -I"$HERE/opensound" -I"$OUT/single" -o "$OUT/soundtest" \
    "$HERE/tests/soundtest.c" "$HERE/opensound/snddec.c" -lm
fail=0
while read -r f ch n rate; do
    case $f in *.flac) tol=0 ;; *) tol=24 ;; esac
    "$OUT/soundtest" "$OUT/$f" "$OUT/$f.s16" "$ch" "$n" "$rate" $tol || fail=1
    "$OUT/soundtest" -damage "$OUT/$f" || fail=1
done < "$OUT/list"
$CC -std=c99 -Os -w -fwrapv -I"$OUT/single" -c "$HERE/opensound/snddec.c" -o "$OUT/snddec.o"
echo "snddec.c: $(size -A "$OUT/snddec.o" | awk '$1 == ".data" || $1 == ".bss" { s += $2 } END { print s + 0 }') bytes of writable data"
[ $fail = 0 ] && echo "all passed" || { echo "FAILED"; exit 1; }
