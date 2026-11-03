#!/usr/bin/env python3
"""Builds GFOpenDatatypes, the pack: a drawer to run the Installer from, and
the same as an LhA archive.

    python3 tools/build_pack.py [--version 0.1] [--out build/pack] [--no-build] [--lha]

The user, 23 October 2026: the repository's objective is "an amiga installer
that allows them to be installed as a pack". The pack holds every datatype
Datatypes/build.sh makes, with its descriptors, and datatypes.library with
the system classes (libraries/opendatatypes), offered and ticked. One
Commodore Installer script installs it on AmigaOS 3.2 with nothing else
needed; what it replaces is kept in SYS:Storage/GFOpenDatatypes, and
Uninstall-GFOpenDatatypes puts it back. Both scripts are written here from
what the builds made, so a new datatype needs no change to this file.

  build/pack/GFOpenDatatypes/           the drawer: run Install-GFOpenDatatypes
    Install-GFOpenDatatypes(.info)
    Uninstall-GFOpenDatatypes(.info)    also copied to SYS:Storage/GFOpenDatatypes
    GFOpenDatatypes.readme
    Classes/DataTypes/                the Team's datatypes
    Devs/DataTypes/                   their descriptors
    Library/Libs/datatypes.library    the library, and its system classes in
    Library/Classes/DataTypes/        Library/Classes/DataTypes
    Libs/z.library                    GFOpenZlib, for gzip and gfopenpng's PNG writing
  build/pack/GFOpenDatatypes.info       the drawer's icon
  build/pack/GFOpenDatatypes.json       what is in it, for GFOpenUp's GFOpenImage part
  build/pack/GFOpenDatatypes-<version>.lha   with --lha (jlha or lha on PATH)

z.library is the Team's GFOpenZlib (amicradle-guest libraries/openzlib), built
from a checkout of it (AMICRADLE_GUEST or GUEST, else beside this one) and
installed whenever a datatype that opens it is: the user, 23 October 2026,
"GFOpenZlib as z.library". Without a checkout the pack has none, and the
Installer says what needs one.

Each datatype is one choice in the Installer, named by its class: a class's
descriptors are the files in Devs/DataTypes whose DTHD base name is that
class (Datatypes/common/mkdtdesc.py writes it). A class no descriptor names
(a superclass such as stream.datatype) goes in with every install.

The builds use the os32-gcc16 stove, as their own scripts do (OS32_GCC16 for
Datatypes/build.sh, STOVE for the library's). --no-build takes what they
already made.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import amiga_icon  # noqa: E402

LIBDIR = ROOT / "libraries" / "opendatatypes"
# datatypes.library's system classes: the build script of each family, and
# the replacement files it makes (the open*.datatype twins are for the ABI
# tests and stay out of the pack).
SYSTEM_CLASSES = {
    "picture": ["picture.datatype"],
    "ilbm": ["ilbm.datatype"],
    "sound": ["sound.datatype"],
    "8svx": ["8svx.datatype"],
    "text": ["text.datatype", "ascii.datatype"],
    "animation": ["animation.datatype", "anim.datatype"],
}

# What each class is called in the Installer; a class not named here is
# shown by its file name. Short: the 3.2 Installer's choice list is narrow.
LABELS = {
    "gfopenpng": "PNG pictures (gfopenpng)",
    "gfopentga": "Targa pictures (gfopentga)",
    "gfopengif": "GIF pictures and animations (gfopengif)",
    "webp": "WebP pictures (webp)",
    "openpicture": "AVIF, HEIC, JPEG XL, RAW... by a Cradle (openpicture)",
    "opensound": "FLAC, Ogg, MP3, AAC... by a Cradle (opensound)",
    "openmodule": "Music modules: MOD, MED, XM, S3M, IT (openmodule)",
    "webm": "WebM films (webm)",
    "openvideo": "MP4, MKV, AVI... by a Cradle (openvideo)",
    "cdxl": "CDXL films (cdxl)",
    "opendoc": "Office documents and e-books by a Cradle (opendoc)",
    "gzip": "gzip files (gzip)",
    "brotli": "Brotli files (brotli)",
}
# Shown first, in this order; the rest follow by name.
ORDER = ["gfopenpng", "gfopentga", "gfopengif", "webp", "openpicture", "opensound", "openmodule",
         "webm", "openvideo", "cdxl", "opendoc"]
PAGE = 9            # the 3.2 Installer shows nine choices a page (GFOpenUp, 8 October 2026)
# The datatypes that open z.library (Datatypes/README.md): the pack brings the Team's own, GFOpenZlib, and
# installs it with them (the user, 23 October 2026: GFOpenZlib is the z.library).
ZLIB_USERS = ("gzip.datatype", "gfopenpng.datatype")

STORE = "SYS:Storage/GFOpenDatatypes"
DEST = {"Classes": "SYS:Classes/DataTypes", "Devs": "DEVS:DataTypes", "Libs": "LIBS:"}


def tackon(directory: str, name: str) -> str:
    """AmigaDOS's path joining, as the Installer's tackon does it."""
    return directory + name if directory.endswith((":", "/")) else directory + "/" + name


def run(cmd: list, env: dict | None = None, cwd: Path | None = None) -> None:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, env=env, cwd=cwd)


def guest_checkout() -> Path | None:
    """amicradle-guest, for GFOpenZlib: AMICRADLE_GUEST or GUEST, else beside this repository."""
    for p in (os.environ.get("AMICRADLE_GUEST"), os.environ.get("GUEST"),
              ROOT.parent / "amicradle-guest", ROOT.parent / "guest"):
        if p and (Path(p) / "libraries/openzlib/build-amiga.sh").is_file():
            return Path(p)
    return None


def build(lib_out: Path, zlib_out: Path) -> None:
    env = dict(os.environ)
    run(["sh", ROOT / "Datatypes/build.sh"], env=env)
    if LIBDIR.is_dir():
        env["OUT"] = str(lib_out)
        run(["sh", LIBDIR / "build-amiga.sh"], env=env)
        for family in SYSTEM_CLASSES:
            run(["sh", LIBDIR / "classes/datatypes" / family / "build-amiga.sh"], env=env)
    # z.library: GFOpenZlib (amicradle-guest libraries/openzlib), with its own script and stove, as GFOpenUp builds it
    g = guest_checkout()
    if g:
        run(["sh", g / "libraries/openzlib/build-amiga.sh"], env=dict(os.environ, OUT=str(zlib_out)))
    else:
        print("build_pack: no amicradle-guest checkout (AMICRADLE_GUEST): the pack has no z.library")


def base_name(descriptor: bytes) -> str:
    """The DTHD base name of a DEVS:DataTypes descriptor: its class, without .datatype."""
    if descriptor[:4] != b"FORM" or descriptor[8:12] != b"DTYP":
        raise ValueError("not a FORM DTYP")
    off, end = 12, 8 + struct.unpack(">I", descriptor[4:8])[0]
    while off + 8 <= end:
        cid, size = struct.unpack_from(">4sI", descriptor, off)
        body = descriptor[off + 8:off + 8 + size]
        if cid == b"DTHD":
            b = struct.unpack_from(">I", body, 4)[0]
            return body[b:body.index(b"\0", b)].decode("latin-1")
        off += 8 + size + (size & 1)
    raise ValueError("no DTHD chunk")


def version_string(data: bytes) -> str:
    i = data.find(b"$VER: ")
    if i < 0:
        return ""
    j = i + 6
    while j < len(data) and data[j] not in (0, 10, 13):
        j += 1
    return data[i + 6:j].decode("latin-1")


def collect(dt_out: Path, lib_out: Path, zlib_out: Path) -> tuple[list[dict], list[dict], dict | None, dict | None]:
    """The choices (one a class, with its descriptors), the classes every install takes,
    the library choice (None when the library wasn't built) and z.library (None when it wasn't
    built, or no datatype here opens it)."""
    classes = {p.name: p for p in sorted((dt_out / "Classes/DataTypes").glob("*.datatype"))}
    if not classes:
        sys.exit(f"build_pack: no datatypes in {dt_out / 'Classes/DataTypes'}: build them first")
    by_class: dict[str, list[Path]] = {}
    for d in sorted((dt_out / "Devs/DataTypes").iterdir()):
        if not d.is_file() or d.name.endswith(".info"):
            continue
        cls = base_name(d.read_bytes()) + ".datatype"
        if cls not in classes:
            sys.exit(f"build_pack: descriptor {d.name} names {cls}, which the build didn't make")
        by_class.setdefault(cls, []).append(d)
    choices, always = [], []
    names = sorted(classes, key=lambda n: (ORDER.index(n[:-9]) if n[:-9] in ORDER else len(ORDER), n))
    for name in names:
        files = [{"pack": f"Classes/DataTypes/{name}", "tag": "Classes", "name": name, "src": classes[name]}]
        files += [{"pack": f"Devs/DataTypes/{d.name}", "tag": "Devs", "name": d.name, "src": d}
                  for d in by_class.get(name, [])]
        entry = {"class": name, "label": LABELS.get(name[:-9], name), "files": files}
        (choices if name in by_class else always).append(entry)
    library = None
    lib = lib_out / "libs/datatypes.library"
    if lib.is_file():
        files = [{"pack": "Library/Libs/datatypes.library", "tag": "Libs", "name": "datatypes.library", "src": lib}]
        for family, made in SYSTEM_CLASSES.items():
            for name in made:
                p = lib_out / "datatypes" / name
                if not p.is_file():
                    sys.exit(f"build_pack: {p} is missing: build {family}'s class first")
                files.append({"pack": f"Library/Classes/DataTypes/{name}", "tag": "Classes", "name": name, "src": p})
        library = {"label": "datatypes.library and the system classes", "files": files,
                   "version": version_string(lib.read_bytes())}
    zlib = None
    z = zlib_out / "libs/z.library"
    users = [n for n in ZLIB_USERS if n in classes]
    if z.is_file() and users:
        zlib = {"label": "z.library (GFOpenZlib)", "for": users, "version": version_string(z.read_bytes()),
                "files": [{"pack": "Libs/z.library", "tag": "Libs", "name": "z.library", "src": z}]}
    return choices, always, library, zlib


# ---------------------------------------------------------------- the Installer scripts

def q(s: str) -> str:
    """A string in the Installer's language."""
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


