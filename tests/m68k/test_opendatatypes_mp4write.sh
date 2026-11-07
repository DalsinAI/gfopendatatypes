#!/bin/sh
# GFOpenDatatypes animation.datatype's MP4 writer (mp4_core.c) and its
# H.264 encoder (mp4_h264.c, minih264e with our patches), in
# libraries/opendatatypes/classes/datatypes/animation/:
#   1. the cores built with an m68k cross GCC at -O0 and -O2 for the 68040:
#      nothing called but each other and libgcc's arithmetic, no writable
#      data, no FPU instructions and no 64-bit multiplies or divides (the
#      68060 traps them); with the os32-gcc16 stove there (STOVE=, or
#      OS32_GCC16=), the same counted in its own build with its objdump;
#   2. opendatatypes_mp4write_test.c under qemu-m68k: films of every depth,
#      HAM6, HAM8, EHB, odd sizes, per-frame colours and durations, sound
#      of a frame's own or the film's, mono and stereo, each frame's YUV
#      against the test's own reading, the file's boxes and tables, and
#      the class's DTM_WRITE on a mock object;
#   3. opendatatypes_mp4write_check.py: FFprobe and FFmpeg read every file
#      (H.264 Baseline, every frame, PSNR over the thresholds, the sound
#      byte for byte, FFmpeg's ANIM reader seeing the same pixels), and the
#      encoder built on the host makes the m68k build's NAL units byte for
#      byte. Skipped with a message when FFmpeg isn't installed.
# COUNT=1 also counts the 68k instructions a frame takes (qemu_count.c):
# a 320x256 film's first frame (with the encoder's start) and the frames
# after it, by OIA_Compression level, and the YUV conversion alone.
#   sh tests/m68k/test_opendatatypes_mp4write.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
cls=$here/../../libraries/opendatatypes/classes/datatypes
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
hostcc=${HOST_CC:-cc}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
CORES="animation/animation_core animation/mp4_core animation/mp4_h264"

# In objdump -d's text: 64-bit MULU.L/MULS.L/DIVU.L/DIVS.L (the 68060 traps
# them), then FPU instructions (an F-line word, coprocessor 1, that objdump
# decodes as one: not a jump table's .short)
wide() {
    python3 -I -c '
import re, sys
n = f = 0
for l in sys.stdin:
    m = re.match(r"\s*[0-9a-f]+:\s+((?:[0-9a-f]{4} )+)\s*(\S+)", l)
    if m:
        w = m.group(1).split()
        if m.group(2)[:3] in ("mul", "div") and len(w) > 1 and (int(w[0], 16) & 0xff80) == 0x4c00 and int(w[1], 16) & 0x0400:
            n += 1
        if m.group(2)[0] == "f" and (int(w[0], 16) & 0xfe00) == 0xf200:
            f += 1
print(n, f)'
}

