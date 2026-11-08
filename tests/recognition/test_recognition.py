#!/usr/bin/env python3
"""Which datatype each kind of file goes to, with the descriptors the pack ships.

    python3 -I tests/recognition/test_recognition.py

1. Datatypes/descriptors.sh writes every descriptor the Team builds
   (desc_all), with stand-ins for the hooks' load files (their DTHOOK: tag
   alone; DTHOOKS= points at hooks/build.sh's real ones instead).
2. common/mkdtdesc.py's own options are checked on the bytes it writes
   (DT_IFF, DT_TEXT, DT_CASE, DT_CODE, a mask too long to ever match), and
   two test-only descriptors show why DT_IFF is needed: a binary one never
   meets IFF data.
3. A corpus of tiny made-up sample headers (no real media) goes through
   datatypes.library's own recognition, libraries/opendatatypes/dt_core.c,
   built for the 68k and run under qemu-m68k (tests/recognition/recognize.c),
   with each hook run as the C it is built from (Datatypes/hooks/dthooks.h).
   It runs twice, the descriptors added in one order and then the other, as
   a directory may list them: the answers must be the same both ways (no
   two descriptors tie on a file) and the ones expected here.
4. With the hooks built (DTHOOKS=, or OS32_GCC16= the stove, and this
   builds them with hooks/build.sh): each load file is loaded, relocated
   and run under qemu-m68k as datatypes.library runs it (hookrun.c), on
   every sample, and must answer as its C does; docservice against
   stand-in exec, dos and expansion libraries, on each case of device and
   card or Cradle, with every Forbid, OpenLibrary and Lock undone.

M68K_CC and QEMU_M68K as tests/m68k/; skipped with no m68k cross GCC.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DT = ROOT / "Datatypes"
LIB = ROOT / "libraries" / "opendatatypes"
HOOKS = ["matroska", "mpegaudio", "oggtheora", "docservice"]

fails = 0


def check(ok: bool, what: str) -> None:
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


# ---- descriptor files ----------------------------------------------------------

def chunks(d: bytes) -> dict:
    out, at = {}, 12
    while at + 8 <= len(d):
        cid, n = struct.unpack(">4sI", d[at:at + 8])
        out[cid] = d[at + 8:at + 8 + n]
        at += 8 + n + (n & 1)
    return out


def header(d: bytes) -> dict:
    h = chunks(d)[b"DTHD"]
    no, bo, po, mo, g, i, ml, _pad, fl, pri = struct.unpack(">IIII4s4shhHH", h[:32])
    return {"name": h[no:h.index(b"\0", no)].decode(), "flags": fl, "pri": pri, "masklen": ml,
            "mask": [struct.unpack(">H", h[mo + 2 * k:mo + 2 * k + 2])[0] for k in range(ml)]}


def mkdtdesc(out: Path, *args: str, **env: str) -> subprocess.CompletedProcess:
    e = {k: v for k, v in os.environ.items() if not k.startswith("DT_")}
    e.update(env)
    return subprocess.run([sys.executable, "-I", str(DT / "common/mkdtdesc.py"), str(out), *args],
                          env=e, capture_output=True, text=True)


def tool_checks(tmp: Path, extra: Path) -> None:
    """mkdtdesc.py's kinds and options, on its bytes."""
    code = tmp / "code.bin"
    code.write_bytes(b"\0\0\x03\xf3DTHOOK:test\0")
    r = mkdtdesc(extra / "TestIFF", "Test IFF", "testiff", "pict", "tst", "#?", "$VER: TestIFF 1.0",
                 *"'F' 'O' 'R' 'M' ANY ANY ANY ANY 'T' 'E' 'S' 'T'".split(), DT_IFF="1")
    check(r.returncode == 0, "mkdtdesc.py DT_IFF=1 writes a descriptor")
    h = header((extra / "TestIFF").read_bytes())
    check(h["flags"] == 0x0002 and h["masklen"] == 12, "DT_IFF=1: DTF_IFF, the FORM mask")
    # the same kind of mask, written as a binary descriptor: it never meets IFF data
    r = mkdtdesc(extra / "TestIFF-Bin", "Test IFF as binary", "testiff", "pict", "tst2", "#?", "$VER: TestIFF-Bin 1.0",
                 *"'F' 'O' 'R' 'M' ANY ANY ANY ANY 'T' 'E' 'S' '2'".split())
    check(r.returncode == 0 and header((extra / "TestIFF-Bin").read_bytes())["flags"] == 0, "the default: DTF_BINARY")
    r = mkdtdesc(tmp / "t1", "T", "t", "text", "t", "#?.t", "$VER: T 1.0", DT_TEXT="1", DT_CASE="1",
                 DT_CODE=str(code), DT_PRIORITY="-1")
    d = (tmp / "t1").read_bytes()
    h = header(d)
    check(r.returncode == 0 and h["flags"] == 0x0011 and h["pri"] == 0xFFFF,
          "DT_TEXT=1 DT_CASE=1: DTF_ASCII|DTF_CASE; DT_PRIORITY=-1 as 0xffff")
    check(chunks(d).get(b"DTCD") == code.read_bytes(), "DT_CODE: the file in a DTCD chunk, as it is")
    r = mkdtdesc(tmp / "t2", "T", "t", "text", "t", "#?", "$VER: T 1.0", DT_TEXT="1", DT_IFF="1")
    check(r.returncode != 0, "DT_TEXT and DT_IFF together: refused")
    r = mkdtdesc(tmp / "t3", "T", "t", "text", "t", "#?", "$VER: T 1.0", *(["ANY"] * 1085))
    check(r.returncode != 0 and "1084" in r.stderr, "a mask longer than the 1084 bytes the library reads: refused")
    r = mkdtdesc(tmp / "t4", "T", "t", "text", "t", "#?", "$VER: T 1.0", DT_CODE=str(tmp / "nothing"))
    check(r.returncode != 0 and "DT_CODE" in r.stderr, "DT_CODE with no such file: refused")


