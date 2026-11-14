#!/bin/sh
# Public message ABI and portable core with the agreed os32-gcc16 stove.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/acmedia-amiga"}
S=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC="$S/prefix/bin/m68k-amigaos-gcc"
[ -x "$CC" ] || { echo "acmedia Amiga ABI: skip (no $CC)"; exit 0; }
mkdir -p "$OUT"
for opt in 0 2; do
    "$CC" -m68020 -noixemul -O"$opt" -fno-delete-null-pointer-checks \
        -Wall -Wextra -Werror -I"$HERE/include" -I"$HERE/acmedia" \
        "$HERE/tests/acmediaabi.c" "$HERE/acmedia/amcore.c" \
        -o "$OUT/acmediaabi-O$opt"
    file "$OUT/acmediaabi-O$opt"
done
echo "acmedia Amiga ABI: O0/O2 sizes and loadseg links pass"
