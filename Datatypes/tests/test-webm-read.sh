#!/bin/sh
# webm.datatype's reader (webm/webm_demux.c) through its read cache
# (webm/webm_readcache.h), on the host under AddressSanitizer and UBSan
# (tests/webmreadtest.c, no libvpx needed): tests/t.webm and, with ffmpeg,
# a VP8 clip with Opus sound and a VP9 clip without, each read straight and
# through the cache, whole, cut short and with bytes changed, must give the
# same frames; the cache must call the source about once for each 4 KB;
# and the audio track count must be right.
# Then webm.datatype's sound (tests/webmsoundtest.c): webm/webmclass.c
# itself, with the Amiga's names from tests/webmclass-host.h (each Amiga
# header the class includes is made here, pointing at it) and a fake
# media.decode/1 that keeps a record: the file goes up in parts of at most
# 256 KB (VUPLOAD), once, and each frame's sound is asked for as it loads
# (VSOUND, scaled when the service counts at another rate); without the
# service or VUPLOAD the clip plays silent. With a service that has VINDEX
# (47.2), the frames come from it too (VFRAME in MD_PIX_WEBM) with the next
# one asked for while this one shows, through one connection an object;
# an older service, one that counts other frames, or one that fails while
# playing leaves the frames to libvpx.
# MIT, Copyright (c) 2026 AmiCradle Team.
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

INC="$OUT/host-class"
for h in exec/types.h exec/memory.h exec/semaphores.h exec/ports.h exec/tasks.h exec/io.h dos/dos.h \
         graphics/gfx.h graphics/rastport.h graphics/view.h intuition/classes.h intuition/classusr.h \
         datatypes/datatypes.h datatypes/datatypesclass.h datatypes/pictureclass.h datatypes/animationclass.h \
         proto/exec.h proto/dos.h proto/graphics.h proto/intuition.h proto/datatypes.h clib/alib_protos.h \
         vpx/vpx_decoder.h vpx/vp8dx.h vpx/vpx_image.h; do
    mkdir -p "$INC/$(dirname "$h")"
    echo '#include "webmclass-host.h"' > "$INC/$h"
done
# shellcheck disable=SC2086
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $SAN -I"$INC" -I"$HERE/tests" -I"$HERE/common" -I"$HERE/include" \
    -I"$HERE/webm" -o "$OUT/webmsoundtest" "$HERE/tests/webmsoundtest.c" "$HERE/webm/webm_demux.c"
"$OUT/webmsoundtest" "$@"