def real_hooks(tmp: Path) -> str | None:
    """The built hooks: DTHOOKS, else built here when the stove is to hand."""
    if os.environ.get("DTHOOKS"):
        return os.environ["DTHOOKS"]
    stove = os.environ.get("OS32_GCC16")
    if stove and Path(stove, "prefix/bin/m68k-amigaos-gcc").is_file():
        out = tmp / "built-hooks"
        r = subprocess.run(["sh", str(DT / "hooks/build.sh")], env=dict(os.environ, DTHOOKS=str(out)),
                           capture_output=True, text=True)
        check(r.returncode == 0, "hooks/build.sh: " + (r.stdout.strip().replace("\n", ", ") or r.stderr.strip()))
        return str(out) if r.returncode == 0 else None
    return None


def write_descriptors(tmp: Path, hooks: str | None) -> Path:
    out = tmp / "pack"
    (out / "Devs/DataTypes").mkdir(parents=True)
    if not hooks:
        hooks = str(tmp / "hooks")
        os.mkdir(hooks)
        for h in HOOKS:
            Path(hooks, h).write_bytes(b"DTHOOK:" + h.encode() + b"\0")
    r = subprocess.run(["sh", "-c", 'set -eu; HERE="$1"; DTHOOKS="$2"; . "$HERE/descriptors.sh"; desc_all "$3"',
                        "sh", str(DT), hooks, str(out)], capture_output=True, text=True)
    check(r.returncode == 0, f"descriptors.sh desc_all: {len(list((out / 'Devs/DataTypes').iterdir()))} descriptors")
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(1)
    for h in HOOKS:
        check(f"DTHOOK:{h}".encode() in Path(hooks, h).read_bytes(), f"hook {h}: its tag in the load file")
    return out / "Devs/DataTypes"


# ---- the corpus ------------------------------------------------------------------

def pad(b: bytes, n: int, fill: bytes = b"\0") -> bytes:
    return b + fill * (n - len(b)) if len(b) < n else b


EBML_BODY = bytes.fromhex("4286 8101 42f7 8101 42f2 8104 42f3 8108")


def ebml(doctype: bytes, long_size: bool = False, doctype_first: bool = False) -> bytes:
    dt = b"\x42\x82" + bytes([0x80 | len(doctype)]) + doctype
    body = (dt + EBML_BODY if doctype_first else EBML_BODY + dt) + bytes.fromhex("4287 8104 4285 8102")
    size = (b"\x01" + b"\0" * 6 + bytes([len(body)])) if long_size else bytes([0x80 | len(body)])
    seg = bytes.fromhex("18538067 01ffffffffffffff 114d9b74 8100")
    return pad(b"\x1a\x45\xdf\xa3" + size + body + seg, 200)


