#!/bin/sh
# cdxl.datatype's core (cdxl/cdxl_core.c) on the host: makes the test films
# (tests/make-cdxl.py), plays each through tests/cdxltest.c and compares what
# every frame shows and its sound with what make-cdxl.py says, byte for byte.
# When ffmpeg is installed, its own CDXL decoder must show the same pictures.
# Then every film cut short at every length and with each header byte
# changed, under AddressSanitizer and UBSan: nothing is read outside the
# film. With QEMU_M68K set (qemu-m68k), the core is built for the 68040 with
# m68k-linux-gnu-gcc and must give the same answers there.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC          the host compiler (default cc)
#   SAN         sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#   QEMU_M68K   qemu-m68k, to run the core as 68k code too
#
# usage: sh tests/test-cdxl.sh [OUTDIR]       (default work/cdxl-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/cdxl-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
mkdir -p "$OUT"
python3 "$HERE/tests/make-cdxl.py" "$OUT"
# shellcheck disable=SC2086
$CC -O1 -g -Wall -Wextra -Werror $SAN -I"$HERE/cdxl" -o "$OUT/cdxltest" "$HERE/tests/cdxltest.c" "$HERE/cdxl/cdxl_core.c"
cd "$OUT"
args=
while read -r name rest; do args="$args $name.cdxl $name"; done < films.txt
# shellcheck disable=SC2086
CDXL_IOLOG=io.txt ./cdxltest $args > got.txt
diff -u films.txt got.txt
# How the frames are found and read (the datatype's DOS packets): a film of
# one frame size opens in constant time (frame 0's header, the last's and
# the length), a frame a read in order (a seek back to the first only);
# one whose sizes vary frame by frame; and backwards a seek and a read a
# frame.
cat io.txt
python3 - io.txt <<'PY'
import re, sys
bad = []
for line in open(sys.argv[1]):
    m = re.match(r"(\S+): (constant time|frame by frame), (\d+) reads and (\d+) length to open, (\d+) reads and (\d+) seeks for (\d+) frames", line)
    if m:
        name, how, opens, lens, reads, seeks, frames = m.groups()
        opens, lens, reads, seeks, frames = int(opens), int(lens), int(reads), int(seeks), int(frames)
        if name == "vary" and how != "frame by frame":
            bad.append(line)
        if name not in ("vary",) and how != "constant time":
            bad.append(line)
        if how == "constant time" and (opens > 2 or lens != 1):
            bad.append(line)
        # in order: a read a frame, a seek only to the first (trap: indexed
        # frame by frame on the way)
        if name != "trap" and (reads != frames or seeks > 1):
            bad.append(line)
        continue
    m = re.match(r"(\S+): backwards (\d+) reads, (\d+) seeks", line)
    if m and int(m.group(2)) != int(m.group(3)):
        bad.append(line)
if bad:
    sys.exit("reads and seeks not as they should be:\n" + "".join(bad))
print("frames found and read in as few reads as they can be")
PY
while read -r name w h frames fps rate; do
    cmp "$name.rgb" "$name.got.rgb"
    cmp "$name.pcm" "$name.got.pcm"
    echo "$name: ${w}x$h, $frames frames at $fps a second, sound at $rate Hz: pictures and sound as made"
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -nostdin -v error -f cdxl -i "$name.cdxl" -f rawvideo -pix_fmt rgb24 -y "$name.ffmpeg.rgb"
        if cmp -s "$name.rgb" "$name.ffmpeg.rgb"; then echo "$name: ffmpeg's CDXL decoder shows the same"
        else echo "$name: ffmpeg's CDXL decoder shows something else" >&2; exit 1; fi
    fi
done < films.txt
# Cut short at every length, and each header byte changed: no reads outside.
python3 - "$OUT" <<'PY'
import os, subprocess, sys
out = sys.argv[1]
data = open(os.path.join(out, "ham6.cdxl"), "rb").read()
cases = []
for n in range(0, len(data) + 1, 7):
    cases.append(data[:n])
for i in range(32):
    for v in (0, 1, 0x20, 0x80, 0xff):
        b = bytearray(data); b[i] = v; cases.append(bytes(b))
for k, c in enumerate(cases):
    open(os.path.join(out, "cut.cdxl"), "wb").write(c)
    r = subprocess.run([os.path.join(out, "cdxltest"), os.path.join(out, "cut.cdxl"), os.path.join(out, "cut")],
                       capture_output=True)
    if r.returncode not in (0, 2):
        sys.exit(f"case {k}: {r.returncode} {r.stderr.decode()[:400]}")
print(f"{len(cases)} cut or changed films: nothing read outside them")
PY
if [ -n "${QEMU_M68K:-}" ]; then
    m68k-linux-gnu-gcc -m68040 -O2 -static -Wall -Wextra -Werror -I"$HERE/cdxl" -o cdxltest-m68k \
        "$HERE/tests/cdxltest.c" "$HERE/cdxl/cdxl_core.c"
    mkdir -p m68k
    (cd m68k && args=; while read -r name rest; do args="$args ../$name.cdxl $name"; done < ../films.txt
     # shellcheck disable=SC2086
     "$QEMU_M68K" ../cdxltest-m68k $args > got.txt)
    # (io.txt as on the host: CDXL_IOLOG unset here)
    diff -u films.txt m68k/got.txt
    while read -r name rest; do cmp "$name.rgb" "m68k/$name.got.rgb"; cmp "$name.pcm" "m68k/$name.got.pcm"; done < films.txt
    echo "68040 under qemu-m68k: the same pictures and sound"
fi
echo "cdxl core: all films as made"
