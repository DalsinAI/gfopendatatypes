#!/bin/sh
# The whole of gfopendatatypes for the Amiga, as Cradle's stove builds call
# a module (STOVE= the os32-gcc16 stove, OUT= where it goes): the
# GFOpenDatatypes pack (tools/build_pack.py: every datatype,
# datatypes.library and its system classes, and GFOpenZlib's z.library when
# an amicradle-guest checkout is beside this one or named by
# AMICRADLE_GUEST), its .lha, and the Amiga-side checks in OUT/Tests
# (dtsave).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   STOVE     compiler root holding prefix/ (default ~/AmiCradle/stoves/os32-gcc16)
#   OUT       where it goes (default ./build-amiga)
#   VERSION   the pack's version (default 0.1)
#
# usage: sh build-amiga.sh
#   OUT/pack/GFOpenDatatypes/          the drawer: run Install-GFOpenDatatypes
#   OUT/pack/GFOpenDatatypes-VERSION.lha
#   OUT/Tests/dtsave
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
OUT=${OUT:-"$HERE/build-amiga"}
VERSION=${VERSION:-0.1}
mkdir -p "$OUT"
export STOVE OS32_GCC16="$STOVE"
LHA=
if command -v jlha >/dev/null 2>&1 || command -v lha >/dev/null 2>&1; then LHA=--lha; fi
python3 "$HERE/tools/build_pack.py" --version "$VERSION" --out "$OUT/pack" \
    --library-out "$OUT/library" --zlib-out "$OUT/zlib" $LHA
PREFIX="$OUT/Tests" sh "$HERE/Datatypes/tests/build-dtsave.sh"
