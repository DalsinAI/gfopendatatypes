#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes classes: builds one family of datatype class libraries
# with the os32-gcc16 stove. text/build-amiga.sh and
# animation/build-amiga.sh call it:
#   build-family.sh <classes dir> <ABI test> <class>...
# e.g. build-family.sh classes/datatypes TextClassABITest text ascii
# Each <class> is a directory with <class>_core.c, open<class>_class.c and
# <class>.lvo. It checks each .lvo against <class>_lib.fd when the NDK has
# one, then builds
#   datatypes/<class>.datatype       the replacement (47.100)
#   datatypes/open<class>.datatype   the same code under its own name; a
#                                    subclass's open build sits on the
#                                    open build of its superclass
#   tests/<ABI test>                 OS 3.2's classes and ours, side by side
# The cores are plain C (as datatypes.library's are); the core calls
# nothing but libgcc, checked with nm.
set -eu
SOFT040="-specs=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/../../../../../build/m68k-soft-m68040.specs"   # the 68040 soft-float link (build/m68k-soft-m68040.specs)

DIR=$1 TEST=$2
shift 2
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
NM=${NM:-"$P/bin/m68k-amigaos-nm"}
OUT=${OUT:-"$DIR/build-amiga"}
mkdir -p "$OUT/datatypes" "$OUT/tests" "$OUT/obj"

COREFLAGS="-m68040 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin
  -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror"
LIBFLAGS="-m68040 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder
  -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror
  -Wno-unused-parameter -Wno-unused-function -nostartfiles -nostdlib"

first=
for NAME in "$@"; do
  [ -n "$first" ] || first=$NAME
  FD=$(find "$P" -name "${NAME}_lib.fd" 2>/dev/null | head -1)
  if [ -n "$FD" ]; then python3 "$DIR/../../../common/check_fd.py" "$FD" "$DIR/$NAME/$NAME.lvo"
  else echo "$NAME.lvo: no ${NAME}_lib.fd in the NDK (-30 is the class call)"; fi
  # shellcheck disable=SC2086
  "$CC" $COREFLAGS -c "$DIR/$NAME/${NAME}_core.c" -o "$OUT/obj/${NAME}_core.o"
  und=$("$NM" -u "$OUT/obj/${NAME}_core.o" | awk '{print $NF}' |
        grep -v '^_*\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' || true)
  [ -z "$und" ] || { echo "the $NAME core calls $und" >&2; exit 1; }
  for file in "$NAME.datatype" "open$NAME.datatype"; do
    side=
    [ "$file" = "$NAME.datatype" ] || side=-DDTC_SIDE_BY_SIDE
    # shellcheck disable=SC2086
    "$CC" $LIBFLAGS $side -DLIBNAME="\"$file\"" -I"$DIR/$NAME" -o "$OUT/datatypes/$file" \
      "$DIR/$NAME/open${NAME}_class.c" "$OUT/obj/${NAME}_core.o" -lgcc
    echo "$OUT/datatypes/$file ($(wc -c < "$OUT/datatypes/$file") bytes)"
    und=$("$NM" -u "$OUT/datatypes/$file" || true)
    [ -z "$und" ] || { echo "undefined in $file: $und" >&2; exit 1; }
  done
done

"$CC" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format -noixemul $SOFT040 \
  -I"$DIR" -o "$OUT/tests/$TEST" "$DIR/$first/tests/$TEST.c"
echo "$OUT/tests/$TEST ($(wc -c < "$OUT/tests/$TEST") bytes)"
sha256sum "$OUT"/datatypes/* "$OUT/tests/$TEST"