COMMON = f"""(set #store {q(STORE)})
(set #sup (tackon #store "Superseded"))
(set #ins (tackon #store "Installed"))

; One file: #src (in this drawer), #dir (where it goes), #name, #tag (Classes,
; Devs or Libs: its folder under Superseded and Installed).
"""

PUT = """; Installs one file. What was there first is kept in Superseded, once: a file
; with a mark in Installed is ours from an earlier install and is not kept.
(procedure P_PUT
    (set #dest (tackon #dir #name))
    (set #old (tackon (tackon #sup #tag) #name))
    (set #mark (tackon (tackon #ins #tag) #name))
    (if (= (exists #dest (noreq)) 1)
        (if (= (exists #mark (noreq)) 0)
            (if (= (exists #old (noreq)) 0)
                (copyfiles (source #dest) (dest (tackon #sup #tag)) (nogauge) (optional "force")))))
    (copyfiles (source #src) (dest #dir) (newname #name) (nogauge) (optional "force"))
    (textfile (dest #mark) (append (cat "GFOpenDatatypes " #version "\\n")))
)

(procedure P_DIRS
    (if (= (exists #store (noreq)) 0) (makedir #store))
    (if (= (exists #sup (noreq)) 0) (makedir #sup))
    (if (= (exists #ins (noreq)) 0) (makedir #ins))
    (set #d (tackon #sup "Classes")) (if (= (exists #d (noreq)) 0) (makedir #d))
    (set #d (tackon #sup "Devs")) (if (= (exists #d (noreq)) 0) (makedir #d))
    (set #d (tackon #sup "Libs")) (if (= (exists #d (noreq)) 0) (makedir #d))
    (set #d (tackon #ins "Classes")) (if (= (exists #d (noreq)) 0) (makedir #d))
    (set #d (tackon #ins "Devs")) (if (= (exists #d (noreq)) 0) (makedir #d))
    (set #d (tackon #ins "Libs")) (if (= (exists #d (noreq)) 0) (makedir #d))
)
"""

