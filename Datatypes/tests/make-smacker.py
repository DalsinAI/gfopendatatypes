#!/usr/bin/env python3
"""Smacker films for smacker.datatype's tests (tests/test-smacker.sh), made
by an encoder of our own, written from the format, so the tests need no
film from a game.

    make-smacker.py OUTDIR

Each film is small and exercises what a decoder must get right: all four
block types and their runs, SMK4's three ways of drawing a full block, the
three cached values of the big trees (escapes), trees left out, palette
changes made of kept, copied and new entries, the ring frame, and sound
tracks packed (8 and 16-bit, mono and stereo) and as plain PCM. FFmpeg's
decoder is the reference the test holds the core to.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import math
import os
import random
import struct
import sys


class Bits:
    """Bits written from the low bit of each byte up"""

    def __init__(self):
        self.out = bytearray()
        self.n = 0

    def put(self, v, count=1):
        for i in range(count):
            if self.n % 8 == 0:
                self.out.append(0)
            self.out[-1] |= ((v >> i) & 1) << (self.n % 8)
            self.n += 1

    def code(self, path):
        for b in path:
            self.put(b)


def tree_shape(values, rng):
    """A full binary tree over the values: nested pairs, leaves the values,
    of uneven depths so codes of many lengths come out"""
    nodes = list(values)
    rng.shuffle(nodes)
    while len(nodes) > 1:
        i = rng.randrange(len(nodes) - 1)
        nodes[i:i + 2] = [(nodes[i], nodes[i + 1])]
    return nodes[0]


def paths(shape, prefix=(), out=None):
    if out is None:
        out = {}
    if isinstance(shape, tuple):
        paths(shape[0], prefix + (0,), out)
        paths(shape[1], prefix + (1,), out)
    else:
        out.setdefault(shape, prefix)
    return out


class Small:
    """A tree of bytes"""

    def __init__(self, values, rng):
        self.shape = tree_shape(sorted(set(values)) or [0], rng)
        self.codes = paths(self.shape)

    def write(self, bits):
        def walk(s):
            if isinstance(s, tuple):
                bits.put(1)
                walk(s[0])
                walk(s[1])
            else:
                bits.put(0)
                bits.put(s, 8)
        walk(self.shape)

    def put(self, bits, v):
        bits.code(self.codes[v])


ESC = ("esc0", "esc1", "esc2")


class Big:
    """A tree of 16-bit values with the three caches: values is every
    value the film draws through it; with escapes, three more leaves stand
    for the caches. present False: the tree is left out (every value 0)."""

    def __init__(self, values, rng, escapes=True, present=True):
        self.present = present
        self.rng = rng
        if not present:
            return
        vals = sorted(set(values)) or [0]
        self.esc = []
        if escapes:
            free = [v for v in range(0x10000) if v not in set(vals)]
            self.esc = rng.sample(free[:4096], 3)
        leaves = vals + [ESC[k] for k in range(len(self.esc))]
        self.shape = tree_shape(leaves, rng)
        self.codes = paths(self.shape)
        lo = [v & 0xFF for v in vals] + [e & 0xFF for e in self.esc]
        hi = [v >> 8 for v in vals] + [e >> 8 for e in self.esc]
        self.lo, self.hi = Small(lo, rng), Small(hi, rng)
        self.entries = sum(1 for _ in self._walk(self.shape))

    def _walk(self, s):
        yield s
        if isinstance(s, tuple):
            yield from self._walk(s[0])
            yield from self._walk(s[1])

    def size(self):
        return 0 if not self.present else (self.entries + 4) * 4

    def write(self, bits):
        if not self.present:
            bits.put(0)
            return
        bits.put(1)
        for t in (self.lo, self.hi):
            bits.put(1)
            t.write(bits)
            bits.put(0)
        escs = self.esc + [0x1FFFF] * (3 - len(self.esc))
        for e in escs:
            bits.put(e & 0xFFFF, 16)

        def walk(s):
            if isinstance(s, tuple):
                bits.put(1)
                walk(s[0])
                walk(s[1])
            else:
                bits.put(0)
                v = self.esc[ESC.index(s)] if s in ESC else s
                self.lo.put(bits, v & 0xFF)
                self.hi.put(bits, v >> 8)
        walk(self.shape)
        bits.put(0)

    def reset(self):
        self.cache = [0, 0, 0]

    def put(self, bits, v):
        if not self.present:
            assert v == 0
            return
        ks = [k for k in range(len(self.esc)) if self.cache[k] == v]
        if ks and self.rng.random() < 0.7:
            bits.code(self.codes[ESC[ks[0]]])
        else:
            bits.code(self.codes[v])
        if v != self.cache[0]:
            self.cache = [v, self.cache[0], self.cache[1]]


RUNS = [i + 1 for i in range(59)] + [128, 256, 512, 1024, 2048]


def runs(n):
    """n blocks as run table indices"""
    out = []
    while n:
        i = max(k for k, r in enumerate(RUNS) if r <= n)
        out.append(i)
        n -= RUNS[i]
    return out


MONO, FULL, SKIP, FILL = 0, 1, 2, 3


def block(px, w, bx, by):
    return [px[(by * 4 + r) * w + bx * 4 + c] for r in range(4) for c in range(4)]


def classify(cur, prev, version, w, bx, by):
    b = block(cur, w, bx, by)
    if prev is not None and b == block(prev, w, bx, by):
        return (SKIP, 0)
    cols = sorted(set(b))
    if len(cols) == 1:
        return (FILL, cols[0])
    if len(cols) == 2:
        return (MONO, 0)
    if version == 4:
        if all(b[r * 4 + c] == b[(r & ~1) * 4 + (c & ~1)] for r in range(4) for c in range(4)):
            return (FULL, 1)
        if all(b[r * 4 + c] == b[(r & ~1) * 4 + c] for r in range(4) for c in range(4)):
            return (FULL, 2)
    return (FULL, 0)


def plan(cur, prev, version, w, h):
    """The frame's runs: (kind, extra, blocks), and the values each tree draws"""
    bw, bh = w // 4, h // 4
    kinds = [classify(cur, prev, version, w, bx, by) for by in range(bh) for bx in range(bw)]
    out = []
    for k in kinds:
        if out and out[-1][0] == k:
            out[-1][1] += 1
        else:
            out.append([k, 1])
    return kinds, out


