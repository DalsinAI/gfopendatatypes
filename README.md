# gfopendatatypes

The AmiCradle Team's datatypes for AmigaOS 3.x on 68k, in one place. Every
decoder is a datatype, so any datatypes program (MultiView, GFOpenBench,
GFOpenBrowser) opens the formats they read.

| Datatype | Folder | What it reads |
| --- | --- | --- |
| `gfopenpicture.datatype` | `Datatypes/openpicture` | picture class (our own) |
| `gfopenpng.datatype` | `Datatypes/png` | PNG (our own decoder; writes PNG from 47.4) |
| `gfopentga.datatype` | `Datatypes/tga` | TGA (our own decoder) |
| `webp.datatype` | `Datatypes/webp` (libwebp 1.6.0) | WebP pictures |
| `webm.datatype` | `Datatypes/webm` (libvpx 1.17.0) | WebM films |
| `gfopensound.datatype` | `Datatypes/opensound` | sound class (our own) |
| `gfopenmodule.datatype` | `Datatypes/openmodule` (libxmp 4.7.3) | music modules: MOD, MED, Oktalyzer, DigiBooster, XM, S3M, IT |
| `gfopendoc.datatype` | `Datatypes/opendoc` | documents |
| `cdxl.datatype` | `Datatypes/cdxl` | CDXL films |
| `gfopenvideo.datatype` | `Datatypes/openvideo` | video class |
| `stream.datatype` | `Datatypes/stream` | the data class (GID_DATA): byte streams, read as a whole or a piece at a time |
| `gzip.datatype` | `Datatypes/gzip` | gzip and zlib data, inflated by z.library (GFOpenZlib) |
| `brotli.datatype` | `Datatypes/brotli` | Brotli data (our own decoder, RFC 7932) |

## GFOpenDatatypes: datatypes.library and the system's class datatypes