# the cores as the class builds them: calls, data, FPU, wide multiplies
checkobjs() { # label nm objdump size objs...
    label=$1 nm=$2 od=$3 sz=$4 bad=0
    shift 4
    def=$("$nm" -g --defined-only "$@" | awk 'NF == 3 {print $3}' | sort -u)
    und=$("$nm" -u "$@" | awk 'NF && $NF !~ /:$/ {print $NF}' | sort -u |
          grep -v '^_*\(mul\|div\|udiv\|mod\|umod\)[sd]i3$' | grep -vxF "$def" | tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL $label: the cores call $und"; bad=1; }
    for o in "$@"; do
        b=$(basename "$o" .o)
        data=$("$sz" -A "$o" | awk '$1 ~ /^\.(data|bss|init_array|ctors)/ && $2 > 0 {print $1}' | tr '\n' ' ' || true)
        [ -z "$data" ] || { echo "FAIL $label $b: writable data in $data"; bad=1; }
        r=$("$od" -d "$o" | wide)
        [ "${r% *}" = 0 ] || { echo "FAIL $label $b: ${r% *} 64-bit multiplies or divides (the 68060 traps them)"; bad=1; }
        [ "${r#* }" = 0 ] || { echo "FAIL $label $b: ${r#* } FPU instructions"; bad=1; }
    done
    [ "$bad" = 1 ] && fail=1 ||
        echo "ok   $label: $# objects call only each other and libgcc; no data, FPU or 64-bit multiplies"
}

# the class's copies of the DTM_WRITE tags are datatypes/gfopenimage.h's
python3 -I - "$cls/animation/mp4_core.h" "$here/../../Datatypes/include/datatypes/gfopenimage.h" <<'EOF' || fail=1
import re, sys
core, pub = (open(f).read() for f in sys.argv[1:3])
bad = 0
for name in ('Compression', 'WriteFormat', 'Quality', 'WriteSound'):
    a = re.search(r'#define ADT_OIA_%s\s+\(ADT_OIA_Dummy \+ (\d+)\)' % name, core)
    b = re.search(r'#define OIA_%s\s+\(OIA_Dummy \+ (\d+)\)' % name, pub)
    if not a or not b or a.group(1) != b.group(1):
        print('FAIL OIA_%s: mp4_core.h %s, gfopenimage.h %s' % (name, a and a.group(1), b and b.group(1)))
        bad = 1
a = re.search(r'#define ADT_OIWF_MP4\s+(0x[0-9A-F]+)UL', core)
b = re.search(r'#define OIWF_MP4\s+(0x[0-9A-F]+)UL', pub)
if not a or not b or a.group(1) != b.group(1):
    print('FAIL OIWF_MP4 differs')
    bad = 1
if not bad:
    print("ok   the class's OIA_ tags and OIWF_MP4 are gfopenimage.h's")
sys.exit(bad)
EOF

stove=${STOVE:-${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}}
sp=$stove/prefix/bin/m68k-amigaos-
if [ -x "${sp}gcc" ]; then
    objs=
    for c in $CORES; do
        o=$tmp/stove-$(basename "$c").o
        "${sp}gcc" -m68040 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin \
            -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -c "$cls/$c.c" -o "$o"
        objs="$objs $o"
    done
    # shellcheck disable=SC2086
    checkobjs "os32-gcc16 stove -O2" "${sp}nm" "${sp}objdump" "${sp}size" $objs
else
    echo "os32-gcc16 stove: not at $stove (set STOVE=), its build not checked"
fi

if command -v "$hostcc" >/dev/null; then
    "$hostcc" -std=gnu99 -O2 -Wall -Wextra -Werror -I"$cls" "$here/mp4h_host.c" "$cls/animation/mp4_h264.c" -o "$tmp/hostenc"
    host=$tmp/hostenc
else
    echo "no host C compiler ($hostcc): the host encoder's comparison skipped"
    host=
fi

for opt in -O0 -O2; do
    objs=
    for c in $CORES; do
        o=$tmp/$(basename "$c").o
        "$cc" -std=gnu99 $opt -m68040 -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c "$cls/$c.c" -o "$o"
        objs="$objs $o"
    done
    # shellcheck disable=SC2086
    checkobjs "m68k-linux $opt" "${prefix}nm" "${prefix}objdump" "${prefix}size" $objs
    # shellcheck disable=SC2086
    "$cc" -std=gnu99 -O2 -m68040 -static -Wall -Wextra -Werror -Wno-format -I"$cls" \
        "$here/opendatatypes_mp4write_test.c" $objs -o "$tmp/t$opt"
    command -v "$qemu" >/dev/null || { echo "skipped the runs: no $qemu"; continue; }
    rm -rf "$tmp/files"; mkdir "$tmp/files"
    if timeout 600 "$qemu" -cpu m68040 "$tmp/t$opt" "$tmp/files" > "$tmp/out.txt"; then
        echo "ok   $opt under qemu-m68k: $(tail -1 "$tmp/out.txt")"
    else
        echo "FAIL $opt under qemu-m68k:"; grep -m 20 FAIL "$tmp/out.txt" || tail -5 "$tmp/out.txt"; fail=1
        continue
    fi
    if python3 -I "$here/opendatatypes_mp4write_check.py" "$tmp/files" "$host" > "$tmp/check.txt"; then
        echo "ok   $opt, FFmpeg and the host encoder: $(tail -1 "$tmp/check.txt")"
        [ "$opt" = -O0 ] || grep '^     ' "$tmp/check.txt" || true
    else
        echo "FAIL $opt, FFmpeg and the host encoder:"; grep FAIL "$tmp/check.txt" | head -30; fail=1
    fi
done

if [ "${COUNT:-0}" = 1 ] && command -v "$qemu" >/dev/null && command -v "$hostcc" >/dev/null; then
    "$hostcc" -O2 "$here/qemu_count.c" -o "$tmp/count"
    t=$tmp/t-O2
    n() { "$qemu" -cpu m68040 -d in_asm,exec,nochain -D /dev/stdout "$t" -speed "$@" 2>/dev/null | "$tmp/count"; }
    K=${COUNT_FRAMES:-6}
    echo "68k instructions a frame, 320x256 (-O2 -m68040, under qemu-m68k; the film's own making taken off):"
    for kind in box plasma ham8; do
        g1=$(n gen 320 256 1 $kind -1 0) gk=$(n gen 320 256 "$K" $kind -1 0)
        y1=$(n yuv 320 256 1 $kind -1 0) yk=$(n yuv 320 256 "$K" $kind -1 0)
        echo "  $kind: to YUV $(( (yk - gk - y1 + g1) / (K - 1) )) a frame"
        for level in ${COUNT_LEVELS:-0 2 5 9}; do
            w1=$(n write 320 256 1 $kind -1 "$level") wk=$(n write 320 256 "$K" $kind -1 "$level")
            echo "  $kind, OIA_Compression $level: first frame (and the encoder's start) $(( w1 - g1 )), each after $(( (wk - gk - w1 + g1) / (K - 1) ))"
        done
    done
fi
exit $fail
