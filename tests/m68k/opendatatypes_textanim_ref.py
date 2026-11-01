#!/usr/bin/env python3
"""OpenDatatypes text, ascii, animation and anim: the test's script and the
log the cores must print for it, from python's own models.

    opendatatypes_textanim_ref.py <dir> <script> <expected> [seed]

Writes generated IFF ANIM files into <dir> (no copyrighted media: every
picture is random bits from the seed), the script the qemu test runs
(tests/m68k/opendatatypes_textanim_test.c) and the log it must print.

The models here are written from the specifications the cores are written
from (the IFF ANIM and ILBM documents, the text and animation class
autodocs, and the decisions each core's README lists), in python, apart
from the C. The ANIMs are made by an encoder from known frames, and the
decoder model must give back those frames before its answers are used.
"""
import os
import random
import struct
import sys

# --------------------------------------------------------------------------
# IFF ANIM: writing
# --------------------------------------------------------------------------

def bpr_of(w):
    return ((w + 15) >> 4) * 2


def chunk(cid, data):
    data = bytes(data)
    out = cid + struct.pack(">I", len(data)) + data
    if len(data) & 1:
        out += b"\0"
    return out


def form(ftype, chunks):
    body = ftype + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def bmhd(w, h, depth, masking, comp):
    return struct.pack(">HHhhBBBBHBBhh", w, h, 0, 0, depth, masking, comp, 0, 0, 10, 11, w, h)


def anhd(op, w, h, reltime, interleave, flags, short=False):
    d = struct.pack(">BBHHhhIIBBI", op, 0, w, h, 0, 0, 0, reltime, interleave, 0, flags) + b"\0" * 16
    return d[:20] if short else d


def byterun1(data, rnd):
    """ByteRun1 of a byte string, with runs, literals and the odd -128 no-op"""
    out = bytearray()
    i = 0
    while i < len(data):
        if rnd.random() < 0.05:
            out.append(0x80)                       # -128: nothing
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3 or (run == 2 and rnd.random() < 0.5):
            out.append((257 - run) & 0xff)
            out.append(data[i])
            i += run
        else:
            n = min(len(data) - i, rnd.randint(1, 128))
            out.append(n - 1)
            out += data[i:i + n]
            i += n
    return bytes(out)


def body_bytes(planes, w, h, depth, masking, comp, rnd, stream=False):
    bpr = bpr_of(w)
    raw = bytearray()
    rows = []
    for y in range(h):
        row = bytearray()
        for p in range(depth):
            row += planes[p][y * bpr:(y + 1) * bpr]
        if masking == 1:
            row += bytes(rnd.getrandbits(8) for _ in range(bpr))
        rows.append(bytes(row))
        raw += row
    if comp == 0:
        return bytes(raw)
    if stream:
        return byterun1(bytes(raw), rnd)       # runs across row ends
    return b"".join(byterun1(r, rnd) for r in rows)


def col_ops_bytes(vals, maxrun, rnd):
    """Ops for one column (op 5/7 style) from vals[row] = None (keep) or a
    value. Returns [(kind, n, values)]"""
    ops = []
    y = 0
    rows = len(vals)
    while y < rows:
        if vals[y] is None:
            n = 0
            while y + n < rows and vals[y + n] is None and n < 127:
                n += 1
            if y + n < rows:                       # a skip at the end may go
                ops.append(("skip", n, []))
            y += n
            continue
        n = 0
        while y + n < rows and vals[y + n] is not None and vals[y + n] == vals[y] and n < maxrun:
            n += 1
        if n >= 3 or (n >= 1 and rnd.random() < 0.2):
            ops.append(("same", n, [vals[y]]))
            y += n
            continue
        n = 0
        while y + n < rows and vals[y + n] is not None and n < 127:
            n += 1
            if rnd.random() < 0.2:
                break
        ops.append(("uniq", n, vals[y:y + n]))
        y += n
    return ops


def changed_vals(base, new, xor, off, size, bpr, rows, rnd, keep_same=0.1):
    vals = []
    for y in range(rows):
        a = base[y * bpr + off:y * bpr + off + size]
        b = new[y * bpr + off:y * bpr + off + size]
        if a == b and rnd.random() > keep_same:
            vals.append(None)
        elif xor:
            vals.append(bytes(x ^ z for x, z in zip(a, b)))
        else:
            vals.append(bytes(b))
    return vals


def delta5(base, new, w, h, depth, xor, rnd):
    bpr = bpr_of(w)
    head = [0] * 16
    data = bytearray()
    for p in range(depth):
        if base[p] == new[p] and rnd.random() < 0.7:
            continue
        head[p] = 64 + len(data)
        for col in range(bpr):
            vals = changed_vals(base[p], new[p], xor, col, 1, bpr, h, rnd)
            ops = col_ops_bytes(vals, 255, rnd)
            data.append(len(ops))
            for kind, n, v in ops:
                if kind == "skip":
                    data.append(n)
                elif kind == "same":
                    data += bytes([0, n]) + v[0]
                else:
                    data.append(0x80 | n)
                    data += b"".join(v)
    return struct.pack(">16I", *head) + bytes(data)


