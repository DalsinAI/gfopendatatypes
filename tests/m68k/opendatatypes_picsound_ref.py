#!/usr/bin/env python3
"""GFOpenDatatypes' picture and sound classes: the test's files, script and
expected log (tests/m68k/test_opendatatypes_picsound.sh).

Writes, into OUTDIR, ILBM, PBM and 8SVX files made here (no media of
anyone's), raw pixel-array files, the script opendatatypes_picsound_test.c
runs and the log it must print. The expected log comes from this file's own
models, written from the IFF ILBM and 8SVX specifications and the
picture/sound autodocs, not from the C: an ILBM decoder (ByteRun1, planes,
HAM6/HAM8, EHB, masks, deep ILBMs), the picture class's remap onto the
mock screen (whose pens are shared as ObtainBestPen shares them), scaling
and mask planes, the 8SVX decoder (Fibonacci delta, stereo) and
sound.datatype's audio.device plan.

  python3 -I opendatatypes_picsound_ref.py OUTDIR SEED
Copyright (c) 2026 AmiCradle Team. MIT.
"""
import os
import random
import struct
import sys

CLOCK = 3546895
MAXLEN = 131070


def chunk(cid, data):
    out = cid + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def form(ftype, chunks):
    body = ftype + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def rowbytes(w):
    return ((w + 15) >> 4) << 1


# ---- ByteRun1, written here two ways, read one way --------------------------

def pack_simple(data):
    """Replicate runs of two or more, literals otherwise."""
    out, i, n = bytearray(), 0, len(data)
    while i < n:
        r = 1
        while i + r < n and r < 128 and data[i + r] == data[i]:
            r += 1
        if r >= 2:
            out += bytes([(256 - (r - 1)) & 0xff, data[i]])
            i += r
            continue
        s = i
        while i < n and i - s < 128 and not (i + 1 < n and data[i] == data[i + 1]):
            i += 1
        out += bytes([i - s - 1]) + data[s:i]
    return bytes(out)


def unpack(data, n):
    out, i = bytearray(), 0
    while len(out) < n and i < len(data):
        c = data[i]
        i += 1
        if c < 128:
            lit = data[i:i + c + 1]
            i += c + 1
            out += lit
        elif c != 128:
            if i >= len(data):
                break
            out += bytes([data[i]]) * (257 - c)
            i += 1
    out = out[:n]
    return bytes(out) + bytes(n - len(out))


# ---- ILBM files -------------------------------------------------------------------

def make_raw(spec, rng):
    """The unpacked BODY and the pixel values it holds."""
    w, h, d = spec["w"], spec["h"], spec["depth"]
    pbm = spec.get("pbm")
    hasmask = spec.get("masking") == 1 and not pbm
    vals = [[rng.randrange(1 << min(d, 24)) if d <= 24 else rng.randrange(1 << 32) for _ in range(w)]
            for _ in range(h)]
    if "values" in spec:
        vals = spec["values"]
    maskbits = [[rng.randrange(2) for _ in range(w)] for _ in range(h)]
    raw = bytearray()
    rb = rowbytes(w)
    for y in range(h):
        if pbm:
            row = bytes(vals[y]) + (b"\0" if w & 1 else b"")
            raw += row
            continue
        for p in range(d):
            pl = bytearray(rb)
            for x in range(w):
                if (vals[y][x] >> p) & 1:
                    pl[x >> 3] |= 0x80 >> (x & 7)
            raw += pl
        if hasmask:
            pl = bytearray(rb)
            for x in range(w):
                if maskbits[y][x]:
                    pl[x >> 3] |= 0x80 >> (x & 7)
            raw += pl
    return bytes(raw), vals


