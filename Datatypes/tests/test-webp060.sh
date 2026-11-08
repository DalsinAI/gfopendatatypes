#!/bin/sh
# patches/libwebp-1.6.0-m68060.patch: the lossless encoder's busiest sums
# without the 64-bit multiplies and divides a 68060 traps, giving the same
# results. On x86 or ARM64 cores, under AddressSanitizer and UBSan:
# tests/webp060test.c checks the patch's helpers against 64-bit arithmetic,
# PredictionCostBias, FastSLog2Slow (every value up to 2^16),
# VP8LBitsEntropy and the entropy gathering against libwebp 1.6.0's own, and the encoder with the
# patch must write the same files as without it (99 encodes, every level).
# With an m68k cross GCC and qemu-m68k, as 68040 code: the same checks and
# encodes (without the largest pictures; the files must be the host's too), then one encode's instructions
# counted (tests/qemu060count.c): the patch must not run more instructions
# than libwebp without it, and must leave under a tenth of the trapping
# ones.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC        the host compiler (default cc)
#   SAN       sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   M68K_CC, QEMU_M68K  the cross tools (default m68k-linux-gnu-gcc, qemu-m68k);
#             QEMU_M68K= skips the m68k half
#   TARBALLS  as for test-webpenc.sh
#
# usage: sh tests/test-webp060.sh [OUTDIR]   (default work/webp060-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/webp060-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
m68kcc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K-qemu-m68k}
if [ -z "${TARBALLS:-}" ]; then
    if [ -d "$HERE/tarballs" ]; then TARBALLS="$HERE/tarballs"; else TARBALLS="$HERE/../tarballs"; fi
fi
T="$TARBALLS/libwebp-1.6.0.tar.gz"
mkdir -p "$TARBALLS"
[ -f "$T" ] || sh "$HERE/../tools/fetch-tarball.sh" libwebp-1.6.0.tar.gz "$TARBALLS"
echo "e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564  $T" | sha256sum -c - >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT"
fail=0
say() { echo "$1"; case "$1" in ok*) ;; *) fail=1 ;; esac; }

# libwebp as build.sh patches it ("after") and without the 68060 patch ("before")
for v in before after; do
    mkdir -p "$OUT/$v"
    tar -xzf "$T" -C "$OUT/$v"
    ps="no-float demux-null-chunk encoder-no-float"
    [ $v = after ] && ps="$ps m68060"
    (cd "$OUT/$v/libwebp-1.6.0" && for p in $ps; do patch -p1 -s < "$HERE/patches/libwebp-1.6.0-$p.patch"; done)
done
# libwebp's decoder and lossless encoder, as test-webpenc.sh builds them
# (the SSE, NEON and MIPS files compile to nothing on 68k), into an
# archive so only what is used is linked
FILES="src/dec/*.c src/demux/demux.c src/dsp/*.c src/utils/*.c src/enc/vp8l_enc.c src/enc/histogram_enc.c
    src/enc/backward_references_enc.c src/enc/backward_references_cost_enc.c src/enc/near_lossless_enc.c"
# build NAME COMPILER FLAGS...: OUT/NAME/{before,after}/webp060test; predictor_enc.c
# is built into the test itself, so it can call PredictionCostBias
build() {
    name=$1 c=$2; shift 2
    for v in before after; do
        W="$OUT/$v/libwebp-1.6.0" o="$OUT/$name/$v"
        mkdir -p "$o/obj"
        # shellcheck disable=SC2086
        for f in $(cd "$W" && ls $FILES); do
            (cd "$W" && $c "$@" -I. -Isrc -c "$f" -o "$o/obj/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')")
        done
        rm -f "$o/libwebp.a"
        ar rcs "$o/libwebp.a" "$o"/obj/*.o
        d=; [ $v = after ] && d=-DWEBP060_CHECK
        $c "$@" $d -I"$HERE/tests/host" -I"$HERE/webp" -I"$W" -o "$o/webp060test" \
            "$HERE/tests/webp060test.c" "$HERE/webp/webpencode.c" "$o/libwebp.a" -lm
    done
}

# shellcheck disable=SC2086
build host "$CC" -O1 -g $SAN
"$OUT/host/after/webp060test" check | sed 's/^/     /'
"$OUT/host/after/webp060test" check > /dev/null 2>&1 && say "ok   the patch's sums are libwebp's (host)" || say "FAIL the patch's sums (host)"
for v in before after; do mkdir -p "$OUT/host/$v/files"; "$OUT/host/$v/webp060test" encode "$OUT/host/$v/files"; done
n=$(ls "$OUT/host/after/files" | wc -l)
if [ "$n" -eq 99 ] && diff -r "$OUT/host/before/files" "$OUT/host/after/files" >/dev/null; then
    say "ok   the encoder writes the same $n files with the patch as without (host)"
else say "FAIL the encoder's files differ with the patch (host, $n files)"; fi

if [ -z "$qemu" ] || ! command -v "$qemu" >/dev/null || ! command -v "$m68kcc" >/dev/null; then
    echo "m68k half skipped (no $m68kcc or qemu-m68k)"
    exit $fail
fi
build m68k "$m68kcc" -O2 -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -DNDEBUG -DWORDS_BIGENDIAN -static
"$qemu" -cpu m68040 "$OUT/m68k/after/webp060test" check | sed 's/^/     /'
"$qemu" -cpu m68040 "$OUT/m68k/after/webp060test" check > /dev/null 2>&1 && say "ok   the patch's sums are libwebp's (68040)" \
    || say "FAIL the patch's sums (68040)"
for v in before after; do
    mkdir -p "$OUT/m68k/$v/files"; "$qemu" -cpu m68040 "$OUT/m68k/$v/webp060test" encode "$OUT/m68k/$v/files" small
done
n=$(ls "$OUT/m68k/after/files" | wc -l)
same=0
for f in "$OUT"/m68k/after/files/*; do cmp -s "$f" "$OUT/host/after/files/${f##*/}" && same=$((same + 1)); done
if [ "$n" -eq 66 ] && [ "$same" -eq 66 ] && diff -r "$OUT/m68k/before/files" "$OUT/m68k/after/files" >/dev/null; then
    say "ok   the encoder writes the same $n files with the patch as without, and as on the host (68040)"
else say "FAIL the encoder's files differ with the patch (68040, $n files)"; fi

# instructions, and the ones a 68060 traps, for one middling encode
cc -O2 -o "$OUT/qemu060count" "$HERE/tests/qemu060count.c"
count() {        # BUILD: "instructions trapping"
    mkfifo "$OUT/log.$1"
    "$OUT/qemu060count" < "$OUT/log.$1" > "$OUT/count.$1" &
    "$qemu" -cpu m68040 -d in_asm,exec,nochain -D "$OUT/log.$1" "$OUT/m68k/$1/webp060test" bench photo 96 64 -1 > /dev/null
    wait
    rm -f "$OUT/log.$1"
    cat "$OUT/count.$1"
}
set -- $(count before) $(count after)
echo "     photo 96x64, level -1: $1 instructions, $2 trapped on a 68060 without the patch; $3 and $4 with it"
if [ "$3" -le "$1" ] && [ $(($4 * 10)) -lt "$2" ]; then
    say "ok   the patch runs no more instructions on a 68040 and leaves $4 of $2 trapping ones"
else say "FAIL the patch: $3 instructions (was $1), $4 trapping (was $2)"; fi
exit $fail
