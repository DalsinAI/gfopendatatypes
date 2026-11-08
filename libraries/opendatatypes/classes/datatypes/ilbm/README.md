# GFOpenILBM: an open ilbm.datatype

ilbm.datatype 47.101 for AmigaOS 3.2, a picture.datatype subclass, API and
ABI compatible, clean-room (from the public IFF ILBM specification and its
updates, the picture_dtc and datatypes autodocs and NDK 3.2's layouts;
never from Kickstart, Hyperion or AROS code, nor any third-party
datatype). The family's shell, env and build are in
[`../picture`](../picture/README.md).

| File | What it is |
| --- | --- |
| `ilbm_core.cpp`, `.h` | The class, lean C++ with no Amiga headers. |
| `openilbm_dtclass.c`, `ilbm.lvo` | Its shell (`../picture/dtc_shell.h`) and its one call, ObtainEngine at -30. |
| `build-amiga.sh` | `ilbm.datatype`, `gfopenilbm.datatype` (over `gfopenpicture.datatype`) and the ABI test. |
| `tests/ILBMDTClassABITest.c` | ILBM and PBM files made by the test, through OS 3.2's class and ours. |

## What it does

At OM_NEW it parses the IFFHandle datatypesclass opened (`DTA_Handle`)
with iffparse: BMHD, CMAP, CAMG, GRAB, NAME, AUTH, ANNO and (c) as
properties, stopping at the BODY of a FORM ILBM or PBM. It fills the
picture's BitMapHeader and colours, sets `PDTA_ModeID`, `PDTA_NumColors`,
`PDTA_SourceMode` (V43), `DTA_NominalHoriz/Vert`, `PDTA_Grab` and the
`DTA_Obj` strings, and hands the pixels over a row at a time with
`PDTM_WRITEPIXELARRAY`: LUT8 for index pictures, ARGB for HAM6, HAM8 and
deep (24 and 32 plane) ILBMs. The BODY is unpacked a row at a time
(ByteRun1 runs may cross rows and planes; -128 is skipped). A mask plane (mskHasMask) goes to
the picture as `PDTA_MaskPlane`. DTM_WRITE, DTWM_IFF or DTWM_RAW, is
picture.datatype's ILBM writer.

## Edge cases

| Case | What we do | Known from |
| --- | --- | --- |
| CMAP of 4-bit guns (every low nibble 0, BMHDF_CMAPOK clear) | scaled: v \| v >> 4 | documented (ILBM update) |
| CMAP shorter than 2^depth | the rest black | ABI test |
| No CMAP, 8 planes or fewer | a grey ramp | ABI test |
| EHB (CAMG EXTRA_HALFBRITE, 6 planes) | 64 colours, 32-63 half of 0-31 | documented |
| No CAMG | HIRES from 640 wide, LACE from 400 high; EHB when 6 planes and a CMAP of 32 | ABI test |
| A CAMG of the old 16-bit kind | SPRITES, VP_HIDE, GENLOCK_AUDIO and GENLOCK_VIDEO dropped | documented (ILBM update) |
| HAM with a depth other than 6 or 8; EHB not 6 | the bit dropped, index pixels | ABI test |
| HAM6 modify | the 4 bits doubled up (v * 17) | ABI test |
| HAM8 modify | the 6 bits are the gun's upper six, the lower two kept | documented |
| HAM row start | colour 0 | documented |
| 32 planes | alpha in planes 24-31, masking mskHasAlpha | ABI test |
| Deep ILBMs other than 24 and 32 planes; PBM other than 8 | DTERROR_INVALID_DATA | ABI test |
| BODY shorter than the picture | the rest 0 | ABI test |
| No BMHD | DTERROR_INVALID_DATA | ABI test |
| No BODY | DTERROR_NOT_ENOUGH_DATA | ABI test |
| Compression other than 0 and 1 | DTERROR_UNKNOWN_COMPRESSION | documented |
| mskLasso | read as no mask | ABI test |
| Odd PBM width | rows padded to even | documented |
| A BMHD whose BODY size passes 32 bits (e.g. 32768x32768x32) | ERROR_NO_FREE_STORE before any allocation | ours |

## Not yet

- ByteRun2 (cmpByteRun2), DPI and DEST/SPRT chunks (ignored), CRNG/CCRT
  colour cycling.
- Writing a mask plane or alpha (the writer writes the pixels and the
  transparent colour only).

## Tests

`sh tests/m68k/test_opendatatypes_picsound.sh` (32 ILBM and PBM cases,
pixel by pixel, each laid out, drawn, written and read back); see
[`../picture`](../picture/README.md).
