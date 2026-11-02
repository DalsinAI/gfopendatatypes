#!/bin/sh
# GFOpenDatatypes' picture and sound families: builds one datatype class
# library with the os32-gcc16 stove. Each class's build-amiga.sh calls it:
#   build-class.sh <dir> <core> <file name> <ABI test>
# e.g. build-class.sh .../ilbm ilbm ilbm.datatype ILBMDTClassABITest
# It checks ndk_check.c's numbers and layouts against the NDK 3.2 headers,
# then builds
#   <file name>        the replacement (47.100), for LIBS:datatypes
#   open<file name>    the same code under its own name (and over open
#                      superclasses), for the ABI test
#   <ABI test>         runs the same set through both and lists every difference
# The core is lean C++ (DalsinAI/gfopengcc docs/lean-cxx.md, the
# library profile); the shell, open<name>_dtclass.c, is C. The class
# libraries have no fd in the NDK: <name>.lvo documents their one call.
set -eu
SOFT040="-specs=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/../../../../../build/m68k-soft-m68040.specs"   # the 68040 soft-float link (build/m68k-soft-m68040.specs)

HERE=$1 CORE=$2 FILE=$3 TEST=$4
FAMILY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
CXX=${CXX:-"$P/bin/m68k-amigaos-g++"}
NM=${NM:-"$P/bin/m68k-amigaos-nm"}
OUT=${OUT:-"$HERE/build-amiga"}
mkdir -p "$OUT/datatypes" "$OUT/tests" "$OUT/obj"

"$CC" -m68040 -fsyntax-only -Wall -Wextra -Werror -I"$FAMILY" "$FAMILY/ndk_check.c"

CXXFLAGS="-m68040 -O2 -std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib++
  -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin
  -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror"
SRC=$(ls "$HERE"/*_core.cpp)
# shellcheck disable=SC2086
"$CXX" $CXXFLAGS -c "$SRC" -o "$OUT/obj/${CORE}_core.o"
bad=$("$NM" "$OUT/obj/${CORE}_core.o" | awk '$1 == "U" {print $2} NF == 3 {print $3}' |
      grep -E '^_?(__cxa_|__gxx_personality|_ZTI|_ZTS|_ZTV|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_ZdlPv|_ZdaPv)' || true)
[ -z "$bad" ] || { echo "forbidden C++ symbols in the core: $bad" >&2; exit 1; }
und=$("$NM" -u "$OUT/obj/${CORE}_core.o" | awk '{print $NF}' |
      grep -v '^_*\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' || true)
[ -z "$und" ] || { echo "the core calls $und" >&2; exit 1; }

LIBFLAGS="-m68040 -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder
  -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror
  -Wno-unused-parameter -Wno-unused-function -nostartfiles -nostdlib"
SHELLSRC=$(ls "$HERE"/open*_dtclass.c)
for name in "$FILE" "open$FILE"; do
  side=
  [ "$name" = "$FILE" ] || side=-DDTC_SIDE_BY_SIDE
  # shellcheck disable=SC2086
  "$CC" $LIBFLAGS $side -DLIBNAME="\"$name\"" -I"$HERE" -I"$FAMILY" -o "$OUT/datatypes/$name" \
    "$SHELLSRC" "$OUT/obj/${CORE}_core.o" -lgcc
  echo "$OUT/datatypes/$name ($(wc -c < "$OUT/datatypes/$name") bytes)"
  und=$("$NM" -u "$OUT/datatypes/$name" || true)
  [ -z "$und" ] || { echo "undefined in $name: $und" >&2; exit 1; }
done

"$CC" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul $SOFT040 \
  -o "$OUT/tests/$TEST" "$HERE/tests/$TEST.c"
echo "$OUT/tests/$TEST ($(wc -c < "$OUT/tests/$TEST") bytes)"
sha256sum "$OUT"/datatypes/* "$OUT/tests/$TEST" 2>/dev/null || true