def unit_cols(bpr, size):
    cols = [(c * size, size) for c in range(bpr // size)]
    if size == 4 and bpr & 2:
        cols.append(((bpr // 4) * 4, 2))
    return cols


def delta7(base, new, w, h, depth, long_, xor, rnd):
    bpr = bpr_of(w)
    size = 4 if long_ else 2
    head = [0] * 16
    ops_all = bytearray()
    data_all = bytearray()
    planes = []
    for p in range(depth):
        if base[p] == new[p] and rnd.random() < 0.7:
            planes.append(None)
            continue
        opb = bytearray()
        dab = bytearray()
        for off, sz in unit_cols(bpr, size):
            vals = changed_vals(base[p], new[p], xor, off, sz, bpr, h, rnd)
            ops = col_ops_bytes(vals, 255, rnd)
            opb.append(len(ops))
            for kind, n, v in ops:
                if kind == "skip":
                    opb.append(n)
                elif kind == "same":
                    opb += bytes([0, n])
                    dab += v[0]
                else:
                    opb.append(0x80 | n)
                    dab += b"".join(v)
        planes.append((opb, dab))
    at = 64
    for p, pl in enumerate(planes):
        if pl is None:
            continue
        head[p] = at
        at += len(pl[0])
    # data lists after the op lists, each started on a word
    if at & 1:
        at += 1
    dat = at
    for p, pl in enumerate(planes):
        if pl is None:
            continue
        head[8 + p] = dat
        dat += len(pl[1])
    for pl in planes:
        if pl is not None:
            ops_all += pl[0]
    if len(ops_all) & 1:
        ops_all.append(0)
    for pl in planes:
        if pl is not None:
            data_all += pl[1]
    return struct.pack(">16I", *head) + bytes(ops_all) + bytes(data_all)


def delta8(base, new, w, h, depth, long_, xor, rnd):
    bpr = bpr_of(w)
    size = 4 if long_ else 2
    head = [0] * 16
    data = bytearray()
    for p in range(depth):
        if base[p] == new[p] and rnd.random() < 0.7:
            continue
        head[p] = 64 + len(data)
        for off, sz in unit_cols(bpr, size):
            fmt = ">I" if sz == 4 else ">H"
            vals = changed_vals(base[p], new[p], xor, off, sz, bpr, h, rnd)
            ops = col_ops_bytes(vals, 0xffff, rnd)
            data += struct.pack(fmt, len(ops))
            for kind, n, v in ops:
                if kind == "skip":
                    data += struct.pack(fmt, n)
                elif kind == "same":
                    data += struct.pack(fmt, 0) + struct.pack(fmt, n) + v[0]
                else:
                    data += struct.pack(fmt, (0x80000000 if sz == 4 else 0x8000) | n) + b"".join(v)
    return struct.pack(">16I", *head) + bytes(data)


def rand_planes(w, h, depth, rnd):
    bpr = bpr_of(w)
    out = []
    for p in range(depth):
        pl = bytearray(rnd.getrandbits(8) for _ in range(bpr * h))
        # the bits past the width are left as random: deltas carry them too
        out.append(pl)
    return out


def mutate(planes, rnd, amount):
    out = [bytearray(p) for p in planes]
    for p in out:
        for _ in range(int(len(p) * amount)):
            i = rnd.randrange(len(p))
            p[i] = rnd.getrandbits(8)
        if rnd.random() < 0.3:                      # a run of one value down a column
            i = rnd.randrange(len(p))
            v = rnd.getrandbits(8)
            for k in range(i, len(p), 7):
                p[k] = v
    return out


def make_anim(rnd, spec):
    """spec: dict(w, h, depth, frames, op, long, xor, interleave, comp,
    masking, cmaps, camg, dpan, reltime, body_at, stream, extra) -> (bytes,
    ground-truth frames or None)"""
    w, h, depth = spec["w"], spec["h"], spec["depth"]
    frames = [rand_planes(w, h, depth, rnd)]
    for k in range(1, spec["frames"]):
        frames.append(mutate(frames[k - 1], rnd, rnd.choice([0.0, 0.05, 0.3])))
    ncol = 1 << min(depth, 5)
    forms = []
    first = [chunk(b"BMHD", bmhd(w, h, depth, spec.get("masking", 0), spec.get("comp", 1)))]
    if spec.get("camg") is not None:
        first.append(chunk(b"CAMG", struct.pack(">I", spec["camg"])))
    first.append(chunk(b"CMAP", bytes(rnd.getrandbits(8) for _ in range(3 * ncol))))
    if spec.get("dpan"):
        first.append(chunk(b"DPAN", struct.pack(">HHI", 4, spec["dpan"], 0)))
    first.append(chunk(b"BODY", body_bytes(frames[0], w, h, depth, spec.get("masking", 0), spec.get("comp", 1), rnd,
                                           spec.get("stream", False))))
    forms.append(form(b"ILBM", first))
    for k in range(1, spec["frames"]):
        il = spec.get("interleave", 0)
        base = frames[k - 1] if (k == 1 or il == 1) else frames[k - 2]
        op = spec["op"]
        cks = []
        if k in spec.get("body_at", ()):
            cks.append(chunk(b"ANHD", anhd(0, w, h, spec.get("reltime", 6), il, 0)))
            cks.append(chunk(b"BODY", body_bytes(frames[k], w, h, depth, spec.get("masking", 0), spec.get("comp", 1), rnd)))
        else:
            flags = (1 if spec.get("long") else 0) | (2 if spec.get("xor") else 0)
            if op == 5:
                d = delta5(base, frames[k], w, h, depth, spec.get("xor"), rnd)
            elif op == 7:
                d = delta7(base, frames[k], w, h, depth, spec.get("long"), spec.get("xor"), rnd)
            elif op == 8:
                d = delta8(base, frames[k], w, h, depth, spec.get("long"), spec.get("xor"), rnd)
            else:
                d = b"\0" * 64
            cks.append(chunk(b"ANHD", anhd(op, w, h, spec.get("reltime", 6) + (k % 3 if spec.get("vary") else 0),
                                           il, flags, short=spec.get("short_anhd", False))))
            if k in spec.get("cmaps", ()):
                cks.append(chunk(b"CMAP", bytes(rnd.getrandbits(8) for _ in range(3 * ncol))))
            if spec.get("cut") and k == spec["cut"]:
                d = d[:max(64, len(d) * 2 // 3)]
            if k not in spec.get("nodlta", ()):
                cks.append(chunk(b"DLTA", d))
        forms.append(form(b"ILBM", cks))
    data = form(b"ANIM", forms)
    if spec.get("trunc"):
        data = data[:spec["trunc"]]
    return data, frames


# --------------------------------------------------------------------------
# IFF ANIM: reading, the model
# --------------------------------------------------------------------------

ERR_NOTANIM, ERR_SHORT, ERR_INVALID, ERR_COMP, ERR_MEMORY = 2000, 2007, 2008, 2006, 103


class Short(Exception):
    pass


def be32(d, i):
    return struct.unpack(">I", d[i:i + 4])[0]


def be16(d, i):
    return struct.unpack(">H", d[i:i + 2])[0]


def scan(d):
    if len(d) < 12 or d[0:4] != b"FORM" or d[8:12] != b"ANIM":
        return ERR_NOTANIM, None
    end = 8 + be32(d, 4)
    if end > len(d) or end < 12:
        end = len(d)
    info = dict(frames=[], camg=None, dpan=0, bmhd=None)
    at = 12
    cmap = None
    while at + 8 <= end:
        cid, size = d[at:at + 4], be32(d, at + 4)
        nxt = at + 8 + size
        if nxt > end:
            nxt = end
        elif nxt + (size & 1) <= end:
            nxt += size & 1
        if cid == b"FORM" and at + 12 <= nxt and d[at + 8:at + 12] == b"ILBM":
            c = at + 12
            f = dict(body=None, dlta=None, hd=None, cmap=None)
            while c + 8 <= nxt:
                ccid, cs = d[c:c + 4], be32(d, c + 4)
                cn = c + 8 + cs
                if cn > nxt:
                    cn = nxt
                    cs = nxt - c - 8
                elif cn + (cs & 1) <= nxt:
                    cn += cs & 1
                p = c + 8
                if ccid == b"BMHD" and info["bmhd"] is None and cs >= 20:
                    info["bmhd"] = struct.unpack(">HHhhBBBBHBBhh", d[p:p + 20])
                elif ccid == b"CAMG" and not info["frames"] and cs >= 4:
                    info["camg"] = be32(d, p)
                elif ccid == b"CMAP":
                    cmap = d[p:p + cs]
                elif ccid == b"BODY":
                    f["body"] = d[p:p + cs]
                elif ccid == b"DLTA":
                    f["dlta"] = d[p:p + cs]
                elif ccid == b"ANHD":
                    a = (d[p:p + cs] + b"\0" * 40)[:40]
                    op, mask, aw, ah, ax, ay, ab, rel, il, pad, fl = struct.unpack(">BBHHhhIIBBI", a[:24])
                    f["hd"] = dict(op=op, rel=rel, il=il, flags=fl)
                elif ccid == b"DPAN" and not info["frames"] and cs >= 4:
                    info["dpan"] = be16(d, p + 2)
                c = cn
            f["cmap"] = cmap
            info["frames"].append(f)
        at = nxt
    if info["bmhd"] is None or not info["frames"]:
        return ERR_INVALID, None
    w, h, x, y, npl = info["bmhd"][:5]
    if npl == 0 or npl > 8 or w == 0 or h == 0:
        return ERR_INVALID, None
    if info["frames"][0]["body"] is None:
        return ERR_INVALID, None
    rel = 0
    for f in info["frames"][1:]:
        if f["hd"] is not None:
            rel = f["hd"]["rel"]
            break
    info["fps"] = max(1, (60 + rel // 2) // rel) if rel else 10
    return 0, info


def mode_id(info):
    w, h, _, _, npl = info["bmhd"][:5]
    if info["camg"] is None:
        m = 0
        if w >= 640:
            m |= 0x8000
        if h >= 400:
            m |= 4
        if npl == 6 and not m & 0x8000:
            m |= 0x80
        return m
    m = info["camg"]
    if m & 0xffff0000 and not m & 0x1000:
        m &= 0xffff
    if not m & 0xffff0000:
        m &= ~(0x4000 | 0x2000 | 0x100 | 0x2) & 0xffffffff
    return m


def colors(info, n):
    if n >= len(info["frames"]):
        n = len(info["frames"]) - 1
    cm = info["frames"][n]["cmap"]
    if cm is None:
        return b""
    count = min(len(cm) // 3, 256)
    rgb = bytearray(cm[:count * 3])
    if mode_id(info) & 0x80 and count == 32 and info["bmhd"][4] == 6:
        rgb += bytes(v >> 1 for v in rgb[:96])
    return bytes(rgb)


def duration(info, n):
    fr = info["frames"]
    rel = 0
    if n + 1 < len(fr) and fr[n + 1]["hd"] is not None:
        rel = fr[n + 1]["hd"]["rel"]
    elif n < len(fr) and fr[n]["hd"] is not None:
        rel = fr[n]["hd"]["rel"]
    if not rel:
        return 1
    return max(1, (rel * info["fps"] + 30) // 60)


def unbody(info, body, planes):
    """planes: list of bytearray, modified in place; returns 0 or ERR_SHORT"""
    w, h, _, _, npl, masking, comp = info["bmhd"][:7]
    bpr = bpr_of(w)
    if comp > 1:
        return ERR_COMP
    pl = npl + (1 if masking == 1 else 0)
    stream = iter(body)

    def nxt():
        try:
            return next(stream)
        except StopIteration:
            raise Short()
    run = 0
    rep = 0
    try:
        for y in range(h):
            for p in range(pl):
                for x in range(bpr):
                    if comp == 0:
                        b = nxt()
                    else:
                        while run == 0:
                            c = nxt()
                            c = c - 256 if c > 127 else c
                            if c >= 0:
                                run = c + 1
                            elif c != -128:
                                rep = nxt()
                                run = -(1 - c)
                        if run > 0:
                            b = nxt()
                            run -= 1
                        else:
                            b = rep
                            run += 1
                    if p < npl:
                        planes[p][y * bpr + x] = b
    except Short:
        return ERR_SHORT
    return 0


def apply_unit(plane, at, val, xor):
    for i, v in enumerate(val):
        plane[at + i] = plane[at + i] ^ v if xor else v


def undelta(op, flags, bpr, rows, depth, d, planes):
    L = len(d)
    xor = bool(flags & 2)
    if op not in (5, 7, 8):
        return ERR_COMP
    if L < 64:
        return ERR_SHORT
    size = 4 if flags & 1 else 2
    cols = unit_cols(bpr, size)
    for p in range(depth):
        pl = planes[p]
        if op == 5:
            at = be32(d, 4 * p)
            if not at:
                continue
            if at >= L:
                return ERR_SHORT
            for col in range(bpr):
                if at >= L:
                    return ERR_SHORT
                n_ops = d[at]
                at += 1
                row = 0
                for _ in range(n_ops):
                    if at >= L:
                        return ERR_SHORT
                    op_ = d[at]
                    at += 1
                    if op_ == 0:
                        if at + 2 > L:
                            return ERR_SHORT
                        n, v = d[at], d[at + 1]
                        at += 2
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + col, bytes([v]), xor)
                            row += 1
                    elif op_ & 0x80:
                        n = op_ & 0x7f
                        if at + n > L:
                            return ERR_SHORT
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + col, d[at:at + 1], xor)
                            at += 1
                            row += 1
                    else:
                        row += op_
        elif op == 7:
            opat, dat = be32(d, 4 * p), be32(d, 32 + 4 * p)
            if not opat:
                continue
            if opat >= L or dat > L:
                return ERR_SHORT
            for x, sz in cols:
                if opat >= L:
                    return ERR_SHORT
                n_ops = d[opat]
                opat += 1
                row = 0
                for _ in range(n_ops):
                    if opat >= L:
                        return ERR_SHORT
                    op_ = d[opat]
                    opat += 1
                    if op_ == 0:
                        if opat >= L or dat + sz > L:
                            return ERR_SHORT
                        n = d[opat]
                        opat += 1
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + x, d[dat:dat + sz], xor)
                            row += 1
                        dat += sz
                    elif op_ & 0x80:
                        n = op_ & 0x7f
                        if dat + n * sz > L:
                            return ERR_SHORT
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + x, d[dat:dat + sz], xor)
                            row += 1
                            dat += sz
                    else:
                        row += op_
        else:
            at = be32(d, 4 * p)
            if not at:
                continue
            if at >= L:
                return ERR_SHORT
            for x, sz in cols:
                hib = 0x80000000 if sz == 4 else 0x8000

                def unit(i):
                    return be32(d, i) if sz == 4 else be16(d, i)
                if at + sz > L:
                    return ERR_SHORT
                n_ops = unit(at)
                at += sz
                row = 0
                for _ in range(n_ops):
                    if at + sz > L:
                        return ERR_SHORT
                    op_ = unit(at)
                    at += sz
                    if op_ == 0:
                        if at + 2 * sz > L:
                            return ERR_SHORT
                        n = unit(at)
                        at += sz
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + x, d[at:at + sz], xor)
                            row += 1
                        at += sz
                    elif op_ & hib:
                        n = op_ & (hib - 1)
                        if n > (L - at) // sz:
                            return ERR_SHORT
                        for k in range(n):
                            if row < rows:
                                apply_unit(pl, row * bpr + x, d[at:at + sz], xor)
                            row += 1
                            at += sz
                    else:
                        row += op_
    return 0


class Decoder:
    """The model of anm_goto: frame k from k-1 (interleave 1, frame 1) or k-2"""

    def __init__(self, data, info):
        self.d, self.info = data, info
        w, h, _, _, npl = info["bmhd"][:5]
        self.bpr, self.rows, self.depth = bpr_of(w), h, npl
        self.size = self.bpr * h
        self.buf = [[bytearray(self.size) for _ in range(npl)] for _ in range(2)]
        self.cur = -1

    def next(self):
        k = self.cur + 1
        f = self.info["frames"][k]
        dst, prev = self.buf[k & 1], self.buf[(k - 1) & 1]
        self.cur = k
        hd = f["hd"]
        if k == 0 or (hd["op"] == 0 if hd is not None else f["body"] is not None):
            for p in range(self.depth):
                dst[p][:] = bytes(self.size) if k == 0 else prev[p]
            r = unbody(self.info, f["body"], dst) if f["body"] is not None else ERR_INVALID
        else:
            if k == 1 or hd["il"] == 1:
                for p in range(self.depth):
                    dst[p][:] = prev[p]
            if f["dlta"] is None:
                r = 0
            else:
                r = undelta(hd["op"], hd["flags"], self.bpr, self.rows, self.depth, f["dlta"], dst)
        if r == ERR_SHORT:
            return 0
        if r:
            for p in range(self.depth):
                dst[p][:] = bytes(self.size) if k == 0 else prev[p]
        return r

    def goto(self, n):
        if n >= len(self.info["frames"]):
            return ERR_INVALID
        if self.cur > n:
            self.cur = -1
        r = 0
        while self.cur < n:
            r2 = self.next()
            if r2 and not r:
                r = r2
        return r

    def pixels(self):
        w = self.info["bmhd"][0]
        pl = self.buf[self.cur & 1]
        out = []
        for y in range(self.rows):
            row = []
            for x in range(w):
                v = 0
                for p in range(self.depth):
                    if pl[p][y * self.bpr + (x >> 3)] & (0x80 >> (x & 7)):
                        v |= 1 << p
                row.append(v)
            out.append(row)
        return out


def frame_pixels(planes, w, h, depth):
    bpr = bpr_of(w)
    return [[sum(1 << p for p in range(depth) if planes[p][y * bpr + (x >> 3)] & (0x80 >> (x & 7)))
             for x in range(w)] for y in range(h)]


def expect_anim(path, data, order, truth):
    out = []
    r, info = scan(data)
    if r:
        out.append("scan err %d" % r)
        return out
    w, h, _, _, npl = info["bmhd"][:5]
    n = len(info["frames"])
    out.append("anim %d %d %d %d fps %d camg %08x dpan %d bpr %d" % (w, h, npl, n, info["fps"], mode_id(info),
                                                                     info["dpan"], bpr_of(w)))
    for i in range(n):
        c = colors(info, i)
        out.append("frame %d dur %d colors %d %s" % (i, duration(info, i), len(c) // 3, c.hex()))
    dec = Decoder(data, info)
    for f in order:
        r = dec.goto(f)
        out.append("goto %d r %d cur %d" % (f, r, dec.cur))
        if dec.cur >= 0:
            px = dec.pixels()
            if truth is not None:
                assert px == frame_pixels(truth[dec.cur], w, h, npl), (path, dec.cur)
            for row in px:
                out.append("row " + "".join("%02x" % v for v in row))
    out.append("allocs 0")
    return out


# --------------------------------------------------------------------------
# text.datatype: the model
# --------------------------------------------------------------------------

YS, BL, XS = 9, 7, 6
BOLD = 2


def cw(c, style):
    return 4 + c % 5 + (1 if style & BOLD else 0)


def tlen(style, s):
    return sum(cw(c, style) for c in s)


class Seg:
    def __init__(self, buf, s, n, row, x, w, flags, fg, bg, style, tab, brk):
        self.buf, self.s, self.n = buf, s, n
        self.row, self.x, self.w, self.flags = row, x, w, flags
        self.fg, self.bg, self.style, self.tab, self.brk = fg, bg, style, tab, brk

    @property
    def text(self):
        return self.buf[self.s:self.s + self.n]


def layout(buf, wrap):
    st = dict(row=0, x=0, s=0, n=0, tab=0, row_has=False, fg=1, bg=0, style=0, last=None)
    segs = []
    width = [0]

    def emit(brk):
        w = tlen(st["style"], buf[st["s"]:st["s"] + st["n"]]) if st["n"] else 0
        g = Seg(buf, st["s"], st["n"], st["row"], st["x"], w, 1 if brk else 0, st["fg"], st["bg"], st["style"],
                st["tab"], brk)
        segs.append(g)
        width[0] = max(width[0], st["x"] + w)
        st["x"] += w
        st["s"] += st["n"]
        st["n"] = 0
        st["tab"] = 0
        st["row_has"] = True
        st["last"] = g

    def end_row(brk, use):
        if use and (st["n"] or st["tab"]):
            emit(brk)
        elif st["row_has"]:
            st["last"].brk = brk
            st["last"].flags |= 1
        else:
            n, s = st["n"], st["s"]
            if not use:
                st["n"] = 0
            emit(brk)
            if not use:
                st["s"], st["n"] = s, n
        st["row"] += 1
        st["x"] = 0
        st["row_has"] = False

    def sgr(p):
        if p == 0:
            st["style"], st["fg"], st["bg"] = 0, 1, 0
        elif p == 1:
            st["style"] |= 2
        elif p == 3:
            st["style"] |= 4
        elif p == 4:
            st["style"] |= 1
        elif p == 22:
            st["style"] &= ~2
        elif p == 23:
            st["style"] &= ~4
        elif p == 24:
            st["style"] &= ~1
        elif 30 <= p <= 37:
            st["fg"] = p - 30
        elif p == 39:
            st["fg"] = 1
        elif 40 <= p <= 47:
            st["bg"] = p - 40
        elif p == 49:
            st["bg"] = 0

    def escape(i):
        j = i + 1
        if buf[i] == 0x1b:
            if j >= len(buf) or buf[j] != ord("["):
                return j
            j += 1
        params, p, have = [], 0, False
        while j < len(buf):
            c = buf[j]
            if 48 <= c <= 57:
                if p < 10000:
                    p = p * 10 + c - 48
                have = True
            elif c == ord(";"):
                if len(params) < 16:
                    params.append(p)
                p, have = 0, False
            elif 0x40 <= c <= 0x7e:
                if c == ord("m"):
                    if len(params) < 16:
                        params.append(p if (have or not params) else 0)
                    if st["n"]:
                        emit(0)
                    for q in params:
                        sgr(q)
                return j + 1
            elif c < 0x20 or c > 0x3f:
                return j
            j += 1
        return len(buf)

    tabw = 8 * cw(32, 0)
    i = 0
    while i < len(buf):
        c = buf[i]
        if c in (10, 13):
            end_row(1, True)
            i += 2 if (c == 13 and i + 1 < len(buf) and buf[i + 1] == 10) else 1
            st["s"] = i
            continue
        if c == 9:
            if st["n"] or st["tab"]:
                emit(0)
            st["x"] = (st["x"] // tabw + 1) * tabw
            st["tab"] = 1
            i += 1
            st["s"] = i
            continue
        if c in (0x1b, 0x9b):
            i = escape(i)
            if st["n"]:
                emit(0)
            st["s"] = i
            continue
        if wrap:
            w = tlen(st["style"], buf[st["s"]:st["s"] + st["n"] + 1])
            if st["x"] + w > wrap and (st["n"] or st["row_has"] or st["x"] > 0):
                sp = None
                for k in range(st["s"] + st["n"], st["s"] - 1, -1):
                    if buf[k] == 32:
                        sp = k
                        break
                if sp is not None:
                    st["n"] = sp - st["s"]
                    end_row(2, True)
                    st["s"] = sp + 1
                    if sp == i:
                        st["n"] = 0
                        i += 1
                        st["s"] = i
                    else:
                        st["n"] = i - st["s"]
                elif st["row_has"] or st["x"] > 0:
                    end_row(3, False)
                else:
                    end_row(3, True)
                    st["s"] = i
                continue
        st["n"] += 1
        i += 1
    if st["n"] or st["tab"]:
        emit(0)
    rows = st["row"] + (1 if st["row_has"] else 0)
    return dict(segs=segs, rows=rows, width=width[0])


def dump_layout(lay):
    out = ["layout rows %d width %d segs %d" % (lay["rows"], lay["width"], len(lay["segs"]))]
    for g in lay["segs"]:
        fg = g.fg - 256 if g.fg > 127 else g.fg
        out.append("seg %d %d %d %d %d %d %d %d %d %d %d %s" % (g.row, g.x & 0xffff, (g.row * YS) & 0xffff, g.w, YS,
                                                                g.flags, fg, g.bg, g.style, g.tab, g.brk,
                                                                g.text.hex()))
    return out


def places(g):
    return g.tab + g.n + (1 if g.brk else 0)


def place_at(lay, x, y):
    at = 0
    row = 0 if y < 0 else y // YS
    if row >= lay["rows"]:
        return sum(places(g) for g in lay["segs"])
    for g in lay["segs"]:
        if g.row < row:
            at += places(g)
            continue
        if g.row > row:
            return at
        if x < g.x:
            return at
        if x < g.x + g.w:
            dx = x - g.x
            k = 0
            while k < g.n and tlen(g.style, g.text[:k + 1]) <= dx:
                k += 1
            if k >= g.n:
                k = g.n - 1 if g.n else 0
            return at + g.tab + k
        at += places(g)
        if g.brk:
            return at - 1
    return at


def split(lay, g, k):
    idx = lay["segs"].index(g)
    w = tlen(g.style, g.text[:k]) if k else 0
    h = Seg(g.buf, g.s + k, g.n - k, g.row, g.x + w, 0, g.flags, g.fg, g.bg, g.style, 0, g.brk)
    h.w = tlen(h.style, h.text) if h.n else 0
    g.n = k
    g.w = w
    g.flags &= ~1
    g.brk = 0
    lay["segs"].insert(idx + 1, h)
    return h


def select(lay, x0, y0, x1, y1):
    a, b = place_at(lay, x0, y0), place_at(lay, x1, y1)
    if a > b:
        a, b = b, a
    at = 0
    count = 0
    for g in list(lay["segs"]):
        n = places(g)
        p0, p1, c0 = at, at + n - 1, at + g.tab
        at += n
        if not n or p1 < a or p0 > b:
            continue
        if a <= p0 and p1 <= b:
            g.flags |= 8
            count += 1
            continue
        ca, cb = max(a, p0), min(b, p1)
        mid = g
        if cb < p1:
            split(lay, g, cb + 1 - c0)
        if ca > c0:
            mid = split(lay, g, ca - c0)
        elif ca > p0:
            mid = split(lay, g, 0)
        mid.flags |= 8
        count += 1
    return count


def clear_selected(lay):
    for g in lay["segs"]:
        g.flags &= ~8


def copy_text(lay, all_):
    any_ = (not all_) and any(g.flags & 8 for g in lay["segs"])
    out = bytearray()
    for g in lay["segs"]:
        if any_ and not g.flags & 8:
            continue
        if g.tab:
            out.append(9)
        out += g.text
        if g.brk == 1:
            out.append(10)
        elif g.brk == 2:
            out.append(32)
    return bytes(out)


def ftxt(text):
    return chunk_form_ftxt(text)


def chunk_form_ftxt(text):
    chrs = b"CHRS" + struct.pack(">I", len(text)) + text + (b"\0" if len(text) & 1 else b"")
    return b"FORM" + struct.pack(">I", 4 + len(chrs)) + b"FTXT" + chrs


DELIMS = b"\t *-,<>()[];\""


def word_at(lay, x, y, size=16):
    if y < 0 or x < 0:
        return b""
    row = y // YS
    for g in lay["segs"]:
        if g.row != row or x < g.x or x >= g.x + g.w:
            continue
        t = g.text
        k = 0
        while k < g.n and tlen(g.style, t[:k + 1]) <= x - g.x:
            k += 1

        def delim(c):
            return c in DELIMS or c in (10, 13, 0)
        if k >= g.n or delim(t[k]):
            return b""
        a = k
        while a > 0 and not delim(t[a - 1]):
            a -= 1
        b = k + 1
        while b < g.n and not delim(t[b]):
            b += 1
        return bytes(t[a:b])[:size - 1]
    return b""


def render(lay, left, top, width, height, top_row, left_px, pens=(1, 0, 3, 2)):
    text, back, fill, filltext = pens
    out = []
    if width <= 0 or height <= 0:
        return out
    rows = (height + YS - 1) // YS
    out.append("clip %d %d %d %d" % (left, top, left + width - 1, top + height - 1))
    out.append("fill %d %d %d %d %d" % (back, left, top, left + width - 1, top + height - 1))
    for g in lay["segs"]:
        if g.row < top_row:
            continue
        if g.row >= top_row + rows:
            break
        sx = left + (g.x & 0xffff) - left_px
        sy = top + (g.row - top_row) * YS
        if sx >= left + width or sx + g.w <= left or not g.n:
            continue
        fg = g.fg
        if g.flags & 8:
            out.append("fill %d %d %d %d %d" % (fill, sx, sy, sx + g.w - 1, sy + YS - 1))
            fg = filltext
        elif g.bg != 0:
            out.append("fill %d %d %d %d %d" % (g.bg, sx, sy, sx + g.w - 1, sy + YS - 1))
        out.append("text %d %d %d %d %s" % (g.style, fg, sx, sy, g.text.hex()))
    out.append("unclip")
    return out


# --------------------------------------------------------------------------
# animation.datatype: the model
# --------------------------------------------------------------------------

class Play:
    def __init__(self, frames):
        self.frames, self.frame, self.state, self.left, self.inc, self.ticks, self.repeat = frames, 0, 0, 0, 1, 0, 1

    def start(self, frame, dur, ticks):
        if not self.frames:
            return
        self.frame = min(frame, self.frames - 1)
        self.state = 1
        self.left = dur or 1
        self.ticks = ticks

    def tick(self):
        if self.state != 1 or not self.frames:
            return False
        self.ticks += 1
        if self.left > 1:
            self.left -= 1
            return False
        nxt = self.frame + (self.inc or 1)
        if nxt >= self.frames:
            if not self.repeat:
                self.state = 0
                self.left = 0
                return False
            nxt %= self.frames
            self.ticks = nxt
        self.frame = nxt
        self.left = 1
        return True


def sound_offset(ticks, fps, period, sps, clock, length):
    rate = sps if sps else (clock // period if period else 0)
    if not fps or not rate:
        return 0
    off = (ticks // fps) * rate + ((ticks % fps) * rate) // fps
    return min(off, length)


def panel_layout(left, top, width, height, ys):
    h = max(ys + 6, 12)
    if h > height:
        h = height if height > 0 else 0
    p = dict(x=left, w=width, h=h, y=top + height - h, bw=h * 3 // 2)
    p["play_x"] = left + 1
    p["stop_x"] = p["play_x"] + p["bw"] + 1
    p["sx"] = p["stop_x"] + p["bw"] + 2
    sw = left + width - 1 - p["sx"]
    p["sw"] = max(sw, 0)
    p["kw"] = p["sw"] if h > p["sw"] else h
    return p


def panel_hit(p, x, y, frames):
    if y < p["y"] or y >= p["y"] + p["h"] or x < p["x"] or x >= p["x"] + p["w"]:
        return 0, 0
    if p["play_x"] <= x < p["play_x"] + p["bw"]:
        return 1, 0
    if p["stop_x"] <= x < p["stop_x"] + p["bw"]:
        return 2, 0
    if p["sw"] > 0 and frames and p["sx"] <= x < p["sx"] + p["sw"]:
        return 3, min((x - p["sx"]) * frames // p["sw"], frames - 1)
    return 0, 0


def knob_x(p, frame, frames):
    if frames < 2 or p["sw"] <= p["kw"]:
        return p["sx"]
    frame = min(frame, frames - 1)
    return p["sx"] + frame * (p["sw"] - p["kw"]) // (frames - 1)


def panel_draw(p, state, frame, frames, back, fill, text):
    out = []
    inset = 3 if p["h"] > 6 else 1
    if p["h"] <= 2 or p["w"] <= 0:
        return out
    out.append("fill %d %d %d %d %d" % (back, p["x"], p["y"], p["x"] + p["w"] - 1, p["y"] + p["h"] - 1))
    out.append("bevel %d %d %d %d %d" % (p["play_x"], p["y"] + 1, p["bw"], p["h"] - 2, 1 if state == 1 else 0))
    out.append("glyph %d %d %d %d %d %d" % (2 if state == 1 else 1, p["play_x"] + inset, p["y"] + inset,
                                            p["bw"] - 2 * inset, p["h"] - 2 * inset, text))
    out.append("bevel %d %d %d %d 0" % (p["stop_x"], p["y"] + 1, p["bw"], p["h"] - 2))
    out.append("glyph 3 %d %d %d %d %d" % (p["stop_x"] + inset, p["y"] + inset, p["bw"] - 2 * inset,
                                           p["h"] - 2 * inset, text))
    if p["sw"] > 0:
        kx = knob_x(p, frame, frames)
        out.append("bevel %d %d %d %d 1" % (p["sx"], p["y"] + 1, p["sw"], p["h"] - 2))
        if p["kw"] > 2 and p["h"] > 4:
            out.append("fill %d %d %d %d %d" % (fill, kx + 1, p["y"] + 3, kx + p["kw"] - 2, p["y"] + p["h"] - 4))
            out.append("bevel %d %d %d %d 0" % (kx, p["y"] + 2, p["kw"], p["h"] - 4))
    return out


def remap(w, h, sd, dd, table, planes):
    bpr = bpr_of(w)
    out = []
    for y in range(h):
        row = ""
        for x in range(w):
            pix = 0
            for p in range(sd):
                if planes[y * bpr * sd + p * bpr + (x >> 3)] & (0x80 >> (x & 7)):
                    pix |= 1 << p
            v = table[pix] & ((1 << dd) - 1)
            row += "%02x" % v
        out.append(row)
    return "aremap " + " ".join(out)


# --------------------------------------------------------------------------
# The class scenarios
# --------------------------------------------------------------------------

def tag_line(tags):
    return "notify" + "".join(" %x=%d" % (t, v) for t, v in tags)


DTA = dict(VisibleVert=12, TotalVert=13, VisibleHoriz=16, TotalHoriz=17, Sync=29, NominalVert=124, NominalHoriz=125)


def text_layout_notify(lay, w, h):
    cols = (lay["width"] + XS - 1) // XS
    tags = [(DTA["VisibleVert"], h // YS), (DTA["TotalVert"], lay["rows"]), (DTA["NominalVert"], lay["rows"] * YS),
            (DTA["VisibleHoriz"], w // XS), (DTA["TotalHoriz"], cols), (DTA["NominalHoriz"], cols * XS),
            (DTA["Sync"], 1)]
    return tag_line(tags)


def expect_tclass(wrap, text):
    out = ["tclass new 1"]
    out.append("buflen %d wrap %d" % (len(text), 1 if wrap else 0))
    box = (5, 7, 100, 40)
    lay = layout(text, box[2] if wrap else 0)
    out.append(text_layout_notify(lay, box[2], box[3]))
    out.append("layout -> %d" % lay["rows"])
    cols = (lay["width"] + XS - 1) // XS
    out.append("si %d %d %d %d %d %d" % (box[3] // YS, lay["rows"], YS, box[2] // XS, cols, XS))
    pens = (12, 17, 15, 16)                 # 10 + TEXTPEN, BACKGROUNDPEN, FILLPEN, FILLTEXTPEN
    out += render(lay, box[0], box[1], box[2], box[3], 1, 0, pens)
    out.append("render -> 1")
    clear_selected(lay)
    n = select(lay, 7, 2, 30, 12)
    lay1 = lay
    out += render(lay1, box[0], box[1], box[2], box[3], 1, 0, pens)
    out.append("select -> %d" % n)
    t = copy_text(lay, False)
    out.append("clip open 0 1")
    out.append("clip push FTXT FORM -1")
    out.append("clip push FTXT CHRS %d" % len(t))
    out.append("clip write " + t.hex())
    out.append("clip pop")
    out.append("clip pop")
    out.append("clip close")
    out.append("copy -> 1")
    if text:
        out.append("write 99 " + text.hex())
    out.append("write raw -> 1")
    out.append("write 99 " + chunk_form_ftxt(copy_text(lay, True)).hex())
    out.append("write iff -> 1")
    word = word_at(lay, 3, 0, 128)
    out.append("notify %x='%s'" % (303, word.decode("latin-1")))
    out.append("wordselect '%s'" % word.decode("latin-1"))
    out.append("set -> 1 buflen 3")
    lay2 = layout(b"x y", box[2] if wrap else 0)
    out.append(text_layout_notify(lay2, box[2], box[3]))
    out.append("layout -> %d" % lay2["rows"])
    out.append("allocs 0 sem 0")
    return out


def expect_aclass(kind, data):
    out = []
    text = data
    ok = True
    if kind == 1:
        text = data                         # read from the start, whatever the position was
    elif kind == 3:
        # FTXT: every CHRS chunk
        t = bytearray()
        at = 0
        while at + 8 <= len(data):
            cid, size = data[at:at + 4], be32(data, at + 4)
            if cid in (b"FORM", b"CAT ", b"LIST"):
                at += 12
                continue
            if at + 8 + size > len(data):
                break
            if cid == b"CHRS":
                t += data[at + 8:at + 8 + size]
            at += 8 + size + (size & 1)
        text = bytes(t)
    elif kind == 4:
        text = b""
    out.append("aclass new %d ioerr 0" % (1 if ok else 0))
    out.append("buf %d %s" % (len(text), text.hex()))
    lay = layout(text, 0)
    out.append(text_layout_notify(lay, 60, 30))
    out.append("layout -> %d" % lay["rows"])
    out.append("allocs 0 sem 0")
    return out


def bitmap_sum(pixels, sx, sy, w, h):
    s = 0
    for y in range(h):
        for x in range(w):
            s = (s * 31 + pixels[y + sy][x + sx]) & 0xffffffff
    return s


def pen_of(r, g, b):
    return (r * 3 + g * 5 + b * 7) % 16


def expect_nclass(data, ops, truth):
    """anim.datatype under animation.datatype: the log of a run of ops"""
    out = []
    r, info = scan(data)
    w, h, _, _, depth = info["bmhd"][:5]
    n = len(info["frames"])
    col = colors(info, 0)
    ncol = len(col) // 3
    out.append("allocbm %d %d %d" % (w, h, depth))     # the key frame
    out.append("nclass new 1 ioerr 0")
    out.append("attrs %d %d %d %d %d %08x %d" % (w, h, depth, n, info["fps"], mode_id(info), ncol))
    out.append("cr %s %08x" % (col.hex(), (col[1] * 0x01010101) if ncol else 0))
    dec = Decoder(data, info)
    pix = {}
    for f in range(n):
        dec.goto(f)
        pix[f] = dec.pixels()
    remap_ok = not (mode_id(info) & 0x800)
    table = [pen_of(col[3 * i], col[3 * i + 1], col[3 * i + 2]) for i in range(ncol)]
    st = dict(preload=4, repeat=1, remap=True, panel=True, newformat=0, slots=[], sample=False, sound_on=False,
              have_gi=False, pens=False, screen=False)
    play = Play(n)
    fps = info["fps"]
    box = (2, 3, 120, 50)

    def window():
        k = st["preload"] or 1
        return min(k, 64, n)

    def wanted(frame, k, f):
        if f >= frame:
            d = f - frame
        elif play.repeat:
            d = f + n - frame
        else:
            return False
        return d < k

    def can_remap():
        return st["remap"] and st["screen"] and ncol and remap_ok and depth <= 8

    kept = []                           # each slot's remapped bitmap, kept from frame to frame

    def load(f, i):
        # first ask with ADTM_LOADNEWFORMATFRAME (anim knows it)
        out.append("allocbm %d %d %d" % (w, h, depth))
        out.append("load 7 %d dur %d -> 1" % (f, duration(info, f)))
        st["newformat"] = 1
        s = dict(frame=f, dur=duration(info, f), remapped=False)
        while len(kept) <= i:
            kept.append(False)
        if can_remap():
            st["pens"] = True
            # the slot's bitmap from an earlier frame is drawn over, after a WaitBlit
            out.append("waitblit" if kept[i] else "allocbm %d %d 4" % (w, h))
            kept[i] = True
            s["remapped"] = True
        elif kept[i]:
            out.append("freebm")
            kept[i] = False
        return s

    def unload(s):
        out.append("freebm")
        out.append("unload 8 %d" % s["frame"])

    def flush():
        # every frame back to anim, and each slot's remapped bitmap freed
        for i in range(max(len(st["slots"]), len(kept))):
            sl = st["slots"][i] if i < len(st["slots"]) else None
            if sl is not None:
                unload(sl)
                st["slots"][i] = None
            if i < len(kept) and kept[i]:
                out.append("freebm")
                kept[i] = False

    def cache(frame):
        k = window()
        if not n or frame >= n:
            return None
        keep = []
        for s in st["slots"]:
            if s is not None and not wanted(frame, k, s["frame"]):
                unload(s)
                st["slots"][st["slots"].index(s)] = None
        shown = None
        for i in range(k):
            f = frame + i
            if f >= n:
                if not play.repeat:
                    break
                f -= n
            s = next((s for s in st["slots"] if s is not None and s["frame"] == f), None)
            if s is None:
                if None in st["slots"]:
                    j = st["slots"].index(None)
                else:
                    j = len(st["slots"])
                    st["slots"].append(None)
                s = load(f, j)
                st["slots"][j] = s
            if i == 0:
                shown = s
        return shown

    def draw():
        s = cache(play.frame)
        p = panel_layout(box[0], box[1], box[2], box[3], 8) if st["panel"] else None
        ph = p["h"] if p else 0
        pic = (box[0], box[1], box[2], box[3] - ph)
        ww, hh = min(w, pic[2]), min(h, pic[3])
        frame_px = pix[s["frame"]] if s else pix[0]
        if s and s["remapped"]:
            frame_px = [[table[v] if v < len(table) else 0 for v in row] for row in frame_px]
            dep = 4
        else:
            dep = depth
        frame_px = [[v & ((1 << dep) - 1) for v in row] for row in frame_px]
        if ww and hh:
            out.append("blit d%d 0 0 %d %d %d %d %08x" % (dep, pic[0], pic[1], ww, hh, bitmap_sum(frame_px, 0, 0, ww, hh)))
        if ww < pic[2] and pic[3] > 0:
            out.append("fill 17 %d %d %d %d" % (pic[0] + ww, pic[1], pic[0] + pic[2] - 1, pic[1] + pic[3] - 1))
        if hh < pic[3] and ww > 0:
            out.append("fill 17 %d %d %d %d" % (pic[0], pic[1] + hh, pic[0] + ww - 1, pic[1] + pic[3] - 1))
        if p:
            out.extend(panel_draw(p, play.state, play.frame, n, 17, 15, 12))

    def redraw():
        if st["have_gi"]:
            draw()

    def sound_off():
        if st["sound_on"]:
            out.append("soundstop")
        st["sound_on"] = False

    def sound_from():
        if not st["sample"]:
            return
        off = sound_offset(play.ticks, fps, 1000, 0, 3546895, 4000)
        if off >= 4000:
            return
        out.append("sound %d %d 1000 64 1" % (off, 4000 - off))
        st["sound_on"] = True

    def tell():
        out.append("notify %x=%d" % (605, play.frame))

    player = [False]
    for op in ops:
        if op == "layout":
            st["have_gi"] = True
            st["screen"] = True
            p = panel_layout(box[0], box[1], box[2], box[3], 8) if st["panel"] else None
            ph = p["h"] if p else 0
            out.append(tag_line([(12, box[3] - ph), (13, h), (124, h + ph), (16, box[2]), (17, w), (125, w), (29, 1)]))
            out.append("layout -> %d" % h)
        elif op == "render":
            st["have_gi"] = True
            st["screen"] = True
            draw()
            out.append("render -> 1")
        elif op == "tick":
            out.append("tick")
            changed = play.tick()
            if changed:
                s = cache(play.frame)
                play.left = (s["dur"] if s else 1) or 1
                if play.frame == 0 and play.ticks == 0:
                    sound_off()
                    sound_from()
            elif play.state != 1:
                sound_off()
            if changed:
                tell()
                redraw()
        elif op.startswith("start"):
            f = min(int(op[5:]), n - 1)
            was = play.frame
            sound_off()
            s = cache(f)
            play.start(f, s["dur"] if s else 1, f)
            sound_from()
            if not player[0]:
                out.append("player %d" % fps)
                player[0] = True
            # a program's play: the frame told when it moved, and redrawn
            if play.frame != was:
                tell()
            redraw()
            out.append("%s -> 1" % op)
        elif op.startswith("locate"):
            f = min(int(op[6:]), n - 1)
            play.frame = f
            play.ticks = f
            if play.state == 1:
                s = cache(f)
                sound_off()
                play.left = (s["dur"] if s else 1) or 1
                sound_from()
            tell()
            redraw()
            out.append("%s -> 1" % op)
        elif op in ("pause", "stop"):
            was = play.frame
            if player[0]:
                out.append("playerstop")
                player[0] = False
            sound_off()
            play.state = 2 if op == "pause" else 0
            if op == "stop":
                play.frame = 0
            if play.frame != was:
                tell()
            redraw()
            out.append("%s -> 1" % op)
        elif op.startswith("preload"):
            st["preload"] = int(op[7:]) or 1
            out.append("%s -> 0" % op)
        elif op.startswith("repeat"):
            play.repeat = int(op[6:]) != 0
            out.append("%s -> 0" % op)
        elif op == "sample":
            st["sample"] = True
            out.append("sample -> 0")
        elif op == "noremap":
            if st["remap"]:
                # remapped frames go: the pens back, the cache loaded again
                if st["pens"]:
                    for i in range(ncol):
                        out.append("releasepen %d" % table[i])
                    st["pens"] = False
                flush()
            st["remap"] = False
            out.append("noremap -> 0")
        elif op == "nopanel":
            st["panel"] = False
            out.append("nopanel -> 1")
        elif op.startswith("click"):
            x, y = (int(v) for v in op[5:].split(","))
            p = panel_layout(box[0], box[1], box[2], box[3], 8)
            hit, f = panel_hit(p, x + box[0], y + box[1], n) if st["panel"] and st["have_gi"] else (0, 0)
            out.append("hittest %d,%d -> %d" % (x, y, 4 if hit else 0))
            if hit == 1:
                if play.state == 1:
                    if player[0]:
                        out.append("playerstop")
                        player[0] = False
                    sound_off()
                    play.state = 2
                else:
                    sound_off()
                    s = cache(play.frame)
                    play.start(play.frame, s["dur"] if s else 1, play.frame)
                    sound_from()
                    if not player[0]:
                        out.append("player %d" % fps)
                        player[0] = True
                redraw()
            elif hit == 2:
                if player[0]:
                    out.append("playerstop")
                    player[0] = False
                sound_off()
                play.state = 0
                play.frame = 0
                tell()
                redraw()
            elif hit == 3:
                play.frame = f
                play.ticks = f
                if play.state == 1:
                    s = cache(f)
                    sound_off()
                    play.left = (s["dur"] if s else 1) or 1
                    sound_from()
                tell()
                redraw()
            out.append("goactive -> %d" % (2 if hit else 0))
        elif op == "frame":
            out.append("frame %d" % play.frame)
    # dispose: anim's own first (the key frame), then animation.datatype's
    out.append("freebm")
    if player[0]:
        out.append("playerstop")
    sound_off()
    flush()
    if st["pens"]:
        for i in range(ncol):
            out.append("releasepen %d" % table[i])
    out.append("allocs 0 sem 0")
    return out


# --------------------------------------------------------------------------
# The script
# --------------------------------------------------------------------------

def main():
    outdir, script_path, expect_path = sys.argv[1], sys.argv[2], sys.argv[3]
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 2026
    rnd = random.Random(seed)
    script, expect = [], []

    def cmd(line, lines):
        script.append(line)
        expect.append("> " + line.split(" ")[0])
        expect.extend(lines)

    # ---- ANIM files ----
    specs = []
    for op in (5, 7, 8):
        for long_ in ((False, True) if op != 5 else (False,)):
            for xor in (False, True):
                for il in (0, 1, 2):
                    specs.append(dict(op=op, long=long_, xor=xor, interleave=il))
    rnd.shuffle(specs)
    files = 0
    for i, sp in enumerate(specs):
        sp.update(w=rnd.choice([8, 16, 17, 31, 32, 33, 48, 63, 80]), h=rnd.randint(1, 13),
                  depth=rnd.randint(1, 8), frames=rnd.randint(2, 7), comp=rnd.choice([0, 1]),
                  masking=rnd.choice([0, 0, 1, 2]), reltime=rnd.choice([0, 1, 2, 3, 4, 5, 6, 10]),
                  camg=rnd.choice([None, 0x8004, 0x00000800, 0x4002, 0x00011000, 0x12340000 | 0x8000]),
                  dpan=rnd.choice([0, 7]), vary=rnd.random() < 0.3, stream=rnd.random() < 0.2,
                  cmaps=(2,) if rnd.random() < 0.3 else ())
        if rnd.random() < 0.2 and sp["frames"] > 3:
            sp["body_at"] = (rnd.randint(2, sp["frames"] - 1),)
        data, truth = make_anim(rnd, sp)
        path = os.path.join(outdir, "a%02d.anim" % i)
        open(path, "wb").write(data)
        nfr = sp["frames"]
        order = list(range(nfr)) + [rnd.randrange(nfr) for _ in range(4)] + [0, nfr - 1]
        cmd("ANIM %s %s" % (path, " ".join(map(str, order))), expect_anim(path, data, order, truth))
        files += 1
    # the corners: EHB colours, short ANHD, a cut DLTA, no DLTA, an unknown op,
    # a short file, not an ANIM, depth 0, compression 2, a short BODY
    corners = [
        dict(op=5, w=16, h=4, depth=6, frames=3, camg=0x80, comp=1),
        dict(op=5, w=16, h=4, depth=6, frames=3, camg=None, comp=1),
        dict(op=8, long=True, w=48, h=6, depth=3, frames=4, short_anhd=True),
        dict(op=7, long=True, w=40, h=9, depth=2, frames=4, cut=2),
        dict(op=8, w=24, h=5, depth=2, frames=4, cut=3),
        dict(op=5, w=16, h=5, depth=2, frames=4, nodlta=(2,)),
        dict(op=4, w=16, h=3, depth=1, frames=3),
        dict(op=5, w=640, h=2, depth=1, frames=2, camg=None),
        dict(op=5, w=16, h=400, depth=1, frames=2, camg=None),
    ]
    for j, sp in enumerate(corners):
        data, truth = make_anim(rnd, sp)
        exact = not (sp.get("cut") or sp.get("nodlta") or sp.get("short_anhd") or sp["op"] == 4)
        path = os.path.join(outdir, "c%02d.anim" % j)
        open(path, "wb").write(data)
        order = list(range(sp["frames"])) + [1, sp["frames"] - 1]
        cmd("ANIM %s %s" % (path, " ".join(map(str, order))), expect_anim(path, data, order, truth if exact else None))
    raw = [
        b"FORM\0\0\0\x04ILBM",
        b"FORM\0\0\0\x08ANIM",
        form(b"ANIM", [form(b"ILBM", [chunk(b"BMHD", bmhd(16, 2, 0, 0, 0)), chunk(b"BODY", b"")])]),
        form(b"ANIM", [form(b"ILBM", [chunk(b"BMHD", bmhd(16, 2, 1, 0, 2)), chunk(b"BODY", b"\0" * 4)])]),
        form(b"ANIM", [form(b"ILBM", [chunk(b"BMHD", bmhd(16, 3, 2, 0, 1)), chunk(b"BODY", b"\x05abcdef\xfe\x11")])]),
        form(b"ANIM", [form(b"ILBM", [chunk(b"BMHD", bmhd(16, 2, 1, 0, 0))])]),
        make_anim(rnd, dict(op=5, w=32, h=8, depth=3, frames=5))[0][:300],
    ]
    for j, data in enumerate(raw):
        path = os.path.join(outdir, "r%02d.anim" % j)
        open(path, "wb").write(data)
        cmd("ANIM %s 0 1" % path, expect_anim(path, data, [0, 1], None))

    # ---- text layout ----
    texts = [b"", b"\n", b"abc", b"abc\n", b"one two three four five six seven eight nine ten",
             b"a\tb\t\tc\r\nd\re\n\nf", b"\x1b[1mbold\x1b[0m plain \x1b[31;42mred on green\x1b[39;49m back",
             b"\x9b4munder\x9b24m\x1b[2Jcls\x1bXq\x1b[12", b"averyveryveryverylongwordwithoutanyspaces end",
             b"\ttabbed words that wrap around\tand more", b"  lead  spaces   here  ", b"x" * 3 + b" " * 40 + b"y"]
    for _ in range(6):
        alphabet = b"abcdefghij     \t\n\x1b[1;3m"
        texts.append(bytes(rnd.choice(alphabet) for _ in range(rnd.randint(1, 160))))
    for t in texts:
        for wrap in (0, 37, 90):
            lay = layout(t, wrap)
            cmd("TLAY %d %s" % (wrap, t.hex()), ["tlay r 0"] + dump_layout(lay))
            ys = lay["rows"] * YS + 5
            for _ in range(2):
                x0, y0 = rnd.randint(-3, 120), rnd.randint(-3, ys)
                x1, y1 = rnd.randint(-3, 120), rnd.randint(-3, ys)
                clear_selected(lay)
                n = select(lay, x0, y0, x1, y1)
                cmd("TSEL %d %d %d %d" % (x0, y0, x1, y1), ["tsel %d" % n] + dump_layout(lay))
                for all_ in (0, 1):
                    c = copy_text(lay, all_)
                    cmd("TCOPY %d" % all_, ["tcopy " + c.hex(), "ftxt " + chunk_form_ftxt(c).hex()])
            for _ in range(3):
                x, y = rnd.randint(-2, 100), rnd.randint(-2, ys)
                wd = word_at(lay, x, y)
                cmd("TWORD %d %d" % (x, y), ["tword %d %s" % (len(wd), wd.hex())])
            args = (rnd.randint(0, 20), rnd.randint(0, 20), rnd.randint(1, 120), rnd.randint(1, 50),
                    rnd.randint(0, 2), rnd.randint(0, 30))
            cmd("TRENDER %d %d %d %d %d %d" % args, render(lay, *args) + ["trender done"])

    # ---- animation, pure ----
    for _ in range(25):
        frames, inc, rep = rnd.randint(1, 9), rnd.randint(1, 3), rnd.randint(0, 1)
        st, ticks = rnd.randrange(frames), rnd.randint(0, 30)
        durs = [rnd.randint(0, 3) for _ in range(rnd.randint(1, 5))]
        p = Play(frames)
        p.inc, p.repeat = inc, rep
        p.start(st, durs[st % len(durs)], st)
        seq = []
        for _ in range(ticks):
            if p.tick():
                p.left = durs[p.frame % len(durs)] or 1
            seq.append("%d%s" % (p.frame, "p" if p.state == 1 else "s"))
        cmd("APLAY %d %d %d %d %d %s" % (frames, inc, rep, st, ticks, " ".join(map(str, durs))),
            ["aplay" + "".join(" " + s for s in seq) + " t%d" % p.ticks])
    for _ in range(25):
        a = (rnd.randint(0, 5000), rnd.choice([0, 1, 10, 25, 60]), rnd.choice([0, 124, 394, 1000]),
             rnd.choice([0, 0, 8000, 22050]), rnd.choice([3546895, 3579545]), rnd.randint(0, 200000))
        cmd("ASND %d %d %d %d %d %d" % a, ["asnd %d" % sound_offset(*a)])
    for _ in range(12):
        w, h, sd, dd = rnd.randint(1, 40), rnd.randint(1, 6), rnd.randint(1, 8), rnd.randint(1, 8)
        table = bytes(rnd.getrandbits(8) for _ in range(256))
        planes = bytes(rnd.getrandbits(8) for _ in range(bpr_of(w) * sd * h))
        cmd("AREMAP %d %d %d %d %s %s" % (w, h, sd, dd, table.hex(), planes.hex()),
            [remap(w, h, sd, dd, table, planes)])
    # the remap against the one it replaced, on bitmaps the test makes
    cmd("AREMAPCMP 2026 3000", ["aremapcmp 3000 cases, 0 differ from the old remap"])
    for _ in range(10):
        l, t, w, h, ys, fr = (rnd.randint(0, 30), rnd.randint(0, 30), rnd.randint(0, 200), rnd.randint(0, 40),
                              rnd.choice([6, 8, 11]), rnd.randint(0, 40))
        p = panel_layout(l, t, w, h, ys)
        pts = [(rnd.randint(l - 2, l + w + 2), rnd.randint(t - 2, t + h + 2)) for _ in range(8)]
        lines = ["apanel %d %d %d %d %d %d %d %d %d %d" % (p["x"], p["y"], p["w"], p["h"], p["play_x"], p["stop_x"],
                                                       p["bw"], p["sx"], p["sw"], p["kw"])]
        for x, y in pts:
            hit, f = panel_hit(p, x, y, fr)
            lines.append("hit %d %d %d %d" % (x, y, hit, f if hit == 3 else 0))
        for i in range(min(fr, 4)):
            lines.append("knob %d %d" % (i, knob_x(p, i, fr)))
        lines += panel_draw(p, 1, 1, fr, 0, 3, 1) + panel_draw(p, 0, 0, fr, 0, 3, 1)
        cmd("APANEL %d %d %d %d %d %d %d %s" % (l, t, w, h, ys, fr, len(pts),
                                               " ".join("%d %d" % pt for pt in pts)), lines)

    # ---- the classes ----
    for wrap in (0, 1):
        for t in (b"hello world, and a second line\nthe\tend", b"\x1b[1mBold\x1b[0m words wrap here too and on",
                  b"", b"a\n\n\nb"):
            cmd("TCLASS %d %s" % (wrap, t.hex() if t else "00"), expect_tclass(wrap, t if t else b"\0"))
    ftxt_data = (form(b"FTXT", [chunk(b"CHRS", b"first "), chunk(b"FONS", b"\0" * 9), chunk(b"CHRS", b"second\n")]))
    for kind, data in ((1, b"file text\nline two\t!"), (2, b"memory\n"), (3, ftxt_data), (4, b"")):
        cmd("ACLASS %d %s" % (kind, data.hex()), expect_aclass(kind, data))
    runs = [
        (dict(op=5, w=32, h=10, depth=3, frames=6, reltime=6),
         ["layout", "render", "start0", "tick", "tick", "tick", "frame", "locate4", "tick", "pause", "render",
          "click10,45", "tick", "click70,45", "click3,45", "stop", "frame"]),
        (dict(op=8, long=True, w=48, h=8, depth=2, frames=5, reltime=2, vary=True),
         ["preload2", "repeat0", "sample", "layout", "start2", "tick", "tick", "tick", "tick", "tick", "tick", "frame",
          "noremap", "locate1", "render", "nopanel", "render", "start0", "tick"]),
        (dict(op=7, w=16, h=5, depth=1, frames=3, camg=0x800, reltime=10),
         ["render", "preload9", "start1", "tick", "tick", "tick", "tick", "tick", "tick", "tick", "tick"]),
    ]
    for j, (sp, ops) in enumerate(runs):
        data, truth = make_anim(rnd, sp)
        path = os.path.join(outdir, "n%02d.anim" % j)
        open(path, "wb").write(data)
        cmd("NCLASS %s %s" % (path, " ".join(ops)), expect_nclass(data, ops, truth))

    expect.append("end allocs 0")
    with open(script_path, "w") as f:
        f.write("\n".join(script) + "\n")
    with open(expect_path, "w") as f:
        f.write("\n".join(expect) + "\n")


if __name__ == "__main__":
    main()
