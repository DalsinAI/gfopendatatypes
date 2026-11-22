#!/bin/sh
# smacker.datatype's core (smacker/smk_core.c) on the host: makes the test
# films (tests/make-smacker.py), decodes each through tests/smktest.c and
# FFmpeg's Smacker decoder, and compares every frame's pixels and the first
# sound track's samples, byte for byte. Then the films cut short and with
# bytes changed, under AddressSanitizer and UBSan: nothing is read or
# written outside the film or the picture. With QEMU_M68K set (qemu-m68k),
# the core is built for the 68040 with m68k-linux-gnu-gcc and must give the
# same answers there. With SMACKER_FILMS set to a folder, every .smk in it
# (a game's films) is compared with FFmpeg too.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC             the host compiler (default cc)
#   SAN            sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   QEMU_M68K      qemu-m68k, to run the core as 68k code too
#   SMACKER_FILMS  a folder of .smk films to check as well
#
# usage: sh tests/test-smacker.sh [OUTDIR]       (default work/smacker-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/smacker-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
command -v ffmpeg >/dev/null 2>&1 || { echo "test-smacker.sh needs ffmpeg, the decoder it compares with" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
python3 "$HERE/tests/make-smacker.py" "$OUT/films" > /dev/null
# shellcheck disable=SC2086
$CC -std=c99 -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/smacker" -o "$OUT/smktest" \
    "$HERE/tests/smktest.c" "$HERE/smacker/smk_core.c"

# FILM NAME: ours and FFmpeg's, the same pictures and sound
check() {
    rm -f "$OUT/$2.s16" "$OUT/$2.ffmpeg.s16"
    "$OUT/smktest" "$1" "$OUT/$2.rgb" "$OUT/$2.s16" > "$OUT/$2.txt"
    ffmpeg -nostdin -v error -i "$1" -fps_mode passthrough -f rawvideo -pix_fmt rgb24 -y "$OUT/$2.ffmpeg.rgb"
    ffmpeg -nostdin -v quiet -i "$1" -map '0:a:0?' -f s16le -y "$OUT/$2.ffmpeg.s16" || :
    [ -f "$OUT/$2.s16" ] || : > "$OUT/$2.s16"
    [ -f "$OUT/$2.ffmpeg.s16" ] || : > "$OUT/$2.ffmpeg.s16"
    cmp "$OUT/$2.rgb" "$OUT/$2.ffmpeg.rgb" >&2 || { echo "$1: not the pictures FFmpeg decodes" >&2; return 1; }
    cmp "$OUT/$2.s16" "$OUT/$2.ffmpeg.s16" >&2 || { echo "$1: not the sound FFmpeg decodes" >&2; return 1; }
    echo "$2: $(head -1 "$OUT/$2.txt"): $(grep -c '^frame' "$OUT/$2.txt") frames and $(wc -c < "$OUT/$2.s16") bytes of sound as FFmpeg decodes them"
}
for f in "$OUT"/films/*.smk; do
    check "$f" "$(basename "$f" .smk)"
done
if [ -n "${SMACKER_FILMS:-}" ]; then
    n=0
    for f in "$SMACKER_FILMS"/*.smk "$SMACKER_FILMS"/*.SMK; do
        [ -f "$f" ] || continue
        check "$f" given > /dev/null
        n=$((n + 1))
    done
    echo "$SMACKER_FILMS: $n films as FFmpeg decodes them"
fi

# Cut short, and bytes changed in the header, the tables, the trees and the
# frames: no reads or writes outside.
python3 - "$OUT" <<'PY'
import os, random, subprocess, sys
out = sys.argv[1]
rng = random.Random(5)
cases = []
for name in ("smk2-mono8", "smk4-modes", "smk2-pcm"):
    data = open(os.path.join(out, "films", name + ".smk"), "rb").read()
    for n in range(0, len(data) + 1, max(1, len(data) // 150)):
        cases.append(data[:n])
    for i in list(range(104)) + rng.sample(range(104, len(data)), 300):
        for v in (0, 1, 0x7f, 0x80, 0xff, rng.randrange(256)):
            b = bytearray(data); b[i] = v; cases.append(bytes(b))
# smktest's own answers are 0 and 1; a sanitizer's is 99
env = dict(os.environ, ASAN_OPTIONS="exitcode=99", UBSAN_OPTIONS="exitcode=99")
for k, c in enumerate(cases):
    path = os.path.join(out, "cut.smk")
    open(path, "wb").write(c)
    r = subprocess.run([os.path.join(out, "smktest"), path], capture_output=True, env=env)
    if r.returncode not in (0, 1):
        sys.exit(f"case {k}: {r.returncode} {r.stderr.decode()[:600]}")
print(f"{len(cases)} cut or changed films: nothing read or written outside them")
PY

if [ -n "${QEMU_M68K:-}" ]; then
    m68k-linux-gnu-gcc -std=c99 -m68040 -O2 -static -Wall -Wextra -Werror -I"$HERE/smacker" -o "$OUT/smktest-m68k" \
        "$HERE/tests/smktest.c" "$HERE/smacker/smk_core.c"
    for f in "$OUT"/films/*.smk; do
        n=$(basename "$f" .smk)
        "$QEMU_M68K" "$OUT/smktest-m68k" "$f" "$OUT/$n.m68k.rgb" "$OUT/$n.m68k.s16" > /dev/null
        [ -f "$OUT/$n.m68k.s16" ] || : > "$OUT/$n.m68k.s16"
        cmp "$OUT/$n.rgb" "$OUT/$n.m68k.rgb"
        cmp "$OUT/$n.s16" "$OUT/$n.m68k.s16"
    done
    echo "68040 under qemu-m68k: the same pictures and sound"
fi
echo "smacker core: every film as FFmpeg decodes it"