TAKE = """; Takes one file out, if this pack put it there: what was there first comes
; back from Superseded; else the file goes.
(procedure P_TAKE
    (set #dest (tackon #dir #name))
    (set #old (tackon (tackon #sup #tag) #name))
    (set #mark (tackon (tackon #ins #tag) #name))
    (if (= (exists #mark (noreq)) 1)
        (
            (if (= (exists #old (noreq)) 1)
                (
                    (copyfiles (source #old) (dest #dir) (nogauge) (optional "force"))
                    (delete #old (optional "force" "nofail"))
                )
                (if (= (exists #dest (noreq)) 1) (delete #dest (optional "force" "nofail")))
            )
            (delete #mark (optional "force" "nofail"))
            (set #took (+ #took 1))
            (if (= #tag "Libs") (set #restart 1))
        )
    )
)
"""


def file_lines(f: dict, call: str, indent: str) -> str:
    return (f'{indent}(set #src {q(f["pack"])}) (set #dir {q(DEST[f["tag"]])}) '
            f'(set #name {q(f["name"])}) (set #tag {q(f["tag"])}) ({call})\n')


def library_version(library: dict) -> str:
    """" 47.100" from the library's $VER, or nothing."""
    v = library.get("version", "").split(" (")[0].split(" ")
    return " " + v[1] if len(v) > 1 else ""