def write_ilbm(spec, rng):
    w, h, d = spec["w"], spec["h"], spec["depth"]
    cmp = spec.get("cmp", 1)
    if spec.get("body") is not None:
        body = spec["body"]                               # as given (a size no body could fill)
    elif cmp == 1:
        raw, vals = make_raw(spec, rng)
        if spec.get("stream"):
            body = pack_simple(raw)                       # runs across rows and planes
        else:
            rowsize = len(raw) // h
            body = b"".join(pack_simple(raw[y * rowsize:(y + 1) * rowsize]) for y in range(h))
            if spec.get("nop"):
                body = b"\x80" + body                     # a -128: no operation
    else:
        body = make_raw(spec, rng)[0]
    if spec.get("short"):
        body = body[:len(body) * 2 // 3]
    bmhd = struct.pack(">HHhhBBBBHBBhh", w, h, 3, -2, d, spec.get("masking", 0), cmp, spec.get("pad", 0),
                       spec.get("transparent", 0), spec.get("xasp", 10), spec.get("yasp", 11),
                       spec.get("pagew", 320), spec.get("pageh", 256))
    chunks = []
    if not spec.get("nobmhd"):
        chunks.append(chunk(b"BMHD", bmhd))
    if spec.get("name"):
        chunks.append(chunk(b"NAME", spec["name"].encode()))
    if spec.get("cmap") is not None:
        chunks.append(chunk(b"CMAP", bytes(c for rgb in spec["cmap"] for c in rgb)))
    if spec.get("camg") is not None:
        chunks.append(chunk(b"CAMG", struct.pack(">I", spec["camg"])))
    if spec.get("grab"):
        chunks.append(chunk(b"GRAB", struct.pack(">hh", *spec["grab"])))
    if not spec.get("nobody"):
        chunks.append(chunk(b"BODY", body))
    data = form(b"PBM " if spec.get("pbm") else b"ILBM", chunks)
    if spec.get("notform"):
        data = b"CAT " + data[4:]
    return data


# ---- the model of ilbm.datatype and picture.datatype ------------------------------------

def parse_iff(data):
    if len(data) < 12 or data[:4] != b"FORM":
        return None, {}, None, -10
    ftype = data[8:12]
    end = min(8 + struct.unpack(">I", data[4:8])[0], len(data))
    o, props = 12, {}
    while o + 8 <= end:
        cid = data[o:o + 4]
        size = min(struct.unpack(">I", data[o + 4:o + 8])[0], end - o - 8)
        if cid == b"BODY":
            return ftype, props, data[o + 8:o + 8 + size], 0
        props[cid] = data[o + 8:o + 8 + size]
        o += 8 + size + (size & 1)
    return ftype, props, None, -1


def model_ilbm(data):
    ftype, props, body, r = parse_iff(data)
    if r:
        return {"err": 2007 if r == -1 else 2008}
    pbm = ftype == b"PBM "
    bh = props.get(b"BMHD")
    if bh is None or len(bh) < 20:
        return {"err": 2008}
    w, h, left, top, d, masking, cmp, pad, transp, xa, ya, pw, ph = struct.unpack(">HHhhBBBBHBBhh", bh[:20])
    if cmp > 1:
        return {"err": 2006}
    if not w or not h or not d or (d != 8 if pbm else (d > 8 and d not in (24, 32))):
        return {"err": 2008}
    cm = props.get(b"CMAP")
    ncmap = min(len(cm) // 3, 256) if cm else 0
    camg = props.get(b"CAMG")
    if camg is not None and len(camg) >= 4:
        mode = struct.unpack(">I", camg[:4])[0]
        if not mode & 0xffff0000:
            mode &= ~(0x0002 | 0x0100 | 0x2000 | 0x4000)
    else:
        mode = (0x8000 if w >= 640 else 0) | (4 if h >= 400 else 0)
        if d == 6 and ncmap == 32:
            mode |= 0x80
    if mode & 0x800 and d not in (6, 8):
        mode &= ~0x800
    if mode & 0x80 and d != 6:
        mode &= ~0x80
    ham, ehb, deep = bool(mode & 0x800), bool(mode & 0x80), d > 8
    ncol = 0 if deep else (16 if d == 6 else 64) if ham else 64 if ehb else 1 << d
    pal = [[0, 0, 0] for _ in range(256)]
    if not ncmap and not deep and ncol > 1:
        for i in range(ncol):
            pal[i] = [i * 255 // (ncol - 1)] * 3
    scale4 = not (pad & 0x80) and ncmap > 0 and all((b & 15) == 0 for b in cm[:ncmap * 3])
    for i in range(ncmap):
        pal[i] = [(b | b >> 4) if scale4 else b for b in cm[i * 3:i * 3 + 3]]
    if ehb:
        for i in range(32, 64):
            pal[i] = [c >> 1 for c in pal[i - 32]]
    rb = rowbytes(w)
    hasmask = not pbm and masking == 1
    rowsize = (w + (w & 1)) if pbm else rb * (d + (1 if hasmask else 0))
    if rowsize * h > 0xffffffff:
        return {"err": 103}                               # no buffer that size: ERROR_NO_FREE_STORE
    raw = unpack(body, rowsize * h) if cmp == 1 else (body[:rowsize * h] + bytes(max(0, rowsize * h - len(body))))
    argb, lut, alpha = [], [], []
    for y in range(h):
        src = raw[y * rowsize:(y + 1) * rowsize]
        cur = list(pal[0])
        arow, lrow, mrow = [], [], []
        for x in range(w):
            if pbm:
                v = src[x]
            else:
                v = 0
                for p in range(d):
                    if src[p * rb + (x >> 3)] & (0x80 >> (x & 7)):
                        v |= 1 << p
            if deep:
                arow.append(((v >> 24) & 0xff if d == 32 else 0xff, v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff))
            elif ham:
                sh = d - 2
                ctl, dd = v >> sh, v & ((1 << sh) - 1)
                if ctl == 0:
                    cur = list(pal[dd])
                else:
                    gun = {1: 2, 2: 0, 3: 1}[ctl]
                    cur[gun] = dd * 17 if d == 6 else (dd << 2) | (cur[gun] & 3)
                arow.append((0xff, cur[0], cur[1], cur[2]))
            else:
                lrow.append(v)
                if hasmask:
                    a = 0xff if src[d * rb + (x >> 3)] & (0x80 >> (x & 7)) else 0
                    mrow.append(a)
                elif masking == 2 and v == transp:
                    a = 0
                else:
                    a = 0xff
                arow.append((a, pal[v][0], pal[v][1], pal[v][2]))
        argb.append(arow)
        lut.append(lrow)
        alpha.append(mrow)
    return {"err": 0, "w": w, "h": h, "d": d, "masking": 4 if d == 32 else masking, "cmp": cmp, "pad": pad,
            "transp": transp, "xa": xa, "ya": ya, "pw": pw, "ph": ph, "mode": mode, "ncol": ncol, "pal": pal,
            "argb": argb, "lut": None if (deep or ham) else lut, "alpha": alpha if hasmask else None,
            "name": props.get(b"NAME"), "grab": struct.unpack(">hh", props[b"GRAB"][:4]) if b"GRAB" in props else (0, 0)}


class Screen:
    FIXED = [(0, 0, 0), (255, 255, 255), (0, 0x55, 0xaa), (0xff, 0x88, 0)]

    def __init__(self, depth):
        self.depth = depth
        self.npens = 1 << depth if depth <= 8 else 0
        self.rgb = [(0, 0, 0)] * max(self.npens, 1)
        self.share = [0] * max(self.npens, 1)
        self.fixed = [False] * max(self.npens, 1)
        for p in range(min(4, self.npens)):
            self.rgb[p] = self.FIXED[p]
            self.fixed[p] = True

    def obtain(self, c):
        if not self.npens:
            return -1
        used = [p for p in range(self.npens) if self.fixed[p] or self.share[p] > 0]
        for p in used:
            if self.rgb[p] == c:
                self.share[p] += 1
                return p
        for p in range(self.npens):
            if not (self.fixed[p] or self.share[p] > 0):
                self.rgb[p] = c
                self.share[p] = 1
                return p
        best = min(range(self.npens), key=lambda p: (sum((a - b) ** 2 for a, b in zip(self.rgb[p], c)), p))
        self.share[best] += 1
        return best


def hx(seq):
    return "".join("%02x" % b for b in seq)


def expect_ilbm(name, data, sdepth, remap, destmode, sw, sh, keep):
    out = ["== ilbm %s %d %d %d %d %d %d" % (name, sdepth, remap, destmode, sw, sh, keep)]
    m = model_ilbm(data)
    if m["err"]:
        return out + ["fail %d" % m["err"], "pens left 0", "live 0"]
    w, h = m["w"], m["h"]
    out.append("bmhd %d %d %d %d %d %d %d %d %d %d" % (w, h, m["d"], m["masking"], m["cmp"], m["transp"], m["xa"],
                                                      m["ya"], m["pw"], m["ph"]))
    out.append("mode %08x ncol %d" % (m["mode"], m["ncol"]))
    out.append("cmap" + "".join(" " + hx(m["pal"][i]) for i in range(m["ncol"])))
    if m["name"]:
        out.append("name " + m["name"].decode())
    out.append("grab %d %d" % m["grab"])
    argb = m["argb"]
    out += ["argb " + hx(b for px in row for b in px) for row in argb]
    if m["lut"] is None:
        out.append("lut no")
    else:
        out += ["lut " + hx(row) for row in m["lut"]]
    out += ["grey " + hx((px[1] * 77 + px[2] * 151 + px[3] * 28) >> 8 for px in row) for row in argb]
    if sw:
        out.append("scale 1")
        if keep:
            if sw * h > sh * w:
                sw = max(sh * w // h, 1)
            else:
                sh = max(sw * h // w, 1)
    dw, dh = (sw, sh) if sw else (w, h)
    store_argb = m["lut"] is None
    scr = Screen(sdepth) if sdepth else None
    rm = bool(remap and scr)
    truecolour = (sdepth > 8 and destmode == 1) if rm else store_argb
    depth = 8 if (rm and sdepth > 8 and not truecolour) else sdepth
    alloc = 0

    def src(x, y):
        return (y * h // dh), (x * w // dw)

    if truecolour:
        dest = [[argb[src(x, y)[0]][src(x, y)[1]] for x in range(dw)] for y in range(dh)]
        if not rm:
            depth = 24
    elif not store_argb:
        nc = m["ncol"] or 256
        if rm:
            ct = []
            for c in range(nc):
                p = scr.obtain(tuple(m["pal"][c]))
                alloc += 1
                ct.append(0 if p < 0 else p)
        else:
            ct = list(range(256))
            depth = m["d"] if 1 <= m["d"] <= 8 else 8
        dest = [[ct[m["lut"][src(x, y)[0]][src(x, y)[1]]] for x in range(dw)] for y in range(dh)]
    else:
        L = 6 if depth >= 8 else 5 if depth == 7 else 4 if depth == 6 else 3 if depth == 5 else 2
        cube = {}
        for r in range(L):
            for g in range(L):
                for b in range(L):
                    p = scr.obtain((r * 255 // (L - 1), g * 255 // (L - 1), b * 255 // (L - 1)))
                    alloc += 1
                    cube[(r, g, b)] = 0 if p < 0 else p

        def lv(v):
            return (v * (L - 1) + 127) // 255

        dest = []
        for y in range(dh):
            row = []
            for x in range(dw):
                px = argb[src(x, y)[0]][src(x, y)[1]]
                row.append(cube[(lv(px[1]), lv(px[2]), lv(px[3]))])
            dest.append(row)
    out.append("layout 1 %d %d depth=%d alloc=%d" % (dw, dh, depth, min(alloc, 256)))
    for row in dest:
        out.append("dest " + (hx(b for px in row for b in px) if (truecolour) else hx(row)))
    masking = m["masking"]
    if m["alpha"] is not None or masking in (1, 2, 4):
        def opaque(y, x):
            if m["alpha"] is not None:
                return m["alpha"][y][x] != 0
            if store_argb:
                return masking != 4 or argb[y][x][0] >= 0x80
            if masking == 2:
                return m["lut"][y][x] != m["transp"]
            return True
        rb = rowbytes(dw)
        for y in range(dh):
            row = bytearray(rb)
            for x in range(dw):
                sy, sx = src(x, y)
                if opaque(sy, sx):
                    row[x >> 3] |= 0x80 >> (x & 7)
            out.append("mask " + hx(row))
    else:
        out.append("mask none")
    bw, bh = min(16, dw - 1), min(12, dh - 1)
    if bw > 0 and bh > 0:
        has_mask = m["alpha"] is not None or masking in (1, 2, 4)
        out.append("blit 1 1 10 5 %d %d %d" % (bw, bh, 1 if has_mask else 0))
    out += ["rt " + hx(b for px in row for b in px[1:]) for row in argb]
    out += ["pens left 0", "live 0"]
    return out


# ---- PDTM_WRITEPIXELARRAY and READPIXELARRAY ---------------------------------------------

def pixw_case(rng, w, h, depth, ncol, fmt):
    pal = [(rng.randrange(256), rng.randrange(256), rng.randrange(256)) for _ in range(ncol)]
    bpp = 3 if fmt == 0 else 4 if fmt <= 2 else 1
    data = bytes(rng.randrange(256) if fmt != 3 else rng.randrange(max(ncol, 1) + 2) for _ in range(w * h * bpp))
    blob = struct.pack(">HHBHB", w, h, depth, ncol, fmt) + bytes(c for rgb in pal for c in rgb) + data
    palf = pal + [(0, 0, 0)] * (256 - ncol)
    store_lut = fmt in (3, 4) and depth <= 8
    pix = []
    for i in range(w * h):
        s = data[i * bpp:(i + 1) * bpp]
        if fmt == 0:
            a, r, g, b = 0xff, s[0], s[1], s[2]
        elif fmt == 1:
            a, r, g, b = s[3], s[0], s[1], s[2]
        elif fmt == 2:
            a, r, g, b = s
        elif fmt == 4:
            a, r, g, b = 0xff, s[0], s[0], s[0]
        else:
            a, (r, g, b) = 0xff, palf[s[0]]
        if not store_lut:
            pix.append((a, r, g, b))
        elif fmt in (3, 4):
            pix.append(s[0])
        else:
            n = ncol or 1
            pix.append(min(range(n), key=lambda k: (sum((x - y) ** 2 for x, y in zip(palf[k], (r, g, b))), k)))
    return blob, pal, store_lut, pix


def expect_pixw(name, w, h, pal, store_lut, pix):
    out = ["== pixw " + name, "write 1"]
    palf = list(pal) + [(0, 0, 0)] * (256 - len(pal))

    def argb(i):
        return (0xff,) + tuple(palf[pix[i]]) if store_lut else pix[i]

    def reads(tag, fmt):
        if fmt == 3 and not store_lut:
            return ["%s no" % tag]
        rows = []
        for y in range(h):
            row = []
            for x in range(w):
                i = y * w + x
                if fmt == 3:
                    row.append(pix[i])
                    continue
                a, r, g, b = argb(i)
                row += [[r, g, b], [r, g, b, a], [a, r, g, b], None, [(r * 77 + g * 151 + b * 28) >> 8]][fmt]
            rows.append("%s %s" % (tag, hx(row)))
        return rows

    for f in range(5):
        out += reads("rd%d" % f, f)
    one = list(range(1, 17))
    for y in range(2):
        for x in range(2):
            i = (h - 2 + y) * w + (w - 2 + x)
            v = one[y * 4 + x]
            pix[i] = v if store_lut else (0xff, v, v, v)
    out.append("clip 1")
    out += reads("rd2", 0)
    out.append("live 0")
    return out


# ---- 8SVX and sound.datatype --------------------------------------------------------------

FIB = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21]


def fib_pack(samples):
    """A delta stream that the decoder reproduces exactly (each step one of FIB)."""
    out = bytearray([0, samples[0] & 0xff])
    x, codes = (samples[0] + 128) % 256 - 128, []
    for s in samples[1:]:
        code = rng_fib.randrange(16)
        x = (x + FIB[code] + 128) % 256 - 128
        codes.append(code)
    if len(codes) & 1:
        codes.append(8)
    for i in range(0, len(codes), 2):
        out.append(codes[i] << 4 | codes[i + 1])
    return bytes(out)


rng_fib = random.Random(1)


def fib_unpack(data):
    if len(data) < 2:
        return []
    x = (data[1] + 128) % 256 - 128
    out = []
    for i in range(2 * (len(data) - 2)):
        d = data[2 + (i >> 1)]
        d = d & 15 if i & 1 else d >> 4
        x = (x + FIB[d] + 128) % 256 - 128
        out.append(x & 0xff)
    return out


def write_8svx(spec, rng):
    n = spec["n"]
    chans = 2 if spec.get("chan") == 6 else 1
    bodies = []
    for c in range(chans):
        if spec.get("cmp") == 1:
            bodies.append(fib_pack([rng.randrange(256) for _ in range(n // 2 + 2)]))
        else:
            bodies.append(bytes(rng.randrange(256) for _ in range(n)))
    vhdr = struct.pack(">IIIHBBI", spec.get("oneshot", n), spec.get("repeat", 0), spec.get("percycle", 0),
                       spec.get("rate", 8363), spec.get("oct", 1), spec.get("cmp", 0), spec.get("vol", 0x10000))
    chunks = []
    if not spec.get("novhdr"):
        chunks.append(chunk(b"VHDR", vhdr))
    if spec.get("name"):
        chunks.append(chunk(b"NAME", spec["name"].encode()))
    if spec.get("chan") is not None:
        chunks.append(chunk(b"CHAN", struct.pack(">I", spec["chan"])))
    if not spec.get("nobody"):
        chunks.append(chunk(b"BODY", b"".join(bodies)))
    return form(b"8SVX", chunks)


def plan_lines(chans, length, period, volume, cycles, panset, pan, sigmask):
    """sound.datatype's plan, from what it holds (chans: lists of signed 8-bit samples as bytes)."""
    st = len(chans) == 2
    if not chans or not length:
        return None
    nbytes = (length + 1) & ~1
    nbytes = min(nbytes, MAXLEN * (64 // (2 if st else 1)))
    bufs = [bytes(c[:min(nbytes, length)]) + bytes(nbytes - min(nbytes, length)) for c in chans]
    role = 1
    if st:
        alloc = [3, 5, 10, 12]
    elif not panset:
        alloc = [1, 2, 4, 8]
    elif pan < 0x8000:
        alloc = [1, 8]
    elif pan > 0x8000:
        alloc = [2, 4]
    else:
        alloc, role = [3, 5, 10, 12], 3
    period = max(period, 124)
    nchunks = (nbytes + MAXLEN - 1) // MAXLEN
    out = ["play alloc=%s repeat=%d sig=%08x" % (",".join(map(str, alloc)), 1 if nchunks == 1 else cycles, sigmask)]
    for c in range(nchunks):
        off = c * MAXLEN
        ln = min(MAXLEN, nbytes - off)
        for ch in range(len(bufs)):
            seg = bufs[ch][off:off + ln]
            s = sum(b * (k % 7 + 1) for k, b in enumerate(seg)) & 0xffffffff
            out.append("seg r=%d v=%d p=%d c=%d len=%d sum=%08x" % (1 << ch if st else role, min(volume, 64),
                                                                     min(period, 65535),
                                                                     cycles if nchunks == 1 else 1, ln, s))
    return out


def replay(length, rate):
    if not rate:
        return 0, 0
    rest = length % rate
    return length // rate, (rest * 1000 // rate) * 1000 + ((rest * 1000) % rate) * 1000 // rate


def model_8svx(data):
    ftype, props, body, r = parse_iff(data)
    if r:
        return {"err": 2007 if r == -1 else 2008}
    vh = props.get(b"VHDR")
    if vh is None or len(vh) < 20:
        return {"err": 2008}
    oneshot, repeat, percycle, rate, octv, cmp, vol = struct.unpack(">IIIHBBI", vh[:20])
    if cmp > 1:
        return {"err": 2006}
    chan = struct.unpack(">I", props[b"CHAN"][:4])[0] if b"CHAN" in props and len(props[b"CHAN"]) >= 4 else 0
    nch = 2 if chan == 6 else 1
    half = len(body) // nch
    avail = (2 * (half - 2) if half >= 2 else 0) if cmp == 1 else half
    want = oneshot + repeat
    length = min(want, avail) if want else avail
    chans = []
    for c in range(nch):
        part = body[c * half:(c + 1) * half]
        s = fib_unpack(part) if cmp == 1 else list(part)
        chans.append(bytes(s[:length]))
    sps = rate or 8000
    return {"err": 0, "vh": (oneshot, repeat, percycle, rate, octv, cmp, vol), "chans": chans, "len": length,
            "sps": sps, "period": (CLOCK + sps // 2) // sps, "vol": 64 if vol >= 0x10000 else vol * 64 // 0x10000,
            "pan": 0 if chan == 2 else 0x10000 if chan == 4 else 0x8000, "panset": chan in (2, 4),
            "name": props.get(b"NAME")}


def sound_lines(s):
    out = ["snd len=%d stereo=%d period=%d sps=%d vol=%d cycles=1 pan=%d" % (
        s["len"], 1 if len(s["chans"]) == 2 else 0, s["period"], s["sps"], s["vol"], s["pan"])]
    out.append("L " + hx(s["chans"][0]))
    if len(s["chans"]) == 2:
        out.append("R " + hx(s["chans"][1]))
    return out


PENS = [1, 2, 1, 2, 1, 3, 1, 0, 2]


def render_lines(s, panel, active):
    out = []
    x0, y0, W, H = 10, 5, 40, 30
    pnl = panel and H > 14
    wh = H - (14 if pnl else 0)
    out.append("fill %d %d %d %d %d" % (PENS[7], x0, y0, x0 + W - 1, y0 + wh - 1))
    ln = s["len"]
    left = s["chans"][0]
    if ln:
        for x in range(W):
            s0 = x * ln // W
            s1 = max((x + 1) * ln // W, s0 + 1)
            vals = [(left[i] + 128) % 256 - 128 for i in range(s0, min(s1, ln))]
            if not vals:
                continue
            lo, hi = min(vals), max(vals)
            out.append("line %d %d %d %d %d" % (PENS[2], x0 + x, y0 + (127 - hi) * (wh - 1) // 255, x0 + x,
                                                y0 + (127 - lo) * (wh - 1) // 255))
    if pnl:
        py, mid = y0 + wh, x0 + W // 2
        for (bx0, bx1, play, down) in ((x0, mid - 1, True, active == 1), (mid, x0 + W - 1, False, active == 2)):
            by0, by1 = py, py + 13
            hi, lo = (PENS[4], PENS[3]) if down else (PENS[3], PENS[4])
            out.append("fill %d %d %d %d %d" % (PENS[5] if down else PENS[7], bx0, by0, bx1, by1))
            out += ["line %d %d %d %d %d" % (hi, bx0, by0, bx1, by0), "line %d %d %d %d %d" % (hi, bx0, by0, bx0, by1),
                    "line %d %d %d %d %d" % (lo, bx1, by0, bx1, by1), "line %d %d %d %d %d" % (lo, bx0, by1, bx1, by1)]
            cx, cy = (bx0 + bx1) // 2, (by0 + by1) // 2
            sz = max(min(bx1 - bx0, by1 - by0) // 4, 1)
            if play:
                for k in range(sz + 1):
                    out.append("line %d %d %d %d %d" % (PENS[2], cx - sz // 2 + k, cy - (sz - k), cx - sz // 2 + k,
                                                        cy + (sz - k)))
            else:
                out.append("fill %d %d %d %d %d" % (PENS[2], cx - sz, cy - sz, cx + sz, cy + sz))
    return out


def expect_svx(name, data, panel):
    out = ["== svx %s %d" % (name, panel)]
    s = model_8svx(data)
    if s["err"]:
        return out + ["fail %d" % s["err"], "live 0"]
    out.append("vh %d %d %d %d %d %d %d" % s["vh"])
    out += sound_lines(s)
    out.append("replay %d %d" % replay(s["len"], s["sps"]))
    if s["name"]:
        out.append("name " + s["name"].decode())
    plan = plan_lines(s["chans"], s["len"], s["period"], s["vol"], 1, s["panset"], s["pan"], 0)
    playing = plan is not None
    out += plan or []
    out.append("trig %d" % (1 if playing else 0))
    out += render_lines(s, panel, 0)
    if panel:
        if playing:
            out.append("stop")
        playing = False
        out.append("obtainrp")
        out += render_lines(s, panel, 2)
        out += render_lines(s, panel, 0)
        out.append("releaserp")
    else:
        if playing:
            out.append("stop")
        plan = plan_lines(s["chans"], s["len"], s["period"], s["vol"], 1, s["panset"], s["pan"], 0)
        out += plan or []
        playing = plan is not None
    out.append("goactive 2")
    if playing:
        out.append("stop")
    out.append("trig 1")
    # written as 8SVX (CHAN 6 for stereo only) and read back
    rate = min(s["sps"], 65535)
    vol = 0x10000 if s["vol"] >= 64 else s["vol"] * 0x10000 // 64
    r = {"len": s["len"], "chans": s["chans"], "sps": rate or 8000, "vol": 64 if vol >= 0x10000 else vol * 64 // 0x10000,
         "pan": 0x8000}
    r["period"] = (CLOCK + r["sps"] // 2) // r["sps"]
    out.append("rt")
    out += sound_lines(r)
    out.append("live 0")
    return out


def expect_snd(length, stereo, cycles, pan, typ):
    out = ["== snd %d %d %d %d %d" % (length, stereo, cycles, pan, typ)]
    inter = typ in (1, 3)
    st = inter or stereo
    chans = []
    for ch in range(2 if st else 1):
        chans.append(bytes((s * 37 + ch * 11) & 0xff for s in range(length)))
    sps = 22050
    period = (CLOCK + sps // 2) // sps
    out.append("period %d replay %d %d" % ((period,) + replay(length, sps)))
    plan = plan_lines(chans, length, period, 50, cycles, pan >= 0, pan, 1 << 12)
    out += plan or []
    out.append("trig %d" % (1 if plan else 0))
    if plan:
        out.append("stop")
    out.append("live 0")
    return out


def expect_sndvh(length):
    # SDTA_VoiceHeader: copied, its rate and volume taken; SDTA_SampleLength 0 stops replay
    out = ["== sndvh %d" % length, "vh %d 11025 32768" % length]
    period = (CLOCK + 11025 // 2) // 11025
    out.append("sps 11025 period %d vol 32" % period)
    plan = plan_lines([bytes((s * 37) & 0xff for s in range(length))], length, period, 32, 1, False, -1, 1 << 12)
    out += plan or []
    out.append("trig %d" % (1 if plan else 0))
    if plan:
        out.append("stop")
    return out + ["cleared", "live 0"]


# ---- the cases -------------------------------------------------------------------

def main():
    outdir, seed = sys.argv[1], int(sys.argv[2])
    rng = random.Random(seed)
    script, expected = [], []

    def pal(n, four=False):
        return [tuple((rng.randrange(16) << 4) if four else rng.randrange(256) for _ in range(3)) for _ in range(n)]

    ilbm_cases = [
        # (spec, screen depth, remap, destmode, sw, sh, keep)
        ({"w": 13, "h": 5, "depth": 1, "cmap": [(0, 0, 0), (255, 255, 255)]}, 4, 1, 0, 0, 0, 0),
        ({"w": 17, "h": 6, "depth": 3, "cmap": pal(8), "cmp": 0}, 4, 1, 0, 0, 0, 0),
        ({"w": 20, "h": 7, "depth": 4, "cmap": pal(16), "stream": True}, 8, 1, 0, 0, 0, 0),
        ({"w": 9, "h": 4, "depth": 5, "cmap": pal(32), "nop": True, "name": "odd"}, 0, 1, 0, 0, 0, 0),
        ({"w": 16, "h": 8, "depth": 8, "cmap": pal(256), "grab": (3, -4)}, 8, 1, 0, 0, 0, 0),
        ({"w": 12, "h": 5, "depth": 4, "cmap": pal(16, True)}, 4, 0, 0, 0, 0, 0),     # 4-bit CMAP
        ({"w": 12, "h": 5, "depth": 4, "cmap": pal(16, True), "pad": 0x80}, 4, 0, 0, 0, 0, 0),
        ({"w": 10, "h": 4, "depth": 4, "cmap": pal(5)}, 0, 1, 0, 0, 0, 0),             # CMAP too short
        ({"w": 10, "h": 4, "depth": 3}, 0, 1, 0, 0, 0, 0),                              # no CMAP: grey
        ({"w": 14, "h": 6, "depth": 4, "cmap": pal(16), "masking": 1}, 4, 1, 0, 0, 0, 0),
        ({"w": 14, "h": 6, "depth": 3, "cmap": pal(8), "masking": 2, "transparent": 5}, 4, 1, 0, 0, 0, 0),
        ({"w": 11, "h": 5, "depth": 8, "pbm": True, "cmap": pal(256)}, 0, 1, 0, 0, 0, 0),
        ({"w": 12, "h": 4, "depth": 6, "cmap": pal(16), "camg": 0x800}, 0, 1, 0, 0, 0, 0),           # HAM6
        ({"w": 12, "h": 4, "depth": 8, "cmap": pal(64), "camg": 0x800 | 0x8000}, 4, 1, 0, 0, 0, 0),  # HAM8
        ({"w": 12, "h": 4, "depth": 6, "cmap": pal(32), "camg": 0x80}, 0, 1, 0, 0, 0, 0),            # EHB
        ({"w": 12, "h": 4, "depth": 6, "cmap": pal(32)}, 0, 1, 0, 0, 0, 0),             # EHB, no CAMG
        ({"w": 12, "h": 4, "depth": 4, "cmap": pal(16), "camg": 0x4000 | 0x2000 | 0x8004}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 24}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 24}, 24, 1, 1, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 24}, 6, 1, 0, 0, 0, 0),                                # cube of pens
        ({"w": 7, "h": 3, "depth": 32}, 0, 1, 0, 0, 0, 0),
        ({"w": 9, "h": 6, "depth": 4, "cmap": pal(16)}, 8, 1, 1, 0, 0, 0),
        ({"w": 9, "h": 6, "depth": 4, "cmap": pal(16)}, 4, 1, 0, 18, 3, 0),             # scaled
        ({"w": 9, "h": 6, "depth": 4, "cmap": pal(16)}, 4, 1, 0, 30, 30, 1),            # aspect kept
        ({"w": 40, "h": 3, "depth": 2, "cmap": pal(4), "short": True}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 6, "cmap": pal(64)}, 3, 1, 0, 0, 0, 0),              # more colours than pens
        ({"w": 8, "h": 3, "depth": 2, "cmap": pal(4), "nobmhd": True}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 2, "cmap": pal(4), "cmp": 2}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 9, "cmap": pal(4)}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 4, "cmap": pal(4), "pbm": True}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 2, "cmap": pal(4), "nobody": True}, 0, 1, 0, 0, 0, 0),
        ({"w": 8, "h": 3, "depth": 2, "cmap": pal(4), "notform": True}, 0, 1, 0, 0, 0, 0),
        ({"w": 32768, "h": 32768, "depth": 32, "body": b"\x00" * 8}, 0, 1, 0, 0, 0, 0),  # BODY size past 32 bits
    ]
    for i, (spec, sd, rm, dm, sw, sh, keep) in enumerate(ilbm_cases):
        data = write_ilbm(spec, rng)
        name = "i%02d.iff" % i
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
        script.append("ilbm %s %d %d %d %d %d %d" % (name, sd, rm, dm, sw, sh, keep))
        expected += expect_ilbm(name, data, sd, rm, dm, sw, sh, keep)

    for i, (w, h, depth, ncol, fmt) in enumerate([(5, 3, 4, 16, 3), (5, 3, 8, 7, 0), (4, 4, 8, 3, 1), (6, 2, 24, 0, 2),
                                                   (3, 3, 8, 256, 4), (4, 3, 24, 4, 3), (4, 2, 4, 4, 2)]):
        blob, p, store_lut, pix = pixw_case(rng, w, h, depth, ncol, fmt)
        name = "p%02d.px" % i
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(blob)
        script.append("pixw " + name)
        expected += expect_pixw(name, w, h, p, store_lut, pix)

    svx_cases = [
        ({"n": 50}, 0), ({"n": 51, "rate": 22050, "vol": 0x8000, "name": "beep"}, 1),
        ({"n": 40, "chan": 6}, 1), ({"n": 64, "cmp": 1}, 0), ({"n": 64, "cmp": 1, "chan": 6}, 1),
        ({"n": 30, "oneshot": 10, "repeat": 6}, 0), ({"n": 30, "oneshot": 100}, 0), ({"n": 20, "chan": 2}, 0),
        ({"n": 20, "chan": 4, "rate": 0}, 0), ({"n": 0}, 1), ({"n": 20, "rate": 60000}, 0),
        ({"n": 20, "novhdr": True}, 0), ({"n": 20, "cmp": 2}, 0), ({"n": 20, "nobody": True}, 0),
    ]
    for i, (spec, panel) in enumerate(svx_cases):
        data = write_8svx(spec, rng)
        name = "s%02d.8svx" % i
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)
        script.append("svx %s %d" % (name, panel))
        expected += expect_svx(name, data, panel)

    for args in [(1000, 0, 1, -1, 0), (1001, 1, 3, -1, 0), (300000, 0, 2, -1, 0), (300000, 1, 0, -1, 0),
                 (500, 0, 1, 0, 0), (500, 0, 1, 0x10000, 0), (500, 0, 1, 0x8000, 0), (800, 0, 1, -1, 1),
                 (800, 0, 1, -1, 2), (800, 0, 1, -1, 3), (0, 0, 1, -1, 0)]:
        script.append("snd %d %d %d %d %d" % args)
        expected += expect_snd(*args)

    script.append("sndvh 900")
    expected += expect_sndvh(900)

    with open(os.path.join(outdir, "script.txt"), "w") as f:
        f.write("\n".join(script) + "\n")
    with open(os.path.join(outdir, "expected.txt"), "w") as f:
        f.write("\n".join(expected) + "\nend\n")


if __name__ == "__main__":
    main()
