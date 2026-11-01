# GFOpen8SVX: an open 8svx.datatype

8svx.datatype 47.100 for AmigaOS 3.2, a sound.datatype subclass, API and
ABI compatible, clean-room (from the public IFF 8SVX specification, the
sound_dtc and datatypes autodocs and NDK 3.2's layouts; never from
Kickstart, Hyperion or AROS code, nor any third-party datatype). The
family's shell, env and build are in [`../picture`](../picture/README.md).

| File | What it is |
| --- | --- |
| `svx_core.cpp`, `.h` | The class, lean C++ with no Amiga headers. |
| `open8svx_dtclass.c`, `8svx.lvo` | Its shell and its one call, ObtainEngine at -30. |
| `build-amiga.sh` | `8svx.datatype`, `gfopen8svx.datatype` (over `gfopensound.datatype`) and the ABI test. |
| `tests/8SVXDTClassABITest.c` | 8SVX files made by the test, through OS 3.2's class and ours. |

## What it does

At OM_NEW it parses the IFFHandle (`DTA_Handle`) with iffparse: VHDR, CHAN,
NAME, AUTH, ANNO and (c) as properties, stopping at the BODY. It takes the
first octave (oneShotHiSamples + repeatHiSamples, or the whole BODY when
both are 0), unpacks Fibonacci delta (sCmpFibDelta: a pad byte, the start
value, then two 4-bit codes a byte), splits a stereo BODY (CHAN 6: left,
then right, each packed on its own) and hands the samples to
sound.datatype (which frees them) with the VoiceHeader, length, rate,
volume (VHDR's Fixed volume over 64) and cycles. DTM_WRITE, DTWM_IFF or
DTWM_RAW, is sound.datatype's 8SVX writer.

## Edge cases

| Case | What we do | Known from |
| --- | --- | --- |
| VHDR asks for more than the BODY has | what there is | ABI test |
| oneShot + repeat 0 | the whole BODY | ABI test |
| A repeat part | played once with the one-shot part (cycles 1) | ABI test |
| Octaves after the first | ignored | documented (first octave) |
| CHAN LEFT (2) or RIGHT (4) | `SDTA_Pan` 0 or 0x10000 | ABI test |
| VHDR rate 0 | 8000 | ABI test |
| Volume over 1.0 | 64 | ABI test |
| sCompression 2 and up | DTERROR_UNKNOWN_COMPRESSION | ABI test |
| No VHDR | DTERROR_INVALID_DATA | ABI test |
| No BODY | DTERROR_NOT_ENOUGH_DATA | ABI test |
| An odd stereo BODY | each half rounded down | ABI test |

## Not yet

- Exponential delta and other compressions; ATAK/RLSE envelopes; 16SV.

## Tests

`sh tests/m68k/test_opendatatypes_picsound.sh` (14 8SVX cases, sample by
sample, played, drawn, clicked, written and read back); see
[`../picture`](../picture/README.md).