`libraries/opendatatypes/` is GFOpenDatatypes: our clean-room
`datatypes.library` 47.100 (the ROM's) and its eight class datatypes in
`libraries/opendatatypes/classes/datatypes/`: picture, ilbm, sound, 8svx,
text, ascii, animation and anim. Its README has the steps, the files and
what OS 3.2's probe measured. Build it with
`libraries/opendatatypes/build-amiga.sh` (os32-gcc16 stove; `GUEST=` an
amicradle-guest checkout adds GFOpenBench's AddDataTypes). Its m68k tests
are `tests/m68k/test_opendatatypes*.sh`. The ROM build
(amicradle-guest's `tools/openkickrom/trial-1m/build.sh`) takes it from a
checkout of this repository (`DT=`).

It came here from DalsinAI/amicradle-guest on 23 October 2026 with its
history, at the same paths. `common/include/oa_protos_begin.h`,
`oa_protos_end.h`, `libraries/common/open_arith.c` and `check_fd.py` are
copies of amicradle-guest's, at the same paths, so the library builds to
the same bytes. `common/include/odt_planar.h` is our own: bitplanes to
one byte a pixel, eight pixels at a time, shared by ilbm.datatype's BODY,
picture.datatype's V42 bitmaps and cdxl.datatype.

## Picture, sound and media datatypes

They came here from DalsinAI/gfopenimage on 23 October 2026, with their
history, unchanged: the same files at the same paths, so they build to the
same bytes. Building, the descriptors and the test results are in
[Datatypes/README.md](Datatypes/README.md).

## Building

With the os32-gcc16 stove (68040, soft float through
`build/m68k-soft-m68040.specs`):

```
./Datatypes/build.sh
```

The datatypes land in `Datatypes/out/Classes/DataTypes` and their
descriptors in `Datatypes/out/Devs/DataTypes` (set `PREFIX` to change that).
`SOURCES` lists the upstream tarballs (libwebp, libvpx, libxmp) with their
SHA-256; `tools/fetch-tarball.sh` fetches them. `achelpers/` is AC090's
native helpers for `Datatypes/common/dtlib.c` (`AC_HELPERS`, see
`Datatypes/build.sh`), built from amicradle-guest at the commit in
`AMICRADLE_GUEST_PINNED_COMMIT`.

The ROM's datatypes (gfopenpicture, gfopenpng, gfopentga, gfopensound, gfopendoc in
ROM:Classes/DataTypes) and datatypes.library are built from a checkout of this repository by
amicradle-guest's `tools/openkickrom/trial-1m/build.sh` (`DT=`).

## Tests

- `sh Datatypes/tests/test-tga.sh` and `sh Datatypes/tests/test-cdxl.sh`:
  the decoders on the host under AddressSanitizer and UBSan (CI runs both).
- `sh Datatypes/tests/test-webm.sh`: webm.datatype's reader and dither
  against crafted picture sizes, under the same sanitizers.
- `sh Datatypes/tests/test-webm-read.sh`: webm.datatype's reader through its
  read cache gives the same frames as straight reads, under the same
  sanitizers.
- `sh tests/achelpers/test_achelpers.sh`: the AC090 helpers and dtlib.c's
  memory functions under qemu-m68k.
- `sh Datatypes/tests/test-webpenc.sh` and `test-webp060.sh`: webp.datatype's
  encoder; and the 68060 patch's sums against libwebp's own and the
  encoder's files with and without it (with the instructions it runs under
  qemu-m68k), on the host and as 68040 code.
- `sh Datatypes/tests/test-planar.sh`: `common/include/odt_planar.h`
  against the bit-at-a-time loop it replaced, under AddressSanitizer and
  UBSan (and as 68040 code with `QEMU_M68K=qemu-m68k`).
- `Datatypes/tests/dtpic.c`, `dtsound.c`, `dtanim.c`: the Amiga-side checks.
- `python3 -I tests/pack/test_pack.py`: the pack, built from stand-ins and its
  Installer scripts run on a stand-in AmigaOS 3.2 disk (CI runs it).

## The pack: GFOpenDatatypes, with an Installer

```
python3 tools/build_pack.py --version 0.1 --lha
```

builds everything here (the datatypes, datatypes.library with its system
classes, and z.library, the Team's GFOpenZlib, from an amicradle-guest
checkout: `AMICRADLE_GUEST`, else beside this one) and lays it out as
`build/pack/GFOpenDatatypes`, a drawer to run the Installer from, and
`GFOpenDatatypes-0.1.lha` (jlha or lha on PATH). `--no-build` takes what the
builds already made.

- `Install-GFOpenDatatypes`, a Commodore Installer script, needs AmigaOS 3.2
  and a 68040 or 68060 and nothing else. Every datatype is a choice, and
  datatypes.library with its eight system classes is one more, ticked; a
  Novice user gets them all. z.library (GFOpenZlib) goes in with gzip and
  gfopenpng, which open it. AddDataTypes REFRESH runs at the end.
- Nothing is deleted: what a file replaces is kept in
  `SYS:Storage/GFOpenDatatypes/Superseded`, and each file the pack put in has
  a mark in `SYS:Storage/GFOpenDatatypes/Installed`.
- `Uninstall-GFOpenDatatypes`, left in `SYS:Storage/GFOpenDatatypes`, takes out
  only what the pack put in and puts back what it replaced.

Both scripts are written by `tools/build_pack.py` from what the builds
made: a descriptor goes with the class its DTHD names, so a new datatype
needs no change there (`LABELS` gives it a friendlier name in the
Installer). `build/pack/GFOpenDatatypes.json` lists every file, where it
goes and its SHA-256; GFOpenUp's GFOpenImage part takes its files from it.

## Licence

Our code is MIT (see LICENSE). libwebp and libvpx keep their BSD licences
with the WebM patent grant, libxmp its MIT licence: their notices are in
`upstream/`. Who wrote what is in CONTRIBUTORS.md.

Designed, tested and delivered by The AmiCradle Team.
Copyright (c) 2026 AmiCradle Team
