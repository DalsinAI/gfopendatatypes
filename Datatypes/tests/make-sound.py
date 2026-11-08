#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# SPDX-License-Identifier: MIT
"""Test files for opensound/snddec.c (tests/test-sound.sh), through the
system's libsndfile (ctypes): a 1.5 s stereo tone (440 Hz left, 1000 Hz
right, -6 dBFS) at 44100 Hz and a mono one at 22050 Hz, as FLAC, Ogg
Vorbis and MP3; with each, NAME.s16, libsndfile's own decode of it (host
order, interleaved), and a list. Prints "skip" and writes nothing when
libsndfile can't write a format.

    make-sound.py OUTDIR
"""
import ctypes, ctypes.util, math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
lib = ctypes.util.find_library("sndfile")
if not lib:
    print("skip: no libsndfile")
    sys.exit(0)
L = ctypes.CDLL(lib)


class INFO(ctypes.Structure):
    _fields_ = [("frames", ctypes.c_int64), ("samplerate", ctypes.c_int), ("channels", ctypes.c_int),
                ("format", ctypes.c_int), ("sections", ctypes.c_int), ("seekable", ctypes.c_int)]


L.sf_open.restype = ctypes.c_void_p
L.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(INFO)]
L.sf_writef_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
L.sf_readf_short.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_short), ctypes.c_int64]
L.sf_readf_short.restype = ctypes.c_int64
L.sf_close.argtypes = [ctypes.c_void_p]
FMT = {"flac": 0x170000 | 0x0002, "ogg": 0x200000 | 0x0060, "mp3": 0x230000 | 0x0082}
listing = []
for rate, ch in ((44100, 2), (22050, 1)):
    n = rate * 3 // 2
    s = []
    for i in range(n):
        for c in range(ch):
            s.append(0.5 * math.sin(2 * math.pi * (440 if c == 0 else 1000) * i / rate))
    for ext, fmt in FMT.items():
        fn = os.path.join(out, "tone%d%s.%s" % (rate, "s" if ch == 2 else "m", ext))
        info = INFO(0, rate, ch, fmt, 0, 0)
        f = L.sf_open(fn.encode(), 0x20, ctypes.byref(info))
        if not f:
            print("skip: libsndfile doesn't write %s" % ext)
            continue
        L.sf_writef_float(f, (ctypes.c_float * len(s))(*s), n)
        L.sf_close(f)
        info = INFO()
        f = L.sf_open(fn.encode(), 0x10, ctypes.byref(info))
        buf = (ctypes.c_short * (info.frames * ch))()
        got = L.sf_readf_short(f, buf, info.frames)
        L.sf_close(f)
        open(fn + ".s16", "wb").write(struct.pack("=%dh" % (got * ch), *buf[:got * ch]))
        listing.append("%s %d %d %d" % (os.path.basename(fn), ch, got, rate))
open(os.path.join(out, "list"), "w").write("\n".join(listing) + "\n")
