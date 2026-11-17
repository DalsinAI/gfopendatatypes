#!/bin/sh
# gfopenmidi.datatype and its descriptors, DEVS:DataTypes/MIDI and RMID, for
# AmigaOS 3.x (midiclass.c). Our own code only: the music is played by
# gfopensynth.library (GFOpenSynth), opened from LIBS: when a MIDI file is.
# Built with the os32-gcc16 compiler, as the other datatypes (../build.sh
# runs this too).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   OS32_GCC16   compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   ACFLAGS, ACLIB  AC090's native helpers for common/dtlib.c, as ../build.sh
#                passes them (AC_HELPERS=1 there); empty: none
#   PREFIX       where Classes/ and Devs/ go (default ../out)
#
# usage: sh openmidi/build.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
OUT=${PREFIX:-"$HERE/out"}
WORK="$HERE/work/obj-openmidi"
# Address 0 is memory on an Amiga: keep GCC from dropping null checks (../build.sh).
# Soft float (../build/m68k-soft-m68040.specs); nothing here uses floating point.
SOFT="-specs=$HERE/../build/m68k-soft-m68040.specs"
CFLAGS="-O2 -m68040 $SOFT -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG -Wall -Wno-pointer-sign"
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/Classes/DataTypes" "$OUT/Devs/DataTypes"
for f in common/dtstart.c common/dtlib.c openmidi/midifile.c openmidi/midiclass.c; do
    (cd "$HERE" && $CC $CFLAGS -I"$HERE/common" -I"$HERE/include" -I"$HERE/openmidi" ${ACFLAGS:-} -c "$f" -o "$WORK/$(basename "$f" .c).o")
done
# dtstart.o first, so the library's first code is a safe return.
$CC -nostartfiles -m68040 $SOFT -o "$OUT/Classes/DataTypes/gfopenmidi.datatype" \
    "$WORK/dtstart.o" "$WORK/dtlib.o" "$WORK/midifile.o" "$WORK/midiclass.o" ${ACLIB:-} -lamiga \
    -Wl,-Map="$WORK/gfopenmidi.datatype.map"
echo "gfopenmidi.datatype: $(wc -c < "$OUT/Classes/DataTypes/gfopenmidi.datatype") bytes"
# Its descriptors: descriptors.sh.
. "$HERE/descriptors.sh"
desc_gfopenmidi "$OUT"
