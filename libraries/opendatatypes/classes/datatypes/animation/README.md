# animation.datatype and anim.datatype

Open versions of OS 3.2's `animation.datatype` (a subclass of
datatypesclass) and `anim.datatype` (IFF ANIM, its subclass). Version
47.100; the side-by-side builds are `openanimation.datatype` and
`openanim.datatype`. Written from the NDK 3.2 headers, the autodocs, the
IFF ANIM specs (ANIM-5, ANIM-7/8) and measured behaviour only.

| File | What |
|---|---|
| `animation_core.c/.h` | Plain C core: ADTA_ attributes, ADTM_ methods, frame timing, preload cache, remap, control panel, sound offset |
| `adt_ext_amiga.h` | Amiga services: bitmaps, pens, blits, the panel's drawing, audio.device, the player process on timer.device |
| `openanimation_class.c` | animation.datatype's shell (shell code is `../text/dtc_shell.h`) |
| `../anim/anim_core.c/.h` | anim.datatype: ANIM scan, ANHD, DLTA ops 0, 5, 7, 8, ByteRun1 BODY, CAMG, EHB |
| `../anim/openanim_class.c` | anim.datatype's shell |
| `build-amiga.sh` | Builds both classes and the ABI test (stove) |
| `tests/AnimationClassABITest.c` | OS 3.2's anim.datatype next to openanim.datatype |

The qemu test is `tests/m68k/test_opendatatypes_textanim.sh`.

## Edge cases

| Case | What we do | Source |
|---|---|---|
| fps | 60 / the first delta's reltime, rounded; 10 with none | ABI test |
| Duration | From the next frame's reltime | ABI test |
| Interleave | 1: delta on frame k-1; 0, 2 and anything else: k-2; frame 1 always on frame 0 | documented / ABI test |
| XOR (ANHD bit 1) | Honoured for ops 5, 7 and 8 | documented |
| Op 7/8 long data, BytesPerRow % 4 == 2 | Last column done as a word | measured |
| ByteRun1 BODY | One stream that may cross rows | ABI test |
| Bad or short DLTA | Shows what decoded; a frame in error shows the previous frame | measured |
| Op 0 | A full BODY | documented |
| CAMG | Cleaned of bits that are not mode bits; EHB colours expanded to 64 | documented |
| ADTA_PreloadFrameCount | 4 | ABI test |
| ADTA_KeyFrame | Owned by the subclass; animation.datatype never frees it | ABI test |
| ADTM_LOADNEWFORMATFRAME | Tried first, ADTM_LOADFRAME on failure | documented |
| ADTM_STOP | Rewinds to frame 0 | measured |
| Control panel | On unless OM_NEW's tags give DTA_ControlPanel (datatypesclass always answers it, FALSE) | ABI test |
| Remap | ObtainBestPen per colour, planar to planar | measured |
| Remapping a frame | A byte of each plane at a time (8 pixels); each cache slot keeps its remapped bitmap for the next frame loaded there, after a WaitBlit | ours |
| A colour no pen could take | Shown as pen 0, not in ADTA_Allocated, never released | ours |
| ADTA_Remap, ADTA_Screen or ADTA_NumColors changed | The pens released and the frame cache loaded again (remapped afresh) | ours |
| DTA_TriggerMethods | Play, Pause, Stop, Rewind, Fast Forward (labels ours) | ABI test |
| ADTM_START/PAUSE/STOP, STM_PLAY/PAUSE/STOP from a program | As the panel's buttons: the frame told when it moved, the gadget redrawn | ours |
| Stopping the player | Waits until the process is gone (its start message comes back under Forbid), so the library can be expunged after | ours |
| Sound | Started at ticks/fps x clock/period into the sample | measured |

## Not yet

- ANIM ops 1-4 and 7J (Eric Graham's J), per-frame CMAP (alf_CMap).
- ANIM sound chunks; ANIMs from the clipboard.
- More than 8 planes, HAM remap.
- Dragging the panel's knob (a click jumps), ScrollRaster, DTM_PRINT.
- A shared audio channel; samples over 131070 bytes are cut.
- An ANIM is read whole on OM_NEW, not streamed.
- The shells and ABI test are not yet built or run on the stove; the cores
  are tested under qemu (m68k-linux-gnu-gcc 13, -O0/-O2 for 68000/68020/68040).
