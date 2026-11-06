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
# webp.datatype's encoder alone, when build.sh has built libwebp (work/obj-libwebp):
# webpencamiga and -av with build.sh's objects; -nd and -o1 with libwebp's
# encoder side built again, -fno-tree-loop-distribute-patterns and -O1; -qs
# with build.sh's objects and a qsort of its own (OWN_QSORT)
W="$HERE/work/libwebp/libwebp-1.6.0"
if [ -d "$HERE/work/obj-libwebp" ] && [ -d "$W" ]; then
    B="-m68040 -specs=$HERE/../build/m68k-soft-m68040.specs -fomit-frame-pointer -fno-delete-null-pointer-checks"
    B="$B -DNDEBUG -DWORDS_BIGENDIAN -Wall -Wno-pointer-sign -I$W -I$W/src -I$HERE/webp -DWEBPENC_TRACE"
    ENC="src/enc/vp8l_enc.c src/enc/histogram_enc.c src/enc/backward_references_enc.c
         src/enc/backward_references_cost_enc.c src/enc/predictor_enc.c src/enc/near_lossless_enc.c
         src/dsp/lossless_enc.c src/dsp/lossless.c src/utils/bit_writer_utils.c src/utils/huffman_encode_utils.c
         src/utils/utils.c src/utils/palette.c src/dsp/cpu.c"
    T=$(mktemp -d)
    for v in "" -av -nd -o1 -qs; do
        case "$v" in -nd) F="-O2 -fno-tree-loop-distribute-patterns $B" ;; -o1) F="-O1 $B" ;; *) F="-O2 $B" ;; esac
        rm -rf "$T/o"; mkdir -p "$T/o"
        cp "$HERE"/work/obj-libwebp/*.o "$T/o/"
        if [ "$v" = -nd ] || [ "$v" = -o1 ]; then
            for f in $ENC; do
                (cd "$W" && $CC $F -c "$f" -o "$T/o/$(echo "$f" | tr '/' '_' | sed 's/\.c$//').o")
            done
        fi
        $CC $F -c "$HERE/webp/webpencode.c" -o "$T/webpencode.o"
        X=""
        [ "$v" = -av ] && X=-DAV_MALLOC
        [ "$v" = -qs ] && X=-DOWN_QSORT
        $CC $F $X -c "$HERE/tests/webpencamiga.c" -o "$T/t.o"
        $CC -m68040 -specs="$HERE/../build/m68k-soft-m68040.specs" -o "$OUT/webpencamiga$v" "$T/t.o" \
            "$T/webpencode.o" "$T"/o/*.o
        echo "webpencamiga$v: $(wc -c < "$OUT/webpencamiga$v") bytes"
    done
    rm -rf "$T"
fi