def frame_video(cur, prev, version, w, h, trees, bits):
    bw = w // 4
    kinds, groups = plan(cur, prev, version, w, h)
    for t in trees.values():
        t.reset()
    blk = 0
    for (kind, extra), n in groups:
        for ri in runs(n):
            ty = kind | ri << 2 | ((extra << 8) if kind == FILL else 0)
            trees["types"].put(bits, ty)
            if kind == FULL and version == 4:
                bits.code({0: (0, 0), 1: (1,), 2: (0, 1)}[extra])
            for _ in range(RUNS[ri]):
                bx, by = blk % bw, blk // bw
                b = block(cur, w, bx, by)
                if kind == MONO:
                    lo, hi = sorted(set(b))
                    m = sum(1 << i for i in range(16) if b[i] == hi)
                    trees["mclr"].put(bits, hi << 8 | lo)
                    trees["mmap"].put(bits, m)
                elif kind == FULL and extra == 0:
                    for r in range(4):
                        trees["full"].put(bits, b[r * 4 + 2] | b[r * 4 + 3] << 8)
                        trees["full"].put(bits, b[r * 4] | b[r * 4 + 1] << 8)
                elif kind == FULL and extra == 1:
                    for r in (0, 2):
                        trees["full"].put(bits, b[r * 4] | b[r * 4 + 2] << 8)
                elif kind == FULL and extra == 2:
                    for r in (0, 2):
                        trees["full"].put(bits, b[r * 4 + 2] | b[r * 4 + 3] << 8)
                        trees["full"].put(bits, b[r * 4] | b[r * 4 + 1] << 8)
                blk += 1