def png(chunks_after_ihdr: list[bytes]) -> bytes:
    def c(t: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + t + data + b"\0\0\0\0"
    out = b"\x89PNG\r\n\x1a\n" + c(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0))
    for ch in chunks_after_ihdr:
        out += ch
    return out + c(b"IDAT", b"\x78\x9c\0\0") + c(b"IEND", b"")


def png_chunk(t: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + t + data + b"\0\0\0\0"


def mpa(h: bytes, length: int, frames: int = 2, total: int = 0) -> bytes:
    """MPEG audio frames of the given header and length, silent"""
    out = b"".join(h + b"\0" * (length - 4) for _ in range(frames))
    return pad(out, total) if total else out


def ogg_page(packet: bytes, bos: bool) -> bytes:
    segs, n = [], len(packet)
    while n >= 255:
        segs.append(255)
        n -= 255
    segs.append(n)
    return (b"OggS\0" + bytes([2 if bos else 0]) + b"\0" * 8 + b"\x01\0\0\0" + b"\0" * 8 + bytes([len(segs)])
            + bytes(segs) + packet)


THEORA = b"\x80theora" + b"\x03\x02\x01" + b"\0" * 32
VORBIS = b"\x01vorbis" + b"\0" * 23
SKELETON = b"fishead\0" + b"\0" * 56
OGG_DATA = ogg_page(b"\x05vorbis" + b"\0" * 40, False)


def m2ts(packets: int) -> bytes:
    return b"".join(b"\x12\x34\x56\x78" + b"\x47\x40\x00\x10" + b"\xff" * 184 for _ in range(packets))


def ts(packets: int) -> bytes:
    return b"".join(b"\x47\x40\x00\x10" + b"\xff" * 184 for _ in range(packets))


def ftyp(brand: bytes, compat: bytes = b"isom") -> bytes:
    return struct.pack(">I", 24) + b"ftyp" + brand + b"\0\0\x02\0" + compat + b"mp41" + pad(b"\0\0\0\x08free", 32)


def mod(sig: bytes) -> bytes:
    return pad(b"song", 1080) + sig + b"\0" * 64


def zip_first(name: bytes, data: bytes) -> bytes:
    return b"PK\x03\x04" + b"\0" * 22 + struct.pack("<HH", len(name), 0) + name + data


# (file name or "-" for memory, the data, the descriptor expected, the doc.render service there)
CASES: list[tuple[str, bytes, str, int]] = [
    # Matroska and WebM: one EBML magic, the DocType tells them apart
    ("film.mkv", ebml(b"matroska"), "Matroska video", 0),
    ("film.mkv", ebml(b"matroska", long_size=True), "Matroska video", 0),
    ("-", ebml(b"matroska", long_size=True), "Matroska video", 0),
    ("odd.bin", ebml(b"matroska", doctype_first=True), "Matroska video", 0),
    ("clip.webm", ebml(b"webm"), "WebM", 0),
    ("clip.mkv", ebml(b"webm"), "WebM", 0),
    ("-", ebml(b"webm", long_size=True), "WebM", 0),
    ("broken.mkv", pad(b"\x1a\x45\xdf\xa3\xff", 64), "WebM", 0),
    # APNG: acTL straight after IHDR, or by name with acTL later; a PNG is a PNG
    ("anim.png", png([png_chunk(b"acTL", b"\0\0\0\x02\0\0\0\0")]), "Animated PNG", 0),
    ("anim.apng", png([png_chunk(b"tEXt", b"Software\0x"), png_chunk(b"acTL", b"\0\0\0\x02\0\0\0\0")]),
     "Animated PNG (by name)", 0),
    ("still.png", png([png_chunk(b"tEXt", b"Software\0x")]), "GFOpenPNG", 0),
    ("-", png([]), "GFOpenPNG", 0),
    # SVG and SVGZ
    ("drawing.svgz", b"\x1f\x8b\x08\x00\0\0\0\0\0\x03" + b"\x01" * 20, "SVG drawing (compressed)", 0),
    ("archive.gz", b"\x1f\x8b\x08\x00\0\0\0\0\0\x03" + b"\x01" * 20, "GZip", 0),
    ("-", b"\x1f\x8b\x08\x00\0\0\0\0\0\x03" + b"\x01" * 20, "GZip", 0),
    ("drawing.svg", b'<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>\n', "SVG drawing", 0),
    # MPEG transport streams: 188-byte packets, and BDAV's 192-byte ones
    ("film.m2ts", m2ts(3), "MPEG transport stream (BDAV)", 0),
    ("00001.mts", m2ts(5), "MPEG transport stream (BDAV)", 0),
    ("-", m2ts(3), "MPEG transport stream (BDAV)", 0),
    ("film.ts", ts(3), "MPEG transport stream", 0),
    ("film.m2ts", m2ts(2), "binary", 0),    # shorter than the mask: no answer from it
    # MPEG audio with no ID3v2 tag: MPEG 1 and 2 Layer III, Layer II
    ("song.mp3", mpa(b"\xff\xfb\x90\x64", 417), "MPEG audio", 0),
    ("-", mpa(b"\xff\xfb\x92\x64", 418), "MPEG audio", 0),
    ("song.mp3", mpa(b"\xff\xf3\x80\xc4", 208), "MPEG audio", 0),
    ("song.mp3", mpa(b"\xff\xe3\x80\xc4", 417), "MPEG audio", 0),    # MPEG 2.5, 64 kbit/s at 11025 Hz
    ("radio.mp2", mpa(b"\xff\xfd\xa4\x04", 576), "MPEG audio", 0),
    ("loud.mp3", mpa(b"\xff\xfb\xe8\x64", 1440, frames=2), "MPEG audio", 0),    # 1440-byte frames, past what is read
    ("short.mp3", mpa(b"\xff\xfb\xe8\x64", 600, frames=1), "binary", 0),
    ("song.mp3", b"ID3\x04\0\0\0\0\0\x10" + b"\0" * 16 + mpa(b"\xff\xfb\x90\x64", 417), "MP3 (ID3)", 0),
    ("noise.bin", b"\xff\xfb\x90\x64" + b"\0" * 413 + b"\x12\x34\x56\x78" + b"\0" * 400, "binary", 0),
    ("noise.bin", b"\xff\xfb\xf0\x64" + b"\0" * 500, "binary", 0),    # bit rate index 15
    ("noise.bin", b"\xff\xfe\x41\x00" + b"\0" * 500, "binary", 0),    # Layer I bits (a UTF-16 mark): left out
    ("photo.jpg", b"\xff\xd8\xff\xe0\0\x10JFIF\0" + b"\0" * 64, "binary", 0),
    ("jxl.jxl", b"\xff\x0a" + b"\0" * 64, "JPEG XL", 0),
    # AAC: ADTS MPEG-4 and MPEG-2, with and without a CRC, and ADIF
    ("a.aac", b"\xff\xf1\x50\x80\x02\x1f\xfc" + b"\0" * 64, "AAC (ADTS)", 0),
    ("a.aac", b"\xff\xf0\x50\x80\x02\x1f\xfc" + b"\0" * 64, "AAC (ADTS, CRC)", 0),
    ("a.aac", b"\xff\xf9\x50\x80\x02\x1f\xfc" + b"\0" * 64, "AAC (ADTS, MPEG-2)", 0),
    ("-", b"\xff\xf8\x50\x80\x02\x1f\xfc" + b"\0" * 64, "AAC (ADTS, MPEG-2, CRC)", 0),
    ("a.aac", b"ADIF\0\0\0\0\x01\x02" + b"\0" * 32, "AAC (ADIF)", 0),
    # MPEG-4: M4A and M4B brands anywhere; other brands by name (audio) or as video
    ("a.m4a", ftyp(b"M4A "), "MPEG-4 audio", 0),
    ("-", ftyp(b"M4A "), "MPEG-4 audio", 0),
    ("book.m4b", ftyp(b"M4B "), "MPEG-4 audiobook", 0),
    ("a.m4a", ftyp(b"mp42"), "MPEG-4 audio (by name)", 0),
    ("a.M4A", ftyp(b"isom"), "MPEG-4 audio (by name)", 0),
    ("a.m4a", ftyp(b"dash"), "MPEG-4 audio (by name)", 0),
    ("film.mp4", ftyp(b"isom"), "MPEG-4 video", 0),
    ("film.mp4", ftyp(b"mp42"), "MPEG-4 video", 0),
    ("-", ftyp(b"mp42"), "binary", 0),
    ("photo.avif", ftyp(b"avif"), "AVIF", 0),
    # Ogg: Theora films to gfopenvideo (with Skeleton or a sound stream first), sound to gfopensound
    ("film.ogv", ogg_page(THEORA, True) + OGG_DATA, "Ogg Theora video", 0),
    ("-", ogg_page(THEORA, True) + ogg_page(VORBIS, True) + OGG_DATA, "Ogg Theora video", 0),
    ("film.ogg", ogg_page(VORBIS, True) + ogg_page(THEORA, True) + OGG_DATA, "Ogg Theora video", 0),
    ("film.ogv", ogg_page(SKELETON, True) + ogg_page(THEORA, True) + ogg_page(VORBIS, True) + OGG_DATA,
     "Ogg Theora video", 0),
    ("song.ogg", ogg_page(VORBIS, True) + OGG_DATA, "Ogg sound", 0),
    ("song.opus", ogg_page(b"OpusHead\x01\x02" + b"\0" * 9, True) + OGG_DATA, "Ogg sound", 0),
    ("odd.ogg", ogg_page(VORBIS, True) + ogg_page(b"\x80theora" + b"\0" * 35, False), "Ogg sound", 0),
    # PostScript: "%!PS-Adobe", a bare "%!", text or with 8-bit data; DOS EPS
    ("a.ps", b"%!PS-Adobe-3.0\n%%Title: t\nshowpage\n", "PostScript", 0),
    ("old.ps", b"%!\n/Times-Roman findfont 12 scalefont setfont\n", "PostScript", 0),
    ("-", b"%!PS-Adobe-3.0\n%%BeginBinary: 4\n\x80\x81\x00\x9f\n", "PostScript (binary)", 0),
    ("pic.eps", b"\xc5\xd0\xd3\xc6\x20\0\0\0" + b"\0" * 24, "EPS with preview", 0),
    ("noise.bin", b"\xe5\xf0\xf3\xe6\x20\0\0\0" + b"\0" * 24, "binary", 0),
    ("a.txt", b"Some %! text\n", "ascii", 0),
    # Markdown and CSV: laid out by the service when it can be there, else text
    ("README.md", b"# Title\n\nSome *text*.\n", "Markdown", 1),
    ("README.md", b"# Title\n\nSome *text*.\n", "ascii", 0),
    ("notes.markdown", b"A line\n", "Markdown", 1),
    ("table.csv", b"a,b,c\n1,2,3\n", "CSV table", 1),
    ("table.tsv", b"a\tb\n1\t2\n", "ascii", 0),
    ("table.csv", b"a,b,c\n1,2,3\n", "ascii", 0),
    ("-", b"# Title\n", "ascii", 1),
    # IFF needs an IFF descriptor (DT_IFF): a binary one never meets IFF data
    ("x.tst", b"FORM\0\0\0\x10TESTBODY\0\0\0\x04abcd", "Test IFF", 0),
    ("x.tst", b"FORM\0\0\0\x10TES2BODY\0\0\0\x04abcd", "iff", 0),
    # the rest stays where it was
    ("p.webp", b"RIFF\x24\0\0\0WEBPVP8 " + b"\0" * 24, "WebP", 0),
    ("v.avi", b"RIFF\x24\0\0\0AVI LIST" + b"\0" * 24, "AVI video", 0),
    ("a.gif", b"GIF89a\x10\0\x10\0\x80\0\0" + b"\0" * 16, "GFOpenGIF", 0),
    ("a.flac", b"fLaC\0\0\0\x22" + b"\0" * 34, "FLAC", 0),
    ("a.mid", b"MThd\0\0\0\x06\0\x01\0\x02\x01\xe0MTrk", "MIDI", 0),
    ("song.mod", mod(b"M.K."), "ProTracker module", 0),
    ("mod.song", pad(b"song", 600), "SoundTracker module", 0),
    ("a.xm", b"Extended Module: tune" + b"\0" * 60, "FastTracker 2 module", 0),
    ("a.rtf", b"{\\rtf1\\ansi hello}\n", "Rich Text Format", 0),
    ("a.odt", zip_first(b"mimetype", b"application/vnd.oasis.opendocument.text"), "OpenDocument", 0),
    ("a.docx", b"PK\x03\x04" + b"\0" * 40, "Word document", 0),
    ("a.zip", b"PK\x03\x04" + b"\0" * 40, "binary", 0),
    ("tv", b"#NETSTREAM\nNAME=x\nURL=http://h/s\n", "Network stream", 0),
    ("a.mpg", b"\0\0\x01\xba\x44" + b"\0" * 32, "MPEG video", 0),
    ("a.tga", b"\0\0\x02" + b"\0" * 15, "Targa picture", 0),
    ("a.br", b"\x1b\x03\0\xf8" + b"\0" * 8, "Brotli", 0),
    ("a.xl", b"\x01" + b"\0" * 63, "CDXL", 0),
    ("a.txt", b"Hello\n", "ascii", 0),
    ("a.bin", b"\0\x01\x02\x03", "binary", 0),
]


def build(tmp: Path, name: str, sources: list[Path]) -> list[str] | None:
    cc = os.environ.get("M68K_CC", "m68k-linux-gnu-gcc")
    qemu = os.environ.get("QEMU_M68K", "qemu-m68k")
    if not shutil.which(cc):
        print(f"skipped: no {cc}")
        return None
    exe = tmp / name
    subprocess.run([cc, "-std=gnu99", "-O2", "-m68040", "-static", "-fno-tree-loop-distribute-patterns",
                    "-Wall", "-Wextra", "-Werror", f"-I{LIB}", f"-I{DT / 'hooks'}",
                    *map(str, sources), "-o", str(exe)], check=True)
    if not shutil.which(qemu):
        print(f"skipped: no {qemu} to run it (built)")
        return None
    return [qemu, "-cpu", "m68040", str(exe)]


def built_hooks(tmp: Path, hooks: str, recognize: list[str], samples: list[str]) -> None:
    """Each built hook, run as the library runs it, against its C."""
    run = build(tmp, "hookrun", [HERE / "hookrun.c"])
    if not run:
        return
    for h in ("matroska", "mpegaudio", "oggtheora"):
        want = subprocess.run(recognize + ["-h", h] + samples, capture_output=True, text=True, check=True).stdout
        r = subprocess.run(run + [str(Path(hooks, h)), "000"] + samples, capture_output=True, text=True)
        yes = want.split().count("1")
        check(r.returncode == 0 and r.stdout == want,
              f"hooks/{h} as built: its C's answer on all {len(samples)} samples ({yes} yes)")
        if r.stdout != want:
            print(r.stdout, r.stderr)
    # device in memory, device on disk, then no card or Cradle (0), a card (1), a paired Cradle (2)
    for case in ("000", "001", "002", "010", "011", "012", "100", "101", "102", "110", "112"):
        r = subprocess.run(run + [str(Path(hooks, "docservice")), case, samples[0]], capture_output=True, text=True)
        want = "1" if case[:2] != "00" and case[2] != "0" else "0"
        check(r.returncode == 0 and r.stdout.strip() == want,
              f"hooks/docservice as built, device {case[:2]} reach {case[2]}: {r.stdout.strip() or r.stderr.strip()}")


def main() -> int:
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        extra = tmp / "extra"
        extra.mkdir()
        tool_checks(tmp, extra)
        hooks = real_hooks(tmp)
        devs = write_descriptors(tmp, hooks)
        descs = sorted(devs.iterdir()) + sorted(extra.iterdir())
        names = [header(d.read_bytes())["name"] for d in descs]
        check(len(set(n.lower() for n in names)) == len(names), "every descriptor has a name of its own")
        run = build(tmp, "recognize", [HERE / "recognize.c", LIB / "dt_core.c"])
        if not run:
            return 1 if fails else 0
        args = []
        for k, (name, data, _want, svc) in enumerate(CASES):
            f = tmp / f"sample{k}"
            f.write_bytes(data)
            args += [str(f), name, str(svc)]
        got = {}
        for order in ("", "-r"):
            r = subprocess.run(run + ([order] if order else []) + [str(d) for d in descs] + ["--"] + args,
                               capture_output=True, text=True)
            lines = r.stdout.splitlines()
            check(r.returncode == 0 and len(lines) == len(CASES),
                  f"recognize {'(the other order) ' if order else ''}ran on {len(CASES)} samples")
            if r.returncode or len(lines) != len(CASES):
                print(r.stdout, r.stderr)
                return 1
            got[order] = lines
        for k, (name, _data, want, svc) in enumerate(CASES):
            a, b = got[""][k], got["-r"][k]
            what = f"{name}{' (service)' if svc else ''} [{k}]"
            if a != b:
                check(False, f"{what}: {a!r} or {b!r} by the order the descriptors were added")
            else:
                check(a == want, f"{what}: {a}" + ("" if a == want else f", not {want}"))
        if hooks:
            built_hooks(tmp, hooks, run, [str(tmp / f"sample{k}") for k in range(len(CASES))])
        else:
            print("the hooks as built: not tried (no DTHOOKS, no OS32_GCC16 stove)")
    print(f"{fails} failed" if fails else "all passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
