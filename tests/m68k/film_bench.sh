#!/bin/sh
# The film path's 68k instruction counts (tests/m68k/film_bench.c): the
# animation and anim cores at BASE (a git ref, default origin/main) and as
# they are in this tree, built with m68k-linux-gnu-gcc -O2 -m68040 and run
# under qemu-m68k with one instruction a translation block, every executed
# instruction logged and counted. A workload's count is the run with one
# repetition less the run with none, so set-up and start-up cancel out.
# The output checksums of the two builds must be the same.
#   sh tests/m68k/film_bench.sh [BASE] [WORKLOAD...]
# Not a CI job (a few minutes): the numbers go in the PR.
# Copyright (c) 2026 AmiCradle Team. MIT.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
base=${1:-origin/main}
[ $# -gt 0 ] && shift
works=${*:-remap8 remap5 remapid remap4of8 copyout}
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/base"
git -C "$top" archive "$base" libraries/opendatatypes | tar -x -C "$tmp/base"
build() {   # build TREE OUT
    cls=$1/libraries/opendatatypes/classes/datatypes
    "$cc" -std=gnu99 -O2 -m68040 -static -fno-builtin -fno-tree-loop-distribute-patterns \
        -fno-delete-null-pointer-checks -w -I"$cls" "$here/film_bench.c" \
        "$cls/animation/animation_core.c" "$cls/anim/anim_core.c" -o "$2"
}
build "$tmp/base" "$tmp/before"
build "$top" "$tmp/after"
count() {   # count BIN WORKLOAD REPS: instructions executed
    "$qemu" -cpu m68040 -one-insn-per-tb -d exec,nochain -D "$tmp/log" "$1" "$2" "$3" > "$tmp/out.$3"
    grep -c '^Trace' "$tmp/log"
}
for w in $works; do
    for b in before after; do
        n0=$(count "$tmp/$b" "$w" 0)
        n1=$(count "$tmp/$b" "$w" 1)
        eval "n_$b=$((n1 - n0))"
        eval "sum_$b=\"\$(cat \"$tmp/out.1\")\""
    done
    # shellcheck disable=SC2154
    same=same; [ "$sum_before" = "$sum_after" ] || same="DIFFERENT ($sum_before / $sum_after)"
    # shellcheck disable=SC2154
    echo "$w: $n_before -> $n_after instructions, output $same"
done