def tree_values(frames, version, w, h):
    vals = {"mmap": set(), "mclr": set(), "full": set(), "types": set()}
    prev = None
    for cur in frames:
        bw = w // 4
        kinds, groups = plan(cur, prev, version, w, h)
        blk = 0
        for (kind, extra), n in groups:
            for ri in runs(n):
                vals["types"].add(kind | ri << 2 | ((extra << 8) if kind == FILL else 0))
                for _ in range(RUNS[ri]):
                    b = block(cur, w, blk % bw, blk // bw)
                    if kind == MONO:
                        lo, hi = sorted(set(b))
                        vals["mclr"].add(hi << 8 | lo)
                        vals["mmap"].add(sum(1 << i for i in range(16) if b[i] == hi))
                    elif kind == FULL:
                        rows = (0, 1, 2, 3) if extra == 0 else (0, 2)
                        for r in rows:
                            if extra == 1:
                                vals["full"].add(b[r * 4] | b[r * 4 + 2] << 8)
                            else:
                                vals["full"].add(b[r * 4 + 2] | b[r * 4 + 3] << 8)
                                vals["full"].add(b[r * 4] | b[r * 4 + 1] << 8)
                    blk += 1
        prev = cur
    return vals


def c6(v):
    return (v << 2) | (v >> 4)


def palette_chunk(old, new):
    """Ops turning old into new (both 256 lists of 6-bit triples), as a
    chunk: its length / 4, the ops, padding"""
    ops = bytearray()
    i = 0
    while i < 256:
        if new[i] == old[i]:
            n = 1
            while i + n < 256 and n < 128 and new[i + n] == old[i + n]:
                n += 1
            ops.append(0x80 | (n - 1))
            i += n
            continue
        best = (0, 0)
        for off in range(256):
            n = 0
            while i + n < 256 and off + n < 256 and n < 64 and new[i + n] == old[off + n]:
                n += 1
            if n > best[0]:
                best = (n, off)
        if best[0] >= 2:
            ops += bytes([0x40 | (best[0] - 1), best[1]])
            i += best[0]
            continue
        ops += bytes(new[i])
        i += 1
    total = (len(ops) + 1 + 3) & ~3
    return bytes([total // 4]) + bytes(ops) + bytes(total - 1 - len(ops))


def audio_packed(samples, bits16, stereo):
    """A packet of Smacker DPCM: samples interleaved, unsigned 8-bit or signed 16-bit"""
    rng = random.Random(len(samples))
    ch = 2 if stereo else 1
    b = Bits()
    b.put(1)
    b.put(stereo)
    b.put(bits16)
    pred = list(samples[:ch])
    diffs = [[] for _ in range(ch)]
    p = list(pred)
    for i in range(ch, len(samples)):
        c = i & 1 if stereo else 0
        d = (samples[i] - p[c]) & (0xFFFF if bits16 else 0xFF)
        diffs[c].append(d)
        p[c] = samples[i]
    trees = []
    for c in range(ch):
        if bits16:
            trees += [Small([d & 0xFF for d in diffs[c]], rng), Small([d >> 8 for d in diffs[c]], rng)]
        else:
            trees.append(Small(diffs[c], rng))
    for t in trees:
        b.put(1)
        t.write(b)
        b.put(0)
    for k in range(ch - 1, -1, -1):
        if bits16:
            v = pred[k] & 0xFFFF
            b.put((v >> 8) | (v & 0xFF) << 8, 16)
        else:
            b.put(pred[k], 8)
    idx = [0] * ch
    for i in range(ch, len(samples)):
        c = i & 1 if stereo else 0
        d = diffs[c][idx[c]]
        idx[c] += 1
        if bits16:
            trees[c * 2].put(b, d & 0xFF)
            trees[c * 2 + 1].put(b, d >> 8)
        else:
            trees[c].put(b, d)
    unp = len(samples) * (2 if bits16 else 1)
    return struct.pack("<I", unp) + bytes(b.out)


def audio_raw(samples, bits16):
    if bits16:
        return struct.pack("<%dh" % len(samples), *samples)
    return bytes(samples)


def tone(n, rate, freq, bits16, stereo, start, rng):
    out = []
    for i in range(n):
        t = (start + i) / rate
        for c in range(2 if stereo else 1):
            v = math.sin(2 * math.pi * freq * (1 + c * 0.5) * t) * 0.6 + (rng.random() - 0.5) * 0.1
            out.append(int(v * 30000) if bits16 else max(0, min(255, int(128 + v * 120))))
    return out


def pictures(w, h, n, seed):
    """Frames with flat areas, two-colour patterns, detail, doubled
    pixels, and parts that stay still"""
    rng = random.Random(seed)
    frames = []
    cur = [0] * (w * h)
    for f in range(n):
        cur = list(cur)
        for by in range(h // 4):
            for bx in range(w // 4):
                if f and rng.random() < 0.35:
                    continue                    # left as it was: a skip
                kind = rng.randrange(5)
                a, b = rng.randrange(256), rng.randrange(256)
                for r in range(4):
                    for c in range(4):
                        if kind == 0:
                            v = a
                        elif kind == 1:
                            v = a if (r * 3 + c * 5 + f) % 3 else b
                        elif kind == 2:
                            v = rng.randrange(256)
                        elif kind == 3:
                            v = (a + (r // 2) * 7 + (c // 2) * 13) & 0xFF
                        else:
                            v = (a + (r // 2) * 7 + c * 13) & 0xFF
                        cur[(by * 4 + r) * w + bx * 4 + c] = v
        frames.append(cur)
    return frames


def palettes(n, seed):
    rng = random.Random(seed)
    pal = [(rng.randrange(64), rng.randrange(64), rng.randrange(64)) for _ in range(256)]
    out = [list(pal)]
    for f in range(1, n):
        pal = list(out[-1])
        change = rng.randrange(4)
        if change == 1:                          # a few new colours
            for _ in range(rng.randrange(1, 20)):
                pal[rng.randrange(256)] = (rng.randrange(64), rng.randrange(64), rng.randrange(64))
        elif change == 2:                        # a stretch moved along (copies)
            s, d, k = rng.randrange(200), rng.randrange(200), rng.randrange(2, 50)
            old = list(pal)
            for i in range(k):
                if d + i < 256 and s + i < 256:
                    pal[d + i] = old[s + i]
        elif change == 3:                        # a fade
            pal = [tuple(max(0, x - 3) for x in p) for p in pal]
        out.append(pal)
    return out


def film(path, w, h, frames_n, version=2, ring=False, rate_field=-6666, tracks=(), seed=1,
         escapes=True, drop_trees=()):
    """tracks: (rate, bits16, stereo, packed) each"""
    rng = random.Random(seed)
    stored = frames_n + (1 if ring else 0)
    pics = pictures(w, h, stored, seed)
    if "fills" in drop_trees:                    # only fills and skips: three trees unused
        pics = [[(p // 16) * 16 for p in pic] for pic in pics]
        for pic in pics:
            for by in range(h // 4):
                for bx in range(w // 4):
                    v = pic[(by * 4) * w + bx * 4]
                    for r in range(4):
                        for c in range(4):
                            pic[(by * 4 + r) * w + bx * 4 + c] = v
    pals = palettes(stored, seed + 7)
    vals = tree_values(pics, version, w, h)
    trees = {k: Big(vals[k], rng, escapes, present=bool(vals[k]) and k not in drop_trees)
             for k in ("mmap", "mclr", "full", "types")}
    tb = Bits()
    for k in ("mmap", "mclr", "full", "types"):
        trees[k].write(tb)
    tree_bytes = bytes(tb.out) + bytes((-len(tb.out)) % 4)

    fps_rate = 1000 / rate_field if rate_field > 0 else 100000 / -rate_field if rate_field < 0 else 10
    chunks, types, sizes = [], [], []
    audio_max = [0] * 7
    old = [(0, 0, 0)] * 256
    prev = None
    pos = [0] * len(tracks)
    for f in range(stored):
        data = bytearray()
        ftype = 0
        if f == 0 or pals[f] != old:
            data += palette_chunk(old, pals[f])
            ftype |= 1
            old = pals[f]
        for k, (rate, bits16, stereo, packed) in enumerate(tracks):
            if f % 5 == 3 and k == 0:
                continue                         # a frame without this track's packet
            n = int(rate / fps_rate) + (37 if f == 0 else 0) + rng.randrange(-3, 4)
            s = tone(n, rate, 440 + 110 * k, bits16, stereo, pos[k], rng)
            pos[k] += n
            pkt = audio_packed(s, bits16, stereo) if packed else audio_raw(s, bits16)
            audio_max[k] = max(audio_max[k], len(s) * (2 if bits16 else 1))
            data += struct.pack("<I", len(pkt) + 4) + pkt
            ftype |= 2 << k
        vb = Bits()
        frame_video(pics[f], prev, version, w, h, trees, vb)
        data += vb.out
        data += bytes((-len(data)) % 4)
        prev = pics[f]
        chunks.append(bytes(data))
        types.append(ftype)
        sizes.append(len(data) | (1 if f == 0 else 0))

    rates = [0] * 7
    for k, (rate, bits16, stereo, packed) in enumerate(tracks):
        rates[k] = rate | 0x40000000 | (0x80000000 if packed else 0) | (0x20000000 if bits16 else 0) \
            | (0x10000000 if stereo else 0)
    hdr = b"SMK" + str(version).encode()
    hdr += struct.pack("<IIIiI", w, h, frames_n, rate_field, 1 if ring else 0)
    hdr += struct.pack("<7I", *audio_max)
    hdr += struct.pack("<I", len(tree_bytes))
    hdr += struct.pack("<4I", *(trees[k].size() for k in ("mmap", "mclr", "full", "types")))
    hdr += struct.pack("<7I", *rates)
    hdr += struct.pack("<I", 0)
    assert len(hdr) == 104
    body = struct.pack("<%dI" % stored, *sizes) + bytes(types) + tree_bytes + b"".join(chunks)
    with open(path, "wb") as fh:
        fh.write(hdr + body)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "smacker"
    os.makedirs(out, exist_ok=True)
    film(os.path.join(out, "smk2-mono8.smk"), 64, 48, 12, tracks=[(11025, 0, 0, 1)], seed=1)
    film(os.path.join(out, "smk2-stereo16.smk"), 96, 64, 10, rate_field=66, tracks=[(22050, 1, 1, 1)], seed=2)
    film(os.path.join(out, "smk4-modes.smk"), 80, 48, 14, version=4, ring=True, rate_field=-8333,
         tracks=[(22050, 1, 0, 1), (11025, 0, 1, 1)], seed=3)
    film(os.path.join(out, "smk2-pcm.smk"), 64, 32, 8, rate_field=0, tracks=[(16000, 1, 0, 0), (8000, 0, 1, 0)], seed=4)
    film(os.path.join(out, "smk2-stereo8.smk"), 32, 32, 9, tracks=[(11025, 0, 1, 1)], seed=5)
    film(os.path.join(out, "smk2-noescape.smk"), 48, 48, 6, escapes=False, seed=6)
    film(os.path.join(out, "smk2-fills.smk"), 64, 64, 6, drop_trees=("fills",), tracks=[(22050, 1, 0, 1)], seed=7)
    film(os.path.join(out, "smk4-big.smk"), 320, 200, 5, version=4, tracks=[(22050, 1, 1, 1)], seed=8)
    for name in sorted(os.listdir(out)):
        print(os.path.join(out, name), os.path.getsize(os.path.join(out, name)))


if __name__ == "__main__":
    main()
