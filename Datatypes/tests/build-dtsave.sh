#!/bin/sh
# dtsave (tests/dtsave.c), the Amiga-side check of a picture datatype's raw
# write, with the os32-gcc16 compiler as the datatypes are built.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   PREFIX       where it goes (default ../out/Tests)
#
# usage: sh tests/build-dtsave.sh
# Then on the Amiga, with the datatypes installed:
#   dtsave Pictures/x.tga RAM:x.tga             the same class saves it
#   dtsave Pictures/x.png RAM:x.tga AS gfopentga  converted through an empty gfopentga object
#   dtsave Films/x.cdxl RAM:x.cdxl SAMEBYTES    the film written out again, byte for byte
#   dtsave Films/x.webm RAM:x.anim IFF          any film saved as an IFF ANIM; then
#   dtanim Films/x.webm and dtanim RAM:x.anim   the same frame sums, frame by frame
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${PREFIX:-"$HERE/out/Tests"}
mkdir -p "$OUT"
$CC -O2 -m68040 -specs="$HERE/../build/m68k-soft-m68040.specs" -fomit-frame-pointer -fno-delete-null-pointer-checks \
    -Wall -Wno-pointer-sign -noixemul -I"$HERE/include" -o "$OUT/dtsave" "$HERE/tests/dtsave.c" -lamiga
echo "dtsave: $(wc -c < "$OUT/dtsave") bytes"
$CC -O2 -m68040 -specs="$HERE/../build/m68k-soft-m68040.specs" -fomit-frame-pointer -fno-delete-null-pointer-checks \
    -Wall -Wno-pointer-sign -noixemul -I"$HERE/include" -o "$OUT/dtanim" "$HERE/tests/dtanim.c" -lamiga
echo "dtanim: $(wc -c < "$OUT/dtanim") bytes"
# webp.datatype's encoder alone, when build.sh has built libwebp (work/obj-libwebp)
W="$HERE/work/libwebp/libwebp-1.6.0"
if [ -d "$HERE/work/obj-libwebp" ] && [ -d "$W" ]; then
    F="-O2 -m68040 -specs=$HERE/../build/m68k-soft-m68040.specs -fomit-frame-pointer -fno-delete-null-pointer-checks"
    F="$F -DNDEBUG -DWORDS_BIGENDIAN -Wall -Wno-pointer-sign -I$W -I$W/src -I$HERE/webp"
    T=$(mktemp -d)
    $CC $F -c "$HERE/webp/webpencode.c" -o "$T/webpencode.o"
    for v in "" -av; do
        $CC $F $( [ -n "$v" ] && echo -DAV_MALLOC ) -c "$HERE/tests/webpencamiga.c" -o "$T/t.o"
        $CC -m68040 -specs="$HERE/../build/m68k-soft-m68040.specs" -o "$OUT/webpencamiga$v" "$T/t.o" \
            "$T/webpencode.o" "$HERE"/work/obj-libwebp/*.o
        echo "webpencamiga$v: $(wc -c < "$OUT/webpencamiga$v") bytes"
    done
    rm -rf "$T"
fi
