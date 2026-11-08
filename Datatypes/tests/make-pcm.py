#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. MIT licence.
# SPDX-License-Identifier: MIT
"""Test files for pcm/pcmcore.c (tests/test-pcm.sh): AIFF, AIFC and WAV
in every layout pcmcore reads, each with NAME.s16, the 16-bit samples it
must give (host order, interleaved, every channel), and a list.

    make-pcm.py OUTDIR
"""
import math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
RATE = 22050


def ext80(rate):
    e = rate.bit_length() - 1
    return struct.pack(">HII", 16383 + e, (rate << (31 - e)) & 0xffffffff, 0)


def signal(ch, n):
    """floats in [-1, 1): a different tone per channel, with full-scale ends"""
    v = []
    for i in range(n):
        for c in range(ch):
            x = 0.8 * math.sin(2 * math.pi * (300 + 200 * c) * i / RATE)
            if i == 1: x = 0.999969          # near +1
            if i == 2: x = -1.0              # -1
            v.append(x)
    return v


def to_int(x, bits):
    m = 1 << (bits - 1)
    return max(-m, min(m - 1, int(math.floor(x * m + 0.5))))


def expect16(vals, bits, enc):
    r = []
    for x in vals:
        if enc == "float":
            s = x * 32768
            r.append(max(-32768, min(32767, int(math.floor(abs(s) + 0.5)) * (1 if s >= 0 else -1))))
        elif bits == 8:
            r.append(to_int(x, 8) << 8)
        else:
            r.append(to_int(x, bits) >> (bits - 16))   # the top 16 bits
    return r


def pack(vals, bits, enc, order):
    b = bytearray()
    for x in vals:
        if enc == "float":
            b += struct.pack(order + "f", x)
        elif bits == 8 and enc == "u8":
            b.append(to_int(x, 8) + 128)
        elif bits == 8:
            b += struct.pack("b", to_int(x, 8))
        else:
            v = to_int(x, bits) & ((1 << bits) - 1)
            raw = v.to_bytes(bits // 8, "big")
            b += raw if order == ">" else raw[::-1]
    return bytes(b)


def chunk(cid, data, order):
    body = cid + struct.pack(order + "I", len(data)) + data
    return body + (b"\0" if len(data) & 1 else b"")


def aiff(name, ch, n, bits, comp=None):
    vals = signal(ch, n)
    enc = "float" if comp == b"fl32" else "int"
    order = "<" if comp == b"sowt" else ">"
    comm = struct.pack(">hIh", ch, n, bits if enc == "int" else 32) + ext80(RATE)
    kind = b"AIFF"
    pre = b""
    if comp:
        kind = b"AIFC"
        comm += comp + b"\x04none"
        pre = chunk(b"FVER", struct.pack(">I", 0xA2805140), ">")
    data = pack(vals, bits if enc == "int" else 32, enc, order)
    # SSND before COMM in one of them: the order is free
    ssnd = chunk(b"SSND", struct.pack(">II", 0, 0) + data, ">")
    body = pre + (ssnd + chunk(b"COMM", comm, ">") if name.endswith("rev") else chunk(b"COMM", comm, ">") + ssnd)
    body += chunk(b"ANNO", b"x", ">")
    write(name + ".aif", b"FORM" + struct.pack(">I", 4 + len(body)) + kind + body,
          expect16(vals, bits if enc == "int" else 32, enc), ch, n)


def wav(name, ch, n, bits, enc="int", ext=False):
    vals = signal(ch, n)
    tag = 3 if enc == "float" else 1
    fmt = struct.pack("<HHIIHH", 0xFFFE if ext else tag, ch, RATE, RATE * ch * bits // 8, ch * bits // 8, bits)
    if ext:
        fmt += struct.pack("<HHI", 22, bits, 0) + struct.pack("<H", tag) + bytes(14)
    data = pack(vals, bits, "u8" if bits == 8 else enc, "<")
    body = chunk(b"fmt ", fmt, "<") + chunk(b"LIST", b"INFOx", "<") + chunk(b"data", data, "<")
    write(name + ".wav", b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body,
          expect16(vals, bits, enc), ch, n)


listing = []


def write(fn, data, exp, ch, n):
    open(os.path.join(out, fn), "wb").write(data)
    open(os.path.join(out, fn + ".s16"), "wb").write(struct.pack("=%dh" % len(exp), *exp))
    listing.append("%s %d %d %d" % (fn, ch, n, RATE))


aiff("a8", 1, 501, 8)
aiff("a16", 2, 1000, 16)
aiff("a16rev", 2, 777, 16)
aiff("a24", 2, 333, 24)
aiff("a32", 1, 250, 32)
aiff("c16", 2, 600, 16, b"NONE")
aiff("csowt", 2, 600, 16, b"sowt")
aiff("cfl32", 1, 400, 32, b"fl32")
aiff("a6ch", 6, 100, 16)
wav("w8", 1, 501, 8)
wav("w16", 2, 1000, 16)
wav("w24", 2, 333, 24)
wav("w32", 1, 250, 32)
wav("wf32", 2, 300, 32, "float")
wav("wext", 2, 300, 16, ext=True)
wav("wextf", 1, 300, 32, "float", ext=True)
open(os.path.join(out, "list"), "w").write("\n".join(listing) + "\n")