def install_script(version: str, date: str, choices: list, always: list, library: dict | None,
                   zlib: dict | None = None) -> str:
    n = len(choices) + len(always) + (1 if library else 0)
    out = [f"""; $VER: Install-GFOpenDatatypes {version} ({date})
; GFOpenDatatypes' Installer script (the Commodore Installer of AmigaOS 3.2).
; Run it from its icon in the GFOpenDatatypes drawer. It installs the Team's
; datatypes and their descriptors{", and datatypes.library with the system classes," if library else ""}
; keeps what each file replaces in {STORE}/Superseded, and
; leaves Uninstall-GFOpenDatatypes there to put it all back. Written by
; tools/build_pack.py from what the builds made.
; MIT, Copyright (c) 2026 AmiCradle Team.

(set #version {q(version)})
(set @app-name "GFOpenDatatypes")
{COMMON}
{PUT}
(if (< (/ (getversion) 65536) 47)
    (abort "GFOpenDatatypes needs AmigaOS 3.2 or later. This Amiga has an older version."))
; The datatypes are built for the 68040 (soft float, so a 68LC040 or 68EC040 is fine).
(set #cpu (database "cpu"))
(if (= #cpu "68060") (set #cpu "68040"))
(if (<> #cpu "68040")
    (abort (cat "GFOpenDatatypes is built for a 68040 or 68060. This Amiga has a " #cpu ".")))

(set #zrestart 0)
(welcome (cat "This installs GFOpenDatatypes " #version ": the AmiCradle Team's datatypes, "
              "so every program that uses datatypes opens today's pictures, sounds, films and documents."))
(message (cat "Nothing is deleted: a file GFOpenDatatypes replaces is kept in {STORE}/Superseded, "
              "and Uninstall-GFOpenDatatypes in {STORE} puts it back.\\n\\n"
              "The datatypes marked 'by a Cradle' decode on a services card or a paired Cradle; "
              "without one they say so and don't open."))
"""]
    # What some datatypes need that the pack doesn't carry (Datatypes/README.md): said before the
    # choices, only when this Amiga lacks it, so nobody is left wondering why one doesn't work.
    classes = {c["class"] for c in choices + always}
    zneed = [] if zlib else [n for n in ZLIB_USERS if n in classes]
    sneed = [n for n in ("openpicture.datatype", "opensound.datatype", "opendoc.datatype", "openvideo.datatype")
             if n in classes]
    if zneed or sneed:
        out.append('(set #needs "")\n')
    if zneed:
        what = "gzip.datatype, and gfopenpng.datatype's PNG writing," if len(zneed) == 2 else zneed[0]
        out.append(f"""(if (= (exists "LIBS:z.library" (noreq)) 0)
    (set #needs (cat #needs {q(what + " need z.library in LIBS: (the Team's GFOpenZlib, or z.library 53 from "
                               "Aminet), which this Amiga doesn't have yet. ")})))
""")
    if sneed:
        out.append(f"""(if (= (exists "DEVS:gfopenservice.device" (noreq)) 0)
    (set #needs (cat #needs {q("The datatypes marked 'by a Cradle' need gfopenservice.device in DEVS: (GFOpenUp's "
                               "GFOpenService part) and a services card or paired Cradle; this Amiga doesn't have "
                               "gfopenservice.device yet. ")})))
""")
    if zneed or sneed:
        out.append('(if (<> #needs "") (message #needs))\n')
    pages = [choices[i:i + PAGE] for i in range(0, len(choices), PAGE)]
    for k, page in enumerate(pages, 1):
        labels = " ".join(q(c["label"]) for c in page)
        out.append(f"""(set #sel{k} (askoptions
    (prompt {q("Which datatypes?" if len(pages) == 1 else f"Which datatypes? ({k} of {len(pages)})")})
    (help @askoptions-help)
    (choices {labels})
    (default {(1 << len(page)) - 1})))
""")
    if library:
        out.append(f"""(set #lib (askbool
    (prompt {q("Install the Team's datatypes.library" + library_version(library)
               + " and its system classes (picture, ilbm, sound, 8svx, text, ascii, animation, anim), in place of AmigaOS 3.2's? "
               "They are kept, and come back with Uninstall-GFOpenDatatypes.")})
    (help @askbool-help)
    (choices "Install" "Leave AmigaOS 3.2's")
    (default 1)))
""")
    out.append("""
(working "Installing GFOpenDatatypes")
(P_DIRS)
""")
    done = 0
    for k, page in enumerate(pages, 1):
        for bit, c in enumerate(page):
            out.append(f"; {c['class']}\n(if (bitand #sel{k} {1 << bit})\n    (\n")
            for f in c["files"]:
                out.append(file_lines(f, "P_PUT", "        "))
            done += 1
            out.append(f"    )\n)\n(complete {done * 90 // n})\n")
    for c in always:
        out.append(f"; {c['class']}: the superclass of others, with every install\n")
        for f in c["files"]:
            out.append(file_lines(f, "P_PUT", ""))
    if library:
        out.append("; datatypes.library and the system classes\n(if #lib\n    (\n")
        for f in library["files"]:
            out.append(file_lines(f, "P_PUT", "        "))
        out.append("    )\n)\n")
    if zlib:
        # with the datatypes that open it: any z.library already there is kept, and Uninstall puts it back
        tests = []
        for k, page in enumerate(pages, 1):
            for bit, c in enumerate(page):
                if c["class"] in zlib["for"]:
                    tests.append(f"(bitand #sel{k} {1 << bit})")
        if any(c["class"] in zlib["for"] for c in always):
            tests = ["1"]
        cond = tests[0]
        for t in tests[1:]:
            cond = f"(or {cond} {t})"
        out.append(f"""; z.library, the Team's GFOpenZlib, for {" and ".join(zlib["for"])}
(if {cond}
    (
        (if (= (exists "LIBS:z.library" (noreq)) 1)
            (if (= (exists (tackon (tackon #ins "Libs") "z.library") (noreq)) 0) (set #zrestart 1)))
{file_lines(zlib["files"][0], "P_PUT", "        ")}    )
)
""")
    out.append(f"""
(copyfiles (source "Uninstall-GFOpenDatatypes") (dest #store) (infos) (nogauge) (optional "force"))
(run "C:AddDataTypes REFRESH QUIET >NIL:")
(complete 100)
(exit (cat "GFOpenDatatypes " #version " is installed. "
           {"(if #lib " + q("Restart the Amiga to use the new datatypes.library. ") + ' "")' if library else '""'}
           {"(if #zrestart " + q("Restart the Amiga so every program uses GFOpenZlib's z.library. ") + ' "")' if zlib else '""'}
           "To take it out, run Uninstall-GFOpenDatatypes in {STORE}."))
""")
    return "".join(out)


