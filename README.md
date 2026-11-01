# opendatatypes

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
amigachrome-guest checkout adds GFOpenBench's AddDataTypes). Its m68k tests
are `tests/m68k/test_opendatatypes*.sh`. The ROM build
(amigachrome-guest's `tools/openkickrom/trial-1m/build.sh`) takes it from a
checkout of this repository (`DT=`).

It came here from DalsinAI/amigachrome-guest on 23 October 2026 with its
history, at the same paths. `common/include/oa_protos_begin.h`,
`oa_protos_end.h`, `libraries/common/open_arith.c` and `check_fd.py` are
copies of amigachrome-guest's, at the same paths, so the library builds to
the same bytes.

## Picture, sound and media datatypes

They came here from DalsinAI/openamigaimage on 23 October 2026, with their
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
`Datatypes/build.sh`), built from amigachrome-guest at the commit in
`AMIGACHROME_GUEST_PINNED_COMMIT`.

The ROM's datatypes (gfopenpicture, gfopenpng, gfopentga, gfopensound, gfopendoc in
ROM:Classes/DataTypes) and datatypes.library are built from a checkout of this repository by
amigachrome-guest's `tools/openkickrom/trial-1m/build.sh` (`DT=`).

## Tests

- `sh Datatypes/tests/test-tga.sh` and `sh Datatypes/tests/test-cdxl.sh`:
  the decoders on the host under AddressSanitizer and UBSan (CI runs both).
- `sh Datatypes/tests/test-webm-read.sh`: webm.datatype's reader through its
  read cache gives the same frames as straight reads, under the same
  sanitizers.
- `sh tests/achelpers/test_achelpers.sh`: the AC090 helpers and dtlib.c's
  memory functions under qemu-m68k.
- `Datatypes/tests/dtpic.c`, `dtsound.c`, `dtanim.c`: the Amiga-side checks.

## Licence

Our code is MIT (see LICENSE). libwebp and libvpx keep their BSD licences
with the WebM patent grant, libxmp its MIT licence: their notices are in
`upstream/`. Who wrote what is in CONTRIBUTORS.md.

Designed, tested and delivered by The AmigaChrome Team.
Copyright (c) 2026 AmigaChrome Team
