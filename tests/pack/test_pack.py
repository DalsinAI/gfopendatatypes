#!/usr/bin/env python3
"""The pack, built from stand-in build outputs and run on a stand-in disk.

    python3 -I tests/pack/test_pack.py

1. Stand-ins for what Datatypes/build.sh and the library's builds make:
   every datatype the Team builds, with real descriptors (mkdtdesc.py, the
   same base names as build.sh), a superclass no descriptor names, and the
   library with its system classes.
2. tools/build_pack.py --no-build --lha: the drawer, the icons (read back),
   the scripts (parsed), the manifest, the archive (listed and unpacked
   with lhasa when it is there).
3. Install-GFOpenDatatypes and Uninstall-GFOpenDatatypes run by minstaller.py
   on an AmigaOS 3.2 disk that has its own datatypes.library, classes and
   CDXL datatype: a Novice install; an install over it; the uninstall,
   which must leave the disk as it was; the same with the library left out
   and with some datatypes left out; and the refusals (OS 3.1, a 68030).
4. The descriptors the builds really write (Datatypes/descriptors.sh, with
   stand-ins for the hooks' load files): each in its class's choice, those
   with recognition code (DTCD) as written, all of them installed into
   DEVS:DataTypes and taken out again. Which file each one picks is
   tests/recognition/test_recognition.py's.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
import amiga_icon  # noqa: E402
import minstaller as mi  # noqa: E402

fails = 0


def check(ok: bool, what: str) -> None:
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


# class -> its descriptors: (file, name, group, id), as Datatypes/build.sh names them
DATATYPES = {
    "gfopenpng": [("GFOpenPNG", "GFOpenPNG", "pict", "png")],
    "gfopentga": [("GFOpenTGA", "Targa picture", "pict", "tga")],
    "webp": [("WebP", "WebP", "pict", "webp")],
    "openpicture": [("AVIF", "AVIF", "pict", "avif"), ("HEIC", "HEIC", "pict", "heic")],
    "opensound": [("FLAC", "FLAC", "soun", "flac"), ("WMA", "Windows Media audio", "soun", "wma")],
    "openmodule": [("ProTracker", "ProTracker module", "soun", "mod")],
    "webm": [("WebM", "WebM", "anim", "webm")],
    "openvideo": [("MP4", "MP4", "anim", "mp4")],
    "cdxl": [("CDXL", "CDXL", "anim", "cdxl")],
    "opendoc": [("DOCX", "Word document", "pict", "docx")],
    "gzip": [("gzip", "gzip", "data", "gzip")],
}
SUPERCLASSES = ["stream"]
SYSTEM = ["picture", "ilbm", "sound", "8svx", "text", "ascii", "animation", "anim"]


ZLIB = b"\0$VER: z.library 53.12 (13.10.2026) GFOpenZlib, AmiCradle Team\0"


def stand_ins(base: Path) -> tuple[Path, Path, Path]:
    dt, lib, zl = base / "dtout", base / "libout", base / "zout"
    (zl / "libs").mkdir(parents=True)
    (zl / "libs/z.library").write_bytes(ZLIB)
    (dt / "Classes/DataTypes").mkdir(parents=True)
    (dt / "Devs/DataTypes").mkdir(parents=True)
    for cls, descs in DATATYPES.items():
        (dt / f"Classes/DataTypes/{cls}.datatype").write_bytes(
            b"\0\0\x03\xf3" + f"\0$VER: {cls}.datatype 47.9 (23.10.2026)\0".encode())
        for f, name, group, ident in descs:
            subprocess.run([sys.executable, "-I", ROOT / "Datatypes/common/mkdtdesc.py", dt / "Devs/DataTypes" / f,
                            name, cls, group, ident, "#?", f"$VER: {f} 47.1", "'X'"], check=True)
    for cls in SUPERCLASSES:
        (dt / f"Classes/DataTypes/{cls}.datatype").write_bytes(f"\0$VER: {cls}.datatype 47.1\0".encode())
    (lib / "libs").mkdir(parents=True)
    (lib / "datatypes").mkdir()
    (lib / "libs/datatypes.library").write_bytes(
        b"\0$VER: datatypes.library 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\0")
    for c in SYSTEM:
        (lib / f"datatypes/{c}.datatype").write_bytes(f"\0$VER: {c}.datatype 47.100 Team\0".encode())
        (lib / f"datatypes/open{c}.datatype").write_bytes(b"twin, for the ABI test")
    return dt, lib, zl


def os32_disk() -> mi.Disk:
    """What matters of an AmigaOS 3.2 system disk: its own library, classes and CDXL."""
    files = {"SYS:Libs/datatypes.library": b"OS 3.2 datatypes.library 47.4",
             "DEVS:DataTypes/CDXL": b"OS 3.2 CDXL descriptor",
             "DEVS:DataTypes/ILBM": b"OS 3.2 ILBM descriptor",
             "SYS:Classes/DataTypes/cdxl.datatype": b"OS 3.2 cdxl.datatype",
             "SYS:C/AddDataTypes": b"OS 3.2 AddDataTypes",
             "SYS:Storage/DOSDrivers/PIPE": b"pipe"}
    for c in SYSTEM:
        files[f"SYS:Classes/DataTypes/{c}.datatype"] = f"OS 3.2 {c}.datatype".encode()
    dirs = ["SYS:Libs", "SYS:Classes", "SYS:Classes/DataTypes", "SYS:Devs", "SYS:Devs/DataTypes", "SYS:C",
            "SYS:Storage", "SYS:Storage/DOSDrivers"]
    return mi.Disk(files, dirs)


def run(disk: mi.Disk, drawer: Path, script: str, **kw) -> mi.Installer:
    inst = mi.Installer(disk, drawer, **kw)
    inst.run((drawer / script).read_text(encoding="latin-1"))
    return inst


def main() -> int:
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        dt, lib, zl = stand_ins(tmp)
        out = tmp / "pack"
        r = subprocess.run([sys.executable, "-I", ROOT / "tools/build_pack.py", "--no-build", "--version", "0.1",
                            "--datatypes-out", dt, "--library-out", lib, "--zlib-out", zl, "--out", out]
                           + (["--lha"] if shutil.which("jlha") or shutil.which("lha") else []),
                           capture_output=True, text=True)
        print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
        check(r.returncode == 0, "build_pack.py --no-build")
        if r.returncode:
            print(r.stderr)
            return 1
        top = out / "GFOpenDatatypes"
        man = json.loads((out / "GFOpenDatatypes.json").read_text())

        # the drawer
        names = [c["class"] for c in man["choices"]]
        check(names[:10] == [f"{c}.datatype" for c in ["gfopenpng", "gfopentga", "webp", "openpicture", "opensound",
                                                       "openmodule", "webm", "openvideo", "cdxl", "opendoc"]]
              and names[10:] == ["gzip.datatype"], "a choice a datatype, in the Installer's order")
        check([c["class"] for c in man["always"]] == ["stream.datatype"], "the superclass goes in with every install")
        for cls, descs in DATATYPES.items():
            c = next(c for c in man["choices"] if c["class"] == f"{cls}.datatype")
            want = {f"Classes/DataTypes/{cls}.datatype"} | {f"Devs/DataTypes/{d[0]}" for d in descs}
            check({f["pack"] for f in c["files"]} == want, f"{cls}: the class and its {len(descs)} descriptor(s)")
        lfiles = {f["pack"] for f in man["library"]["files"]}
        check(lfiles == {"Library/Libs/datatypes.library"} | {f"Library/Classes/DataTypes/{c}.datatype" for c in SYSTEM},
              "the library and its eight system classes, not the ABI twins")
        check(man["zlib"] and [f["dest"] for f in man["zlib"]["files"]] == ["LIBS:z.library"]
              and man["zlib"]["for"] == ["gzip.datatype", "gfopenpng.datatype"],
              "z.library (GFOpenZlib) in the pack, for gzip and gfopenpng")
        for c in man["choices"] + man["always"] + [man["library"], man["zlib"]]:
            for f in c["files"]:
                if not (top / f["pack"]).is_file():
                    check(False, f"{f['pack']} in the drawer")
        for info in ("Install-GFOpenDatatypes.info", "Uninstall-GFOpenDatatypes.info"):
            d = amiga_icon.parse((top / info).read_bytes())
            check(d["kind"] == amiga_icon.WBPROJECT and d["tool"] == "Installer"
                  and "APPNAME=GFOpenDatatypes" in d["types"] and "NOLOG" in d["types"], f"{info}: a project for Installer")
        check(amiga_icon.parse((out / "GFOpenDatatypes.info").read_bytes())["kind"] == amiga_icon.WBDRAWER,
              "GFOpenDatatypes.info: a drawer")
        readme = (top / "GFOpenDatatypes.readme").read_text(encoding="latin-1")
        check("Version:      0.1" in readme and max(len(x) for x in readme.splitlines()) <= 78,
              "the readme: its version, 78 columns")
        for s in ("Install-GFOpenDatatypes", "Uninstall-GFOpenDatatypes"):
            text = (top / s).read_text(encoding="latin-1")
            try:
                mi.parse(text)
                check(text.startswith(f"; $VER: {s} 0.1 ("), f"{s}: parses, $VER first")
            except SyntaxError as e:
                check(False, f"{s}: {e}")
        arc = out / "GFOpenDatatypes-0.1.lha"
        if arc.exists():
            ex = tmp / "unpacked"
            ex.mkdir()
            tool = shutil.which("lhasa") or shutil.which("jlha") or shutil.which("lha")
            subprocess.run([tool, "xq" if "lhasa" not in tool else "x", arc], cwd=ex, check=True,
                           capture_output=True)
            same = all((ex / p.relative_to(out)).read_bytes() == p.read_bytes()
                       for p in [out / "GFOpenDatatypes.info", *top.rglob("*")] if p.is_file())
            check(same, f"{arc.name}: unpacks to the drawer, byte for byte")
        else:
            print("skip the archive: no jlha or lha")

        # the Installer, on an AmigaOS 3.2 disk
        disk = os32_disk()
        before = disk.snapshot()
        i = run(disk, top, "Install-GFOpenDatatypes")
        every = man["choices"] + man["always"] + [man["library"], man["zlib"]]
        put_ok = all(disk.files.get(mi.norm(f["dest"])) == (top / f["pack"]).read_bytes() for c in every for f in c["files"])
        check(put_ok, "a Novice install puts every file where it goes")
        check(disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Libs/datatypes.library")]
              == b"OS 3.2 datatypes.library 47.4"
              and disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Devs/CDXL")] == b"OS 3.2 CDXL descriptor"
              and disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Classes/picture.datatype")]
              == b"OS 3.2 picture.datatype", "what it replaced is kept: OS 3.2's library, classes and CDXL")
        kept = [k for k in disk.files if k.startswith("sys:storage/gfopendatatypes/superseded/")]
        check(len(kept) == 2 + len(SYSTEM) + 1, f"only what was there is kept ({len(kept)} files)")
        check(mi.norm("SYS:Storage/GFOpenDatatypes/Uninstall-GFOpenDatatypes") in disk.files
              and mi.norm("SYS:Storage/GFOpenDatatypes/Uninstall-GFOpenDatatypes.info") in disk.files,
              "the uninstaller and its icon are left in SYS:Storage/GFOpenDatatypes")
        check(i.ran == ["C:AddDataTypes REFRESH QUIET >NIL:"], "AddDataTypes REFRESH runs")
        check("Restart the Amiga" in i.exit_text, "it says to restart, for the library")
        check(len(i.asked) == 3, f"two pages of datatypes and the library's question ({len(i.asked)})")
        needs = [m for m in i.messages if "z.library" in m or "gfopenservice.device" in m]
        check(len(needs) == 1 and "z.library" not in needs[0] and "gfopenservice.device in DEVS:" in needs[0],
              "it says what gfopenservice.device is needed for when it is missing, and nothing of z.library: it brings it")
        check(disk.files.get(mi.norm("LIBS:z.library")) == ZLIB and "GFOpenZlib" not in i.exit_text
              and "z.library" not in i.exit_text, "GFOpenZlib's z.library goes in; no restart for it when none was there")
        d2 = os32_disk()
        d2.files[mi.norm("LIBS:z.library")] = b"GFOpenZlib"
        d2.files[mi.norm("DEVS:gfopenservice.device")] = b"GFOpenService"
        i2 = run(d2, top, "Install-GFOpenDatatypes")
        check(not any("z.library" in m or "gfopenservice.device" in m for m in i2.messages),
              "and says nothing when both are there")

        run(disk, top, "Install-GFOpenDatatypes")
        check(disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Libs/datatypes.library")]
              == b"OS 3.2 datatypes.library 47.4", "installed again: OS 3.2's library stays the one kept")

        u = run(disk, top, "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "the uninstall leaves the disk as it was")
        check("Restart the Amiga" in u.exit_text, "the uninstall says to restart, for the library")

        # without the library, and with some datatypes left out
        disk = os32_disk()
        before = disk.snapshot()
        run(disk, top, "Install-GFOpenDatatypes",
            answers={"datatypes.library": 0, "(1 of 2)": 0b111111110, "(2 of 2)": 0})
        check(disk.files[mi.norm("SYS:Libs/datatypes.library")] == b"OS 3.2 datatypes.library 47.4"
              and disk.files[mi.norm("SYS:Classes/DataTypes/picture.datatype")] == b"OS 3.2 picture.datatype",
              "the library left out: OS 3.2's stays")
        check(mi.norm("DEVS:DataTypes/GFOpenPNG") not in disk.files and mi.norm("DEVS:DataTypes/gzip") not in disk.files
              and mi.norm("DEVS:DataTypes/WebP") in disk.files, "datatypes left out stay out")
        u = run(disk, top, "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "and the uninstall leaves the disk as it was")
        check("Restart" not in u.exit_text, "no restart when the library wasn't installed")

        # an Aminet z.library already there: replaced by GFOpenZlib, kept, and put back
        disk = os32_disk()
        disk.files[mi.norm("LIBS:z.library")] = b"Aminet z.library 53.11"
        before = disk.snapshot()
        i = run(disk, top, "Install-GFOpenDatatypes")
        check(disk.files[mi.norm("LIBS:z.library")] == ZLIB
              and disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Libs/z.library")] == b"Aminet z.library 53.11"
              and "GFOpenZlib's z.library" in i.exit_text,
              "an Aminet z.library is replaced by GFOpenZlib's, kept, and it says to restart")
        i = run(disk, top, "Install-GFOpenDatatypes")
        check(disk.files[mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Libs/z.library")] == b"Aminet z.library 53.11"
              and "GFOpenZlib's z.library" not in i.exit_text,
              "installed again: the Aminet one stays the one kept, and no restart for its own z.library")
        run(disk, top, "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "the uninstall puts the Aminet z.library back")
        for answers, what, want in ((({"(1 of 2)": 0b111111110, "(2 of 2)": 0b01}), "neither gzip nor gfopenpng", False),
                                    (({"(1 of 2)": 0b111111110, "(2 of 2)": 0b11}), "gzip without gfopenpng", True),
                                    (({"(1 of 2)": 0b000000001, "(2 of 2)": 0b00}), "gfopenpng without gzip", True)):
            disk = os32_disk()
            run(disk, top, "Install-GFOpenDatatypes", answers=answers)
            check((mi.norm("LIBS:z.library") in disk.files) == want,
                  f"{what}: z.library {'goes in' if want else 'stays out'}")

        # the older pack's descriptors (before the rename) would shadow ours: moved aside, and put back
        disk = os32_disk()
        disk.files[mi.norm("DEVS:DataTypes/OpenPNG")] = b"old OpenPNG descriptor"
        disk.files[mi.norm("SYS:Classes/DataTypes/openpng.datatype")] = b"old openpng.datatype"
        before = disk.snapshot()
        run(disk, top, "Install-GFOpenDatatypes")
        check(mi.norm("DEVS:DataTypes/OpenPNG") not in disk.files
              and disk.files.get(mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Devs/OpenPNG")) == b"old OpenPNG descriptor"
              and disk.files.get(mi.norm("SYS:Classes/DataTypes/openpng.datatype")) == b"old openpng.datatype",
              "the old OpenPNG descriptor goes to Superseded (its class stays, unused)")
        run(disk, top, "Install-GFOpenDatatypes")
        check(disk.files.get(mi.norm("SYS:Storage/GFOpenDatatypes/Superseded/Devs/OpenPNG")) == b"old OpenPNG descriptor",
              "installed again: still kept")
        run(disk, top, "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "the uninstall puts the old descriptor back")

        # an GFOpenUp-installed copy of a datatype is put back as it was
        disk = os32_disk()
        disk.files[mi.norm("SYS:Classes/DataTypes/webp.datatype")] = b"GFOpenUp's webp 47.8"
        before = disk.snapshot()
        run(disk, top, "Install-GFOpenDatatypes")
        run(disk, top, "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "a datatype GFOpenUp put there comes back after the uninstall")

        for kw, what in (({"os_version": 40}, "OS 3.1"), ({"cpu": "68030"}, "a 68030")):
            disk = os32_disk()
            before = disk.snapshot()
            try:
                run(disk, top, "Install-GFOpenDatatypes", **kw)
                check(False, f"refuses {what}")
            except mi.Abort as e:
                check(disk.snapshot() == before, f"refuses {what}, touching nothing: {e}")
        disk = os32_disk()
        run(disk, top, "Install-GFOpenDatatypes", cpu="68060")
        check(mi.norm("SYS:Classes/DataTypes/webp.datatype") in disk.files, "installs on a 68060")
        try:
            run(os32_disk(), top, "Uninstall-GFOpenDatatypes")
            check(False, "the uninstaller refuses when nothing is installed")
        except mi.Abort:
            check(True, "the uninstaller refuses when nothing is installed")

        # without a library build, the pack has no library question
        out2 = tmp / "pack2"
        r = subprocess.run([sys.executable, "-I", ROOT / "tools/build_pack.py", "--no-build", "--datatypes-out", dt,
                            "--library-out", tmp / "none", "--zlib-out", tmp / "none", "--out", out2],
                           capture_output=True, text=True)
        man2 = json.loads((out2 / "GFOpenDatatypes.json").read_text())
        text = (out2 / "GFOpenDatatypes/Install-GFOpenDatatypes").read_text(encoding="latin-1")
        check(r.returncode == 0 and man2["library"] is None and "#lib" not in text and man2["zlib"] is None,
              "no library or GFOpenZlib build: the datatypes only, and no library question")
        i = run(os32_disk(), out2 / "GFOpenDatatypes", "Install-GFOpenDatatypes")
        check(any("gzip.datatype, and gfopenpng.datatype's PNG writing, need z.library" in m for m in i.messages),
              "and without GFOpenZlib it says what needs a z.library")
        disk = os32_disk()
        before = disk.snapshot()
        run(disk, out2 / "GFOpenDatatypes", "Install-GFOpenDatatypes")
        run(disk, out2 / "GFOpenDatatypes", "Uninstall-GFOpenDatatypes")
        check(disk.snapshot() == before, "and that pack installs and comes out cleanly")

        shipping(tmp)

    print("all passed" if not fails else f"{fails} failed")
    return 1 if fails else 0


def shipping(tmp: Path) -> None:
    """The real descriptors (descriptors.sh) through the pack and the Installer."""
    dt = tmp / "dtreal"
    devs = dt / "Devs/DataTypes"
    devs.mkdir(parents=True)
    (dt / "Classes/DataTypes").mkdir(parents=True)
    hooks = tmp / "hooks"
    hooks.mkdir()
    for h in ("matroska", "mpegaudio", "oggtheora", "docservice"):
        (hooks / h).write_bytes(b"\0\0\x03\xf3DTHOOK:" + h.encode() + b"\0")
    r = subprocess.run(["sh", "-c", 'set -eu; HERE="$1"; DTHOOKS="$2"; . "$HERE/descriptors.sh"; desc_all "$3"',
                        "sh", str(ROOT / "Datatypes"), str(hooks), str(dt)], capture_output=True, text=True)
    check(r.returncode == 0, f"descriptors.sh writes {len(list(devs.iterdir()))} descriptors")
    if r.returncode:
        print(r.stderr)
        return
    import build_pack  # noqa: E402
    base = {d.name: build_pack.base_name(d.read_bytes()) for d in devs.iterdir()}
    for cls in set(base.values()) | set(SUPERCLASSES):
        (dt / f"Classes/DataTypes/{cls}.datatype").write_bytes(f"\0$VER: {cls}.datatype 47.9 (7.11.2026)\0".encode())
    out = tmp / "packreal"
    r = subprocess.run([sys.executable, "-I", ROOT / "tools/build_pack.py", "--no-build", "--version", "0.1",
                        "--datatypes-out", dt, "--library-out", tmp / "none", "--zlib-out", tmp / "none", "--out", out],
                       capture_output=True, text=True)
    check(r.returncode == 0, "build_pack.py with the real descriptors")
    if r.returncode:
        print(r.stderr)
        return
    man = json.loads((out / "GFOpenDatatypes.json").read_text())
    top = out / "GFOpenDatatypes"
    placed = {f["pack"][len("Devs/DataTypes/"):]: c["class"] for c in man["choices"] for f in c["files"]
              if f["pack"].startswith("Devs/DataTypes/")}
    check(placed == {n: f"{b}.datatype" for n, b in base.items()}, "each descriptor in its class's choice")
    coded = sorted(n for n in base if b"DTCD" in (devs / n).read_bytes())
    check(coded == ["CSV", "MKV", "MP3", "Markdown", "Ogg-Theora"]
          and all((top / "Devs/DataTypes" / n).read_bytes() == (devs / n).read_bytes() for n in coded),
          f"the ones with recognition code, as written: {', '.join(coded)}")
    check(not any(t == "Devs" and n in base for t, n in build_pack.RETIRED),
          "no descriptor of the pack's is one it retires")
    disk = os32_disk()
    before = disk.snapshot()
    run(disk, top, "Install-GFOpenDatatypes")
    check(all(disk.files.get(mi.norm(f"DEVS:DataTypes/{n}")) == (devs / n).read_bytes() for n in base),
          f"a Novice install puts all {len(base)} in DEVS:DataTypes")
    run(disk, top, "Uninstall-GFOpenDatatypes")
    check(disk.snapshot() == before, "and the uninstall takes them out again")


if __name__ == "__main__":
    sys.exit(main())
