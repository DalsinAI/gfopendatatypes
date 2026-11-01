#!/usr/bin/env python3
"""Make small music modules for testing gfopenmodule.datatype, with no
downloads: every sample is synthesised here and every byte written by hand.

    make-modules.py OUTDIR

writes, all playing the same little tune (bass, drums, a lead with
arpeggio and vibrato, a pad):

  mod.ot-4ch        ProTracker, 4 channels, M.K.            (~31 s)
  mod.ot-8ch        FastTracker MOD, 8 channels, 8CHN       (~31 s)
  mod.ot-st15       SoundTracker, 15 instruments, no magic  (~31 s)
  med.ot-mmd0       MED / OctaMED MMD0, 4 tracks            (~31 s)
  ot-okt.okta       Oktalyzer, 4 channels                   (~31 s)
  ot-xm.xm          FastTracker 2 XM, 6 channels            (~31 s)
  ot-xm32.xm        FastTracker 2 XM, 32 channels: a heavy one (~31 s)

The formats are as libxmp reads them (src/loaders/*_load.c in libxmp
4.7.3). Check them on the PC with modbench (modbench.c) built against
libxmp for the host, which renders each to a WAV file.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import math
import os
import random
import struct
import sys

# ProTracker periods, C-1 to B-3 (finetune 0).
PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 340, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]
NOTE = {n: i for i, n in enumerate(
    ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"])}


def note_index(name):
    """'C-2' -> 0..35 index into PERIODS (C-1 is 0)."""
    return NOTE[name[:2]] + 12 * (int(name[2]) - 1)


# --- the samples (signed 8-bit) ------------------------------------------------

def clip8(v):
    return max(-128, min(127, int(round(v))))


def samples():
    """(name, data, loop_start, loop_len, volume): loop_len 0 = one shot."""
    rnd = random.Random(1987)
    square = [clip8(90 if i < 16 else -90) for i in range(32)]
    saw = [clip8(-110 + 220 * i / 63) for i in range(64)]
    sine = [clip8(100 * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    kick = []
    phase = 0.0
    for i in range(2400):
        f = 180 * math.exp(-i / 500) + 45      # pitch falls (at 8287 Hz, C-2)
        phase += 2 * math.pi * f / 8287
        kick.append(clip8(120 * math.exp(-i / 900) * math.sin(phase)))
    snare = [clip8(110 * math.exp(-i / 380) * (rnd.random() * 2 - 1)) for i in range(1800)]
    hat = [clip8(70 * math.exp(-i / 90) * (rnd.random() * 2 - 1)) for i in range(500)]
    # MOD sample lengths are even; the first two bytes of a one-shot are 0
    # by custom (ProTracker loops them once played).
    kick[0:2] = [0, 0]
    snare[0:2] = [0, 0]
    hat[0:2] = [0, 0]
    return [
        ("square lead", square, 0, 32, 48),
        ("saw bass", saw, 0, 64, 56),
        ("sine pad", sine, 0, 64, 40),
        ("kick", kick, 0, 0, 64),
        ("snare", snare, 0, 0, 52),
        ("hat", hat, 0, 0, 36),
    ]


# --- the tune ----------------------------------------------------------------
# A row is a list of cells per channel: (note name or None, sample 1-based or
# 0, effect, parameter). Three 64-row patterns, played 0 1 2 1.

LEAD = 1
BASS = 2
PAD = 3
KICK = 4
SNARE = 5
HAT = 6


def pattern(p, channels):
    rows = [[(None, 0, 0, 0) for _ in range(channels)] for _ in range(64)]
    bass_lines = [
        ["C-1", "C-1", "G-1", "C-1", "A#1", "A#1", "F-1", "G-1"],
        ["A-1", "A-1", "E-1", "A-1", "F-1", "F-1", "G-1", "G-1"],
        ["C-1", "D#1", "F-1", "G-1", "C-1", "D#1", "G-1", "C-2"],
    ]
    lead = [
        ["C-3", None, "D#3", None, "G-3", None, "D#3", "F-3", "G-3", None, "A#2", None, "C-3", None, None, None],
        ["A-2", None, "C-3", None, "E-3", None, "C-3", "D-3", "E-3", None, "G-2", None, "A-2", None, None, None],
        ["G-3", "F-3", "D#3", "D-3", "C-3", None, "D-3", None, "D#3", None, "G-2", None, "C-3", None, None, None],
    ]
    pads = [["C-2", "A#1"], ["A-1", "F-1"], ["C-2", "G-1"]]
    for r in range(64):
        cell = [(None, 0, 0, 0)] * channels
        # channel 0: bass, every 4 rows, with a volume set
        if r % 4 == 0:
            cell[0] = (bass_lines[p][(r // 8) % 8], BASS, 0xC, 0x30 if r % 8 else 0x38)
        # channel 1: drums
        if r % 16 in (0, 10):
            cell[1] = ("C-2", KICK, 0, 0)
        elif r % 16 in (4, 12):
            cell[1] = ("C-2", SNARE, 0, 0)
        elif r % 2 == 0:
            cell[1] = ("F-3", HAT, 0xC, 0x20 if r % 4 else 0x30)
        # channel 2: lead, every 4 rows; arpeggio on held notes, vibrato too
        n = lead[p][(r // 4) % 16]
        if r % 4 == 0 and n:
            cell[2] = (n, LEAD, 0, 0)
        elif r % 4 == 2 and lead[p][(r // 4) % 16]:
            cell[2] = (None, 0, 0x0, 0x37) if p != 1 else (None, 0, 0x4, 0x46)
        # channel 3: pad, every 32 rows, then a slow volume slide down
        if r % 32 == 0:
            cell[3] = (pads[p][r // 32], PAD, 0xC, 0x28)
        elif r % 32 >= 16:
            cell[3] = (None, 0, 0xA, 0x01)
        if channels > 4:
            # an echo of the lead an octave down, two rows late, quieter;
            # chords on the pad
            src = rows[r - 2][2] if r >= 2 else (None, 0, 0, 0)
            if src[0]:
                lower = src[0][:2] + str(max(1, int(src[0][2]) - 1))
                cell[4] = (lower, LEAD, 0xC, 0x18)
            if r % 32 == 0:
                root = note_index(pads[p][r // 32])
                cell[5] = (name_of(min(35, root + 4)), PAD, 0xC, 0x20)
                cell[6] = (name_of(min(35, root + 7)), PAD, 0xC, 0x20)
            if r % 8 == 6:
                cell[7] = ("C-3", HAT, 0xC, 0x18)
            # eight channels share the mix: keep the sum under full scale
            cell = [(c[0], c[1], c[2], c[3] * 2 // 3) if c[2] == 0xC else c for c in cell]
        if r == 0 and p == 0:
            # speed 6, 125 BPM (the defaults, set anyway)
            b = cell[3]
            cell[3] = (b[0], b[1], 0xF, 0x06)
        rows[r] = list(cell)
    return rows


def name_of(i):
    return ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"][i % 12] + str(i // 12 + 1)


ORDER = [0, 1, 2, 1]


# --- ProTracker MOD ------------------------------------------------------------

def mod_file(title, channels, magic, instruments=31):
    smp = samples()
    out = bytearray(title.encode()[:20].ljust(20, b"\0"))
    for i in range(instruments):
        if i < len(smp):
            name, data, ls, ll, vol = smp[i]
            out += name.encode()[:22].ljust(22, b"\0")
            out += struct.pack(">HBBHH", len(data) // 2, 0, vol, ls // 2, ll // 2 if ll else 1)
        else:
            out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([len(ORDER), 127 if instruments == 31 else 0x78])
    out += bytes(ORDER + [0] * (128 - len(ORDER)))
    if magic:
        out += magic.encode()
    for p in range(3):
        for row in pattern(p, channels):
            for (n, s, fx, prm) in row:
                per = PERIODS[note_index(n)] if n else 0
                out += bytes([(s & 0xF0) | (per >> 8), per & 0xFF, ((s & 0x0F) << 4) | fx, prm])
    for _, data, _, _, _ in smp[:instruments]:
        out += bytes(v & 0xFF for v in data)
    return bytes(out)


# --- MED MMD0 ------------------------------------------------------------------

def mmd0_file(title):
    smp = samples()
    rows = [pattern(p, 4) for p in range(3)]
    header_len = 52
    song_len = 63 * 8 + 2 + 2 + 256 + 2 + 1 + 1 + 1 + 1 + 16 + 1 + 1
    song_off = header_len
    blocks = []
    for p in range(3):
        b = bytearray([4, 63])
        for row in rows[p]:
            for (n, s, fx, prm) in row:
                note = note_index(n) + 1 if n else 0       # 1 = C-1
                # MED commands: C volume (hex, as the song's flags say),
                # A volume slide, 0 arpeggio, 4 vibrato; F would be tempo,
                # so speed is 9
                if fx == 0xF:
                    fx, prm = 0x9, prm
                b += bytes([note | ((s & 0x10) << 3) | ((s & 0x20) << 1), ((s & 0x0F) << 4) | fx, prm])
        blocks.append(bytes(b))
    pos = song_off + song_len
    blockarr_off = pos
    pos += 4 * len(blocks)
    block_offs = []
    for b in blocks:
        block_offs.append(pos)
        pos += len(b)
        pos += pos & 1
    smplarr_off = pos
    pos += 4 * len(smp)
    smp_offs = []
    for _, data, _, _, _ in smp:
        smp_offs.append(pos)
        pos += 6 + len(data)
        pos += pos & 1
    expdata_off = pos
    exp_len = 80
    pos += exp_len
    name_off = pos
    name = title.encode() + b"\0"
    pos += len(name)
    total = pos

    out = bytearray(total)
    struct.pack_into(">4sIIHHIIIIII", out, 0, b"MMD0", total, song_off, 0, 0,
                     blockarr_off, 0, smplarr_off, 0, expdata_off, 0)
    # song
    s = bytearray()
    for i in range(63):
        if i < len(smp):
            _, data, ls, ll, vol = smp[i]
            s += struct.pack(">HHBBBb", ls // 2, ll // 2 if ll else 0, 0, 0, vol, 0)
        else:
            s += struct.pack(">HHBBBb", 0, 0, 0, 0, 0x40, 0)
    s += struct.pack(">HH", len(blocks), len(ORDER))
    s += bytes(ORDER + [0] * (256 - len(ORDER)))
    # deftempo 125 BPM, transpose 0, flags: hex volumes and PT-style
    # slides, flags2: BPM mode with 4 lines a beat, tempo2 (speed) 6
    s += struct.pack(">HbBBB", 125, 0, 0x10 | 0x20, 0x20 | 3, 6)
    s += bytes([64] * 16) + bytes([64, len(smp)])
    assert len(s) == song_len
    out[song_off:song_off + song_len] = s
    for i, off in enumerate(block_offs):
        struct.pack_into(">I", out, blockarr_off + 4 * i, off)
        out[off:off + len(blocks[i])] = blocks[i]
    for i, off in enumerate(smp_offs):
        struct.pack_into(">I", out, smplarr_off + 4 * i, off)
        data = smp[i][1]
        struct.pack_into(">Ih", out, off, len(data), 0)
        out[off + 6:off + 6 + len(data)] = bytes(v & 0xFF for v in data)
    # expdata: only the song name
    exp = bytearray(exp_len)
    struct.pack_into(">II", exp, 44, name_off, len(name))
    out[expdata_off:expdata_off + exp_len] = exp
    out[name_off:name_off + len(name)] = name
    return bytes(out)


# --- Oktalyzer -----------------------------------------------------------------

def okt_file():
    smp = samples()

    def chunk(cid, data):
        return cid + struct.pack(">I", len(data)) + data
    out = bytearray(b"OKTASONG")
    out += chunk(b"CMOD", struct.pack(">4H", 0, 0, 0, 0))
    sh = bytearray()
    for i in range(36):
        if i < len(smp):
            name, data, ls, ll, vol = smp[i]
            sh += name.encode()[:20].ljust(20, b"\0")
            sh += struct.pack(">IHHHH", len(data), ls // 2, ll // 2 if ll else 0, vol, 1)
        else:
            sh += b"\0" * 20 + struct.pack(">IHHHH", 0, 0, 0, 0, 1)
    out += chunk(b"SAMP", bytes(sh))
    out += chunk(b"SPEE", struct.pack(">H", 6))
    out += chunk(b"SLEN", struct.pack(">H", 3))
    out += chunk(b"PLEN", struct.pack(">H", len(ORDER)))
    out += chunk(b"PATT", bytes(ORDER + [0] * (128 - len(ORDER))))
    for p in range(3):
        b = bytearray(struct.pack(">H", 64))
        for row in pattern(p, 4):
            for (n, s, fx, prm) in row:
                # Oktalyzer: note 1-36 (C-1..B-3 as 1..36), its own effects:
                # 31 volume, 28 speed; arpeggio and vibrato left out
                okfx, okp = 0, 0
                if fx == 0xC:
                    okfx, okp = 31, min(prm, 64)
                elif fx == 0xF:
                    okfx, okp = 28, prm
                elif fx == 0xA:
                    okfx, okp = 31, 0x60 + (prm & 0x0F)   # fine slide down per row
                if n:
                    b += bytes([note_index(n) + 1, s - 1, okfx, okp])
                else:
                    b += bytes([0, 0, okfx, okp])
        out += chunk(b"PBOD", bytes(b))
    for _, data, _, _, _ in smp:
        # Oktalyzer's 8-bit mode keeps samples as they are
        out += chunk(b"SBOD", bytes(v & 0xFF for v in data))
    return bytes(out)


# --- FastTracker 2 XM ------------------------------------------------------------

def xm_file(title, channels=6):
    smp = samples()
    out = bytearray(b"Extended Module: ")
    out += title.encode()[:20].ljust(20, b"\0") + b"\x1a"
    out += b"openmodule tests".ljust(20, b"\0")
    out += struct.pack("<H", 0x0104)
    order = ORDER + [0] * (256 - len(ORDER))
    # header size 276, song length, restart, channels, patterns, instruments,
    # flags (1 = linear frequencies), speed, BPM
    out += struct.pack("<IHHHHHHHH", 276, len(ORDER), 0, channels, 3, len(smp), 1, 6, 125)
    out += bytes(order)
    for p in range(3):
        data = bytearray()
        rows = pattern(p, 8)
        for row in rows:
            for c in range(channels):
                n, s, fx, prm = row[c % 8]
                # XM note 1 = C-0; ProTracker's C-1 is XM's C-4 (the sample
                # relative note below lines the pitches up)
                note = note_index(n) + 1 + 36 if n else 0
                # past eight channels the same tune again, quietly (volume
                # column 0x10 + volume), a semitone apart: work for the mixer
                vol = 0x10 + 6 if c >= 8 and n else 0
                if c >= 8 and n:
                    note = min(96, note + c // 8)
                data += bytes([note, s, vol, fx, prm])
        out += struct.pack("<IBHH", 9, 0, 64, len(data)) + data
    for name, data, ls, ll, vol in smp:
        out += struct.pack("<I", 263) + name.encode()[:22].ljust(22, b"\0") + bytes([0]) + struct.pack("<H", 1)
        out += struct.pack("<I", 40)
        out += bytes(96)                         # every note plays sample 0
        out += bytes(48) + bytes(48)             # no envelopes
        out += bytes([0, 0, 0, 0, 0, 0, 0, 0])   # points, sustain, loops
        out += bytes([0, 0])                     # envelope types: off
        out += bytes([0, 0, 0, 0])               # vibrato
        out += struct.pack("<H", 0) + bytes(22)  # fadeout, reserved
        # sample header: lengths in bytes; loop type 1 = forward; panning
        # centre; relative note: ProTracker's C-2 plays 8287 Hz, XM's C-4
        # 8363 Hz with relative note 0
        out += struct.pack("<IIIBbBBbB", len(data), ls, ll, vol, 0, 1 if ll else 0, 128, 0, 0)
        out += name.encode()[:22].ljust(22, b"\0")
        prev = 0                                 # the instrument's sample
        for v in data:                           # data follows, delta coded
            out.append((v - prev) & 0xFF)
            prev = v
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    d = argv[1]
    os.makedirs(d, exist_ok=True)
    files = {
        "mod.ot-4ch": mod_file("openmodule 4ch test", 4, "M.K."),
        "mod.ot-8ch": mod_file("openmodule 8ch test", 8, "8CHN"),
        "mod.ot-st15": mod_file("openmodule st15", 4, None, instruments=15),
        "med.ot-mmd0": mmd0_file("openmodule MMD0 test"),
        "ot-okt.okta": okt_file(),
        "ot-xm.xm": xm_file("openmodule XM test"),
        "ot-xm32.xm": xm_file("openmodule XM 32ch", channels=32),
    }
    for name, data in files.items():
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")


if __name__ == "__main__":
    main(sys.argv)
