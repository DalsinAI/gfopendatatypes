#!/bin/sh
# webm.datatype's reader (webm/webm_demux.c) through its read cache
# (webm/webm_readcache.h), on the host under AddressSanitizer and UBSan
# (tests/webmreadtest.c, no libvpx needed): tests/t.webm and, with ffmpeg,
# a VP8 clip with Opus sound and a VP9 clip without, each read straight and
# through the cache, whole, cut short and with bytes changed, must give the
# same frames; the cache must call the source about once for each 4 KB;
# and the audio track count must be right.
# MIT, Copyright (c) 2026 AmigaChrome Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-webm-read.sh [OUTDIR]       (default work/webm-read-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webm-read-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/webm" \
    -o "$OUT/webmreadtest" "$HERE/tests/webmreadtest.c" "$HERE/webm/webm_demux.c"
set -- -audio 0 "$HERE/tests/t.webm"
if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -v error -y -f lavfi -i testsrc=size=320x240:rate=25:duration=12 -f lavfi -i sine=frequency=440:duration=12 \
        -c:v libvpx -b:v 300k -c:a libopus -shortest "$OUT/sound.webm"
    ffmpeg -v error -y -f lavfi -i testsrc=size=160x120:rate=25:duration=6 -c:v libvpx-vp9 -b:v 100k "$OUT/vp9.webm"
    set -- "$@" -audio 1 "$OUT/sound.webm" -audio 0 "$OUT/vp9.webm"
else
    echo "no ffmpeg: tests/t.webm only"
fi
"$OUT/webmreadtest" "$@"
