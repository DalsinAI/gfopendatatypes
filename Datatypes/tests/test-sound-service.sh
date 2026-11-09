#!/bin/sh
# gfopensound.datatype's decoding on the host (47.3: through mitchy.library
# to media.decode/1, on the services card): tests/soundservice.py sends
# tests/make-sound.py's FLAC, Ogg Vorbis and MP3 tones to gfopenservice's
# host/libmediadecode.so with the requests the datatype makes, loading
# (PROBE keeping the file, one DECODE) and reading (DECODE in pieces from
# the frame reached), and compares with libsndfile's decodes.
# MIT, Copyright (c) 2026 AmiCradle Team.
# SPDX-License-Identifier: MIT
#
#   SERVICE  the gfopenservice checkout (default: beside this repository),
#            its host/libmediadecode.so built with FFmpeg (its build.sh)
#
# usage: sh tests/test-sound-service.sh [OUTDIR]       (default work/sound-service-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/sound-service-test"}
SERVICE=${SERVICE:-"$HERE/../../gfopenservice"}
[ -f "$SERVICE/host/libmediadecode.so" ] || { echo "skipped: no $SERVICE/host/libmediadecode.so (set SERVICE)"; exit 0; }
python3 "$HERE/tests/make-sound.py" "$OUT"
[ -s "$OUT/list" ] || { echo "skipped: no test files (libsndfile)"; exit 0; }
python3 "$HERE/tests/soundservice.py" "$SERVICE/host" "$OUT"
