# GFOpenPicture: an open picture.datatype

picture.datatype 47.100 for AmigaOS 3.2, API and ABI compatible, clean-room
(written from the picture_dtc autodoc, `datatypes/pictureclass.h`'s
layouts in NDK 3.2, the public IFF ILBM specification and OS 3.2's answers
measured by `tests/PictureDTClassABITest`; never from Kickstart, Hyperion
or AROS code, nor any third-party datatype). Step D4 of
[`../../../README.md`](../../../README.md), with its subclass
[`../ilbm`](../ilbm) and the sound family ([`../sound`](../sound),
[`../8svx`](../8svx)).

## The family's files

| File | What it is |
| --- | --- |
| `picture_core.cpp`, `.h` | The class, lean C++ with no Amiga headers: attributes, pixel arrays, layout (remap, scaling, mask), drawing, DTM_FRAMEBOX, the ILBM writer. |
| `dtc_env.h` | What the picture and sound families ask of the system beyond dt-d2's `struct odt_cenv` (`../../../dtclass_env.h`): `struct dtc_ext`, the `class_ext` with graphics, pens and the audio player. |
| `dtc_shell.h` | The C around every core of the family: RomTag, vectors (Open, Close, Expunge, Null, ObtainEngine at -30), MakeClass and AddClass under the superclass, the env from exec, dos, intuition, graphics, iffparse and audio.device (the player process). |
| `openpicture_dtclass.c`, `picture.lvo` | This class's shell and its one call. |
| `ndk_check.c` | Every number and layout the family's cores carry, against the NDK 3.2 headers (compiled by `build-class.sh`). |
| `build-class.sh`, `build-amiga.sh` | With the os32-gcc16 stove: the NDK check, `picture.datatype`, `gfopenpicture.datatype` (side by side, its subclasses' superclass there too) and the ABI test. |
| `tests/PictureDTClassABITest.c`, `tests/dtc_abitest.h` | OS 3.2's class and ours side by side, and the family's harness; PROBE prints OS 3.2's answers. |

## What it does

- Keeps the source as pixels: LUT8 (an index a byte, into `PDTA_CRegs`) or
  ARGB, chosen by the first `PDTM_WRITEPIXELARRAY` (LUT8 and GREY8 into a
  picture of 8 planes or fewer stay indexes; anything else is ARGB). A V42
  subclass's `PDTA_BitMap` is read into LUT8 pixels at layout.
- `PDTM_READPIXELARRAY` in RGB, RGBA, ARGB, LUT8 (index pictures only) and
  GREY8; `PDTM_WRITEPIXELARRAY` from every format into either store (an RGB
  pixel into an index picture takes the nearest colour).
- Layout (`DTM_PROCLAYOUT`, `DTM_ASYNCLAYOUT`, `ObtainDTDrawInfoA`) makes the
  destination: scaled by `PDTM_SCALE` (nearest pixel, `PScale_KeepAspect`),
  remapped with `ObtainBestPen` at `OBP_Precision` (shared pens, released at
  the next layout and at dispose; `PDTA_SparseTable` colours only, the rest
  to the nearest of them), true colour on a screen deeper than 8 planes when
  `PDTA_DestMode` is `PMODE_V43`, a cube of pens for a true colour picture on
  a screen of pens; and `PDTA_MaskPlane` from the mask, the transparent
  colour or alpha. `PDTA_GRegs`, `ColorTable`, `ColorTable2`, `Allocated`,
  `NumAlloc` follow the layout.
- `GM_RENDER` and `DTM_DRAW` blit the destination (through the mask when
  there is one) from `DTA_TopHoriz`/`DTA_TopVert`.
- `DTM_WRITE` (`DTWM_IFF`) writes ILBM: BMHD, CMAP (with `BMHDF_CMAPOK`),
  CAMG (no HAM bit for index pixels), BODY in ByteRun1 a plane row at a
  time; a true colour picture as a 24-plane deep ILBM.

## Edge cases

| Case | What we do | Known from |
| --- | --- | --- |
| `PDTA_ModeID` before a subclass sets one | `INVALID_ID` | ABI test |
| `PDTA_DestMode` default | `PMODE_V42` (V42 programs get a bitmap of pens) | ABI test |
| `PDTM_WRITEPIXELARRAY` before the BitMapHeader has a size | fails (0) | ABI test |
| A rectangle partly outside the picture | clipped; wholly outside: 0 | ABI test |
| GREY8 into an index picture | the byte is the index | ABI test |
| LUT8 read of a true colour picture | fails (0) | ABI test |
| Alpha of an index pixel read as ARGB/RGBA | the mask's, 0 for the transparent colour, else 0xff | ABI test |
| Alpha of an RGB/LUT8/GREY8 write into a true colour picture | 0xff | ABI test |
| GREY8 weights | (77 R + 151 G + 28 B) / 256 | ABI test |
| A colour no pen could take (`ObtainBestPen` -1) | pen 0 | ABI test |
| True colour onto a screen of pens | a cube of 216/125/64/27/8 pens by depth (8/7/6/5/fewer planes), nearest, no dither | ABI test (OS 3.2 may dither) |
| `PMODE_V42` on a deep screen | 8 planes of pens | ABI test |
| Mask cut of mskHasAlpha | alpha 0x80 and up is opaque | ABI test |
| Mask plane row length | the destination bitmap's BytesPerRow (AllocBitMap may round up); RASSIZE on a friend bitmap | ABI test |
| `PDTA_MaskPlane` set by a subclass | taken as the source's mask (OS 3.2 documents it read only) | ABI test |
| `PDTA_BitMap` asked of a V43 picture | one made at the first ask, of `bmh_Depth` planes, freed with the object | ABI test |
| `DTM_WRITE` with `DTWM_RAW` on picture.datatype itself | fails, `ERROR_NOT_IMPLEMENTED` | ABI test |
| ILBM aspect when the BitMapHeader has none | 1:1 | ABI test |
| `DTM_FRAMEBOX` flags | DIPF_IS_LACE/HAM/EXTRAHALFBRITE from the mode; 8 bits a gun; FIF_REMAPPABLE, FIF_SCALABLE | ABI test |
| `OBP_Precision` passed to NewDTObject (bitmap.image does) | taken as the pens' precision | ABI test |
| `PDTA_UseFriendBitMap` default | TRUE | documented (V43 autodoc); ABI test probes it |
| `PDTA_FreeSourceBitMap` | the source bitmap freed as soon as a layout has the pixels read from it | documented |
| A mask that cannot be allocated | the layout fails (ERROR_NO_FREE_STORE), the destination freed | ours |
| `PDTM_SCALE` or a picture past 65535 pixels a side | refused (a BitMap's sizes are 16 bits); 4-byte pixel buffers checked before multiplying | ours |
| `DTM_OBTAINDRAWINFO` | lays out and holds `si_Lock` until the matching `DTM_RELEASEDRAWINFO` (a layout waits) | ours |
| `GM_LAYOUT` | passed to datatypesclass, which runs `DTM_ASYNCLAYOUT` (dt-d2); the picture is made there or at `DTM_PROCLAYOUT` | documented |

## Not yet

- `PDTA_DitherQuality`, `PDTA_MaxDitherPens`, `PDTA_ScaleQuality`: kept by
  no one yet (nearest colour, nearest pixel).
- `DTM_COPY` to the clipboard, `DTM_PRINT`: datatypesclass's (dt-d2).
- Multi-picture files (`PDTA_WhichPicture` is 0, `PDTA_GetNumPictures` 1).
- True colour destinations need cybergraphics.library's header in the
  stove's NDK; without it a deep screen gets no pixels (`write_argb` NULL).
- The shells and ABI tests are built only with the os32-gcc16 stove (not
  in the cloud); only the cores have run, under qemu.

## Tests

- `sh tests/m68k/test_opendatatypes_picsound.sh` (from the repository
  root): all four cores at -O0 and -O2 for the 68000, 68020 and 68040,
  m68k-linux-gnu-g++ 13.3 and qemu-m68k, against
  `opendatatypes_picsound_ref.py`'s own models; nm checks that each core
  calls nothing but libgcc's arithmetic and holds no C++ runtime and no
  data.
- `PictureDTClassABITest PROBE` on a lab Amiga: OS 3.2's answers;
  `PictureDTClassABITest LIB=dir`: ours against them.
