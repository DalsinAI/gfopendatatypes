#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# SPDX-License-Identifier: MIT
"""gfopensound.datatype 47.3's way through media.decode/1 (tests/test-sound-service.sh),
against gfopenservice's host library, with the requests the datatype makes,
on make-sound.py's FLAC, Ogg Vorbis and MP3 tones and libsndfile's decodes:

  - loading (loadSound): PROBE with the file kept (extra[3] KEEP, buf3 the
    handle), at most stereo and 28000 Hz; then one DECODE naming the kept
    file and dropping it. 44.1 kHz comes as 22.05 kHz, the length halved,
    each channel's tone where it was (zero crossings);
  - reading (OIA_Reader, OIM_READSOUND): PROBE at the file's own rate, then
    DECODEs of 1000 frames from the frame reached, the file named each time
    and let go by RELEASE at the end: the pieces joined are libsndfile's
    decode, FLAC sample for sample, Vorbis and MP3 within a few LSB once
    lined up (decoders differ by an encoder delay at most);
  - a file that is not sound: PROBE says no (-2), so the datatype goes on to
    the decoders on this CPU.

    soundservice.py SERVICE_HOST_DIR SOUNDDIR
"""
import struct
import sys

sys.path.insert(0, sys.argv[1])
import media_decode as md  # noqa: E402

D = sys.argv[2]
PROBE, DECODE, RELEASE = 1, 2, 34
KEEP, DROP = 2, 4
fails = 0


def check(what, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else ": " + detail))
    fails += not ok


def kept(h):
    return h << 8


def crossings(pcm, ch, c):
    s = pcm[c::ch]
    return sum(1 for a, b in zip(s, s[1:]) if a < 0 <= b)


for line in open(D + "/list"):
    name, ch, n, rate = line.split()
    ch, n, rate = int(ch), int(n), int(rate)
    data = open(D + "/" + name, "rb").read()
    ref = struct.unpack("<%dh" % (n * ch), open(D + "/" + name + ".s16", "rb").read())

    # loading: PROBE keeping the file, one DECODE naming and dropping it
    st, frames, r, out = md.call(PROBE, 0, [2, 28000, 0, KEEP], 2 | 8, [data, None, None, None], [0, 24, 0, 4])
    info = struct.unpack(">6I", out[1]) if st == 0 else (0,) * 6
    h = struct.unpack(">I", out[3])[0] if st == 0 else 0
    fourcc = {"flac": b"FLAC", "ogg": b"VORB", "mp3": b"MP3 "}[name.rsplit(".", 1)[1]]
    want = rate // 2 if rate > 28000 else rate
    check(f"{name}: PROBE kind 3, {fourcc.decode()}, {ch} ch at {want} Hz, file kept",
          st == 0 and info[0] == 3 and info[1] == struct.unpack(">I", fourcc)[0] and info[4] == want
          and info[5] == ch and h != 0, f"st {st} info {info} handle {h:#x}")
    st, got, r, out = md.call(DECODE, 0, [2, 28000, 0, kept(h) | DROP], 2 | 8, [b"", None, None, None],
                              [0, frames * ch * 2, 0, 4])
    pcm = struct.unpack(">%dh" % (got * ch), out[1][:got * ch * 2]) if st == 0 else ()
    halves = n * want // rate
    check(f"{name}: DECODE by the kept file gives {got} frames (~{halves})", st == 0 and abs(got - halves) < 1200,
          f"st {st} got {got}")
    for c in range(ch):
        a, b = crossings(pcm, ch, c) if pcm else 0, crossings(ref, ch, c)
        check(f"{name}: channel {c}'s tone ({a} cycles, libsndfile {b})", abs(a - b) <= max(3, b // 50), "")
    st, *_ = md.call(DECODE, 0, [2, 28000, 0, kept(h)], 2, [b"", None, None, None], [0, 64, 0, 0])
    check(f"{name}: dropped after the last DECODE (-7)", st == -7, f"st {st}")

    # reading: PROBE at the file's rate, DECODE in pieces from the frame reached
    st, frames, r, out = md.call(PROBE, 0, [2, 0, 0, KEEP], 2 | 8, [data, None, None, None], [0, 24, 0, 4])
    h = struct.unpack(">I", out[3])[0] if st == 0 else 0
    check(f"{name}: reader PROBE at {r} Hz, {frames} frames", st == 0 and r == rate and abs(frames - n) < 2400,
          f"st {st} rate {r} frames {frames}")
    pos, pieces, calls = 0, [], 0
    while pos < frames and calls < 10000:
        k = min(1000, frames - pos)
        st, got, r2, out = md.call(DECODE, pos, [2, 0, 0, kept(h)], 2 | 8, [b"", None, None, None], [0, k * ch * 2, 0, 4])
        calls += 1
        if st != 0 or not got:
            break
        pieces.append(out[1][:got * ch * 2])
        pos += got
    check(f"{name}: {calls} DECODEs reach the end", pos == frames, f"pos {pos} of {frames}, st {st}")
    st, *_ = md.call(RELEASE, h, [0, 0, 0, 0], 0, [None] * 4, [0] * 4)
    check(f"{name}: RELEASE lets it go", st == 0, f"st {st}")
    b = b"".join(pieces)
    pcm = struct.unpack(">%dh" % (len(b) // 2), b)
    lossless = name.endswith(".flac")
    m = len(pcm) // ch
    if lossless:
        best = (max(abs(pcm[i] - ref[i]) for i in range(len(ref))) if len(pcm) >= len(ref) else 99999, 0)
    else:
        # the delay: the shift whose 400 frames from 5000 on match best, then the largest difference over all
        def score(shift, frm, to, step):
            return max(abs(pcm[(i + shift) * ch + c] - ref[i * ch + c]) for i in range(frm, to, step)
                       for c in range(ch))
        shift = min(range(-2400, 2401), key=lambda s: score(s, 5000, 5400, 1))
        lo, hi = max(0, -shift) + 2000, min(n, m - shift) - 2000
        best = (score(shift, lo, hi, 1), shift)
    tol = 0 if lossless else 64
    check(f"{name}: the pieces joined are libsndfile's decode (largest difference {best[0]}, "
          f"{best[1]} frames of delay)", best[0] <= tol, "")

st, *_ = md.call(PROBE, 0, [2, 28000, 0, 0], 2, [b"definitely not a sound file", None, None, None], [0, 24, 0, 0])
check("not a sound: PROBE says no, for the decoders on this CPU", st == -2, f"st {st}")
print("all passed" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
