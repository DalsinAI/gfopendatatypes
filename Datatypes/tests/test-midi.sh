#!/bin/sh
# gfopenmidi.datatype's portable part on x86 or ARM64 cores (tests/miditest.c):
# what counts as a MIDI file (format, tracks, a header or a track cut short, a
# .rmi wrapper) and a file's length in frames, under AddressSanitizer and UBSan.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-midi.sh [OUTDIR]       (default work/midi-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/midi-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/openmidi" \
    -o "$OUT/miditest" "$HERE/tests/miditest.c" "$HERE/openmidi/midifile.c"
"$OUT/miditest"