def uninstall_script(version: str, date: str, choices: list, always: list, library: dict | None,
                     zlib: dict | None = None) -> str:
    out = [f"""; $VER: Uninstall-GFOpenDatatypes {version} ({date})
; Takes GFOpenDatatypes off this Amiga: each file it installed (it has a mark in
; {STORE}/Installed) goes, and what it replaced comes back from
; {STORE}/Superseded. Files it never installed are left alone.
; Written by tools/build_pack.py. MIT, Copyright (c) 2026 AmiCradle Team.

(set #version {q(version)})
(set @app-name "GFOpenDatatypes")
{COMMON}
{TAKE}
(if (= (exists #ins (noreq)) 0)
    (abort "GFOpenDatatypes is not installed on this Amiga."))
(welcome "This takes GFOpenDatatypes off this Amiga and puts back what it replaced.")
(set #took 0)
(set #restart 0)
(working "Taking GFOpenDatatypes out")
"""]
    for c in choices + always:
        out.append(f"; {c['class']}\n")
        for f in c["files"]:
            out.append(file_lines(f, "P_TAKE", ""))
    if library:
        out.append("; datatypes.library and the system classes\n")
        for f in library["files"]:
            out.append(file_lines(f, "P_TAKE", ""))
    if zlib:
        out.append("; z.library (GFOpenZlib)\n" + file_lines(zlib["files"][0], "P_TAKE", ""))
    out.append(f"""
; The folders, once empty, and this script
(delete (tackon #ins "Classes") (optional "nofail"))
(delete (tackon #ins "Devs") (optional "nofail"))
(delete (tackon #ins "Libs") (optional "nofail"))
(delete #ins (optional "nofail"))
(delete (tackon #sup "Classes") (optional "nofail"))
(delete (tackon #sup "Devs") (optional "nofail"))
(delete (tackon #sup "Libs") (optional "nofail"))
(delete #sup (optional "nofail"))
(delete (tackon #store "Uninstall-GFOpenDatatypes") (optional "force" "nofail"))
(delete (tackon #store "Uninstall-GFOpenDatatypes.info") (optional "force" "nofail"))
(delete #store (optional "nofail"))
(run "C:AddDataTypes REFRESH QUIET >NIL:")
(complete 100)
(exit (cat "GFOpenDatatypes is taken out (" #took " files). "
           (if #restart "Restart the Amiga so the libraries it put back are the ones in use." "")))
""")
    return "".join(out)


# ---------------------------------------------------------------- the pack

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", default="0.1")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "pack")
    ap.add_argument("--datatypes-out", type=Path, default=ROOT / "Datatypes" / "out",
                    help="where Datatypes/build.sh put Classes/ and Devs/ (its PREFIX)")
    ap.add_argument("--library-out", type=Path, default=ROOT / "build" / "library",
                    help="the library's and its classes' build output (their OUT)")
    ap.add_argument("--zlib-out", type=Path, default=ROOT / "build" / "openzlib",
                    help="GFOpenZlib's build output (its OUT), from an amicradle-guest checkout")
    ap.add_argument("--no-build", action="store_true", help="use what the builds already made")
    ap.add_argument("--lha", action="store_true", help="also make GFOpenDatatypes-<version>.lha (jlha or lha on PATH)")
    a = ap.parse_args()

    if not a.no_build:
        build(a.library_out, a.zlib_out)
    choices, always, library, zlib = collect(a.datatypes_out, a.library_out, a.zlib_out)
    if not library:
        print("build_pack: no datatypes.library build: the pack has the datatypes only")
    date = time.strftime("%d.%m.%Y", time.gmtime(int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))))

    top = a.out / "GFOpenDatatypes"
    if top.exists():
        shutil.rmtree(top)
    top.mkdir(parents=True)

    def put(rel: str, data: bytes) -> None:
        p = top / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    extras = ([library] if library else []) + ([zlib] if zlib else [])
    for c in choices + always + extras:
        for f in c["files"]:
            put(f["pack"], f["src"].read_bytes())
    put("Install-GFOpenDatatypes", install_script(a.version, date, choices, always, library, zlib).encode("latin-1"))
    put("Uninstall-GFOpenDatatypes", uninstall_script(a.version, date, choices, always, library, zlib).encode("latin-1"))
    # NOLOG, as GFOpenUp's: the Installer's own log would stay behind after an uninstall
    for script in ("Install-GFOpenDatatypes", "Uninstall-GFOpenDatatypes"):
        put(script + ".info", amiga_icon.icon(amiga_icon.WBPROJECT, "install", "Installer",
                                              ["APPNAME=GFOpenDatatypes", "MINUSER=NOVICE", "DEFUSER=NOVICE", "NOLOG"]))
    readme = (ROOT / "pack" / "GFOpenDatatypes.readme").read_text(encoding="latin-1")
    put("GFOpenDatatypes.readme", readme.replace("@VERSION@", a.version).encode("latin-1"))
    (a.out / "GFOpenDatatypes.info").write_bytes(amiga_icon.icon(amiga_icon.WBDRAWER, "datatypes", window=(420, 200)))

    def entry(c: dict) -> dict:
        return {k: v for k, v in c.items() if k != "files"} | {"files": [
            {"pack": f["pack"], "dest": tackon(DEST[f["tag"]], f["name"]),
             "sha256": hashlib.sha256(f["src"].read_bytes()).hexdigest()} for f in c["files"]]}
    manifest = {"pack": "GFOpenDatatypes", "version": a.version, "date": date,
                "choices": [entry(c) for c in choices], "always": [entry(c) for c in always],
                "library": entry(library) if library else None, "zlib": entry(zlib) if zlib else None}
    (a.out / "GFOpenDatatypes.json").write_text(json.dumps(manifest, indent=1) + "\n")

    n = sum(len(c["files"]) for c in choices + always + extras)
    print(f"{top}: {len(choices)} datatypes to choose, {len(always)} always, "
          f"{'the library and ' + str(len(library['files']) - 1) + ' system classes, ' if library else ''}"
          f"{'z.library, ' if zlib else ''}{n} files")
    if a.lha:
        lha = shutil.which("jlha") or shutil.which("lha")
        if not lha:
            sys.exit("build_pack: --lha needs jlha or lha on PATH")
        arc = a.out / f"GFOpenDatatypes-{a.version}.lha"
        arc.unlink(missing_ok=True)
        run([lha, "aq", arc.name, "GFOpenDatatypes", "GFOpenDatatypes.info"], cwd=a.out)
        print(f"{arc} ({arc.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
