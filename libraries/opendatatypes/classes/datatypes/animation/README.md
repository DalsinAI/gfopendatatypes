# animation.datatype and anim.datatype

Open versions of OS 3.2's `animation.datatype` (a subclass of
datatypesclass) and `anim.datatype` (IFF ANIM, its subclass). Version
47.100; the side-by-side builds are `gfopenanimation.datatype` and
`gfopenanim.datatype`. Written from the NDK 3.2 headers, the autodocs, the
IFF ANIM specs (ANIM-5, ANIM-7/8) and measured behaviour only.

| File | What |
|---|---|
| `animation_core.c/.h` | Plain C core: ADTA_ attributes, ADTM_ methods, frame timing, preload cache, remap, control panel, sound offset, and the IFF ANIM writer (`adt_anim_write`) |
| `mp4_core.c/.h` | The MP4 writer (`adt_mp4_write`): frames to 4:2:0 YUV (palette, EHB, HAM6, HAM8), the MP4 file (ftyp, mdat, moov; avc1 and `twos` tracks). Plain C, our own |
| `mp4_h264.c/.h`, `h264/minih264e.h` | The H.264 encoder: minih264e (CC0, `../../../../../upstream/minih264/`) built for the class, and our patches to it |
| `extra-cores` | The core's other files, for `../text/build-family.sh` |
| `adt_ext_amiga.h` | Amiga services: bitmaps, pens, blits, the panel's drawing, audio.device, the player process on timer.device |
| `openanimation_class.c` | animation.datatype's shell (shell code is `../text/dtc_shell.h`) |
| `../anim/anim_core.c/.h` | anim.datatype: ANIM scan, ANHD, DLTA ops 0, 5, 7, 8, ByteRun1 BODY, CAMG, EHB |
| `../anim/openanim_class.c` | anim.datatype's shell |
| `build-amiga.sh` | Builds both classes and the ABI test (stove) |
| `tests/AnimationClassABITest.c` | OS 3.2's anim.datatype next to gfopenanim.datatype |

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

## Saving (DTM_WRITE)

`DTWM_IFF` writes any animation, whatever its subclass, as an IFF ANIM:
the frames are asked for with `ADTM_LOADNEWFORMATFRAME` (else
`ADTM_LOADFRAME`) one at a time and given back after, the player waiting
on the object's lock meanwhile. The first frame is a FORM ILBM (BMHD,
CMAP from `ADTA_ColorRegisters`, CAMG from `ADTA_ModeID`, a ByteRun1
BODY); each later one an ANHD and a DLTA in method 5 (byte vertical
delta, stored, not XORed) against the frame two before (interleave 0, as
double-buffered players draw; frame 1 against frame 0), each frame
`60 / ADTA_FramesPerSecond` jiffies. A column that would need more than
255 ops is written with the fewest instead. The FORM's size is written
last, by a Seek back, so the file must be seekable. `DTWM_RAW` is left to
the superclass (the file as it came) or the subclass's own writer.
`DTM_WRITE` is in `DTA_Methods`, so MultiView offers Save As.

`tests/m68k/test_opendatatypes_animwrite.sh` (5 November 2026, qemu-m68k,
-O0 and -O2): 243 films of 1x1 to 640x512, 1 to 8 planes and 1 to 12
frames (noise, a moving box, still frames, runs, whole frames flipping,
and 1,200 and 3,000-row films whose columns need more than 255 ops) are
written and read back by anim.datatype's own reader, every frame forward,
backward and at random, with the size, CMAP, CAMG and fps: 7,165 checks,
none failed. FFmpeg's IFF ANIM decoder reads all 243 with every frame.

Not yet: sound in the ANIM, per-frame colours (`alf_CMap`), more than 8
planes, and writing to a stream that can't Seek.

## Saving as MP4

`DTWM_RAW` with `OIA_WriteFormat` `OIWF_MP4` in `dtw_AttrList`
(`Datatypes/include/datatypes/gfopenimage.h`; the reasons are in
`docs/DESIGN.md`, section 1) writes any animation, whatever its subclass
(anim, cdxl, webm, gfopenvideo, an animated GIF), as an MP4, here on this
CPU: the Team's ruling of 6 November 2026, "mp4 we merge in". No tag, or
0, is the subclass's own raw write as before; any other format fails with
`ERROR_NOT_IMPLEMENTED`.

- **Frames** are asked for as for the ANIM (`ADTM_LOADNEWFORMATFRAME`, else
  `ADTM_LOADFRAME`, given back at once, the player waiting on the lock).
  Their planes (up to 8) go through `ADTA_ColorRegisters`, or the frame's
  own `alf_CMap` (read with `GetRGB32`), with Extra Half-Brite (`ADTA_ModeID`'s
  EHB bit, six planes) and HAM6 and HAM8 (its HAM bit; a row starts from
  colour 0, a modified gun repeats its bits, as FFmpeg's ILBM reader does),
  into BT.601 studio-range 4:2:0 YUV. An odd width or height gains a copy of
  its last column or row (H.264 codes 4:2:0 in pairs); the SPS crops the
  rest of the macroblocks off.
- **Video**: H.264 Constrained Baseline in all but name (profile 66, CAVLC,
  one slice and one reference frame), from minih264e at a fixed quantiser:
  `OIA_Quality` 0 to 100 (QP 51 to 10; default 60, QP 26) and
  `OIA_Compression` 0 (the fastest, minih264e's speed 10: whole-pixel
  motion, no deblocking), 1 (deblocking), 2 (quarter-pixel motion, no
  deblocking), 3 to 7 (both; the same today), 8 (and 4x4 intra blocks in
  P frames) and 9 (and partitions of a macroblock: minih264e's speed 0);
  default 0: on a 68k the fastest is the one to use (below). A key frame every
  five seconds (at most 250 frames). Each frame lasts its `alf_Duration`
  ticks of `ADTA_FramesPerSecond` (stts), so a GIF's own delays survive.
- **Sound**, unless `OIA_WriteSound` is FALSE: the film's `ADTA_Sample`
  (or `ADTA_LeftSample` and `ADTA_RightSample`) from frame 0, a frame's
  share in each chunk; else each frame's own `alf_Sample` (or, V44, its
  left and right) as it plays, at `alf_SamplesPerSec` or the clock over
  `alf_Period`. Written as it is, 8-bit signed PCM (`twos`, mono or stereo,
  at the first rate given): FFmpeg, VLC and QuickTime play it; browsers
  don't, so a film for the web is saved with `OIA_WriteSound` FALSE. AAC is
  not in the pack: the ruling leaves it to the service's FFmpeg.
- **The file**: `ftyp` (isom), `mdat` (each frame's access unit, its SPS and
  PPS moved to the `avcC`, then its sound), `moov` last. The mdat's size is
  written last by a Seek back, as the ANIM's FORM, so the file must be
  seekable; up to 4 GB.
- **Memory**: the encoder's state and scratch (98 KB for 160 x 128, 340 KB
  for 320 x 256, 1.2 MB for 640 x 480), a frame of YUV padded to whole
  macroblocks, 21 bytes a frame for the tables; any memory, no Chip RAM. The encoder runs on a
  stack of 32 KB of its own when the caller has less free (exec's
  `StackSwap`).
- **This CPU only**: integer arithmetic, no FPU, no 64-bit multiplies or
  divides (`test_opendatatypes_mp4write.sh` counts them in the m68k build
  and the stove's). The cores board and the service are not used yet.

`tests/m68k/test_opendatatypes_mp4write.sh` (7 November 2026, qemu-m68k,
-O0 and -O2): 43 films and a mock object's DTM_WRITE (two more MP4s), 827
checks under qemu-m68k (each frame's YUV against the test's own reading of
the planes, the boxes and tables against the frames and the sound) and 562
outside it, none failed: FFprobe reads every file as one H.264 Baseline
stream with every frame and its rate (and 8-bit PCM where there is sound);
FFmpeg decodes every frame (luma PSNR against what was encoded 34 to 57 dB
at the default quality, the worst frame of each film), its sound byte for
byte; FFmpeg's ANIM reader sees the same pixels in the same frames written
as an ANIM (HAM6, HAM8, EHB included); and the encoder built on the host
makes the m68k build's NAL units byte for byte. The cores have no FPU
instructions and no 64-bit multiplies or divides, in the m68k-linux build
and the os32-gcc16 stove's.

**Speed**, measured: 68k instructions a 320 x 256 frame under qemu-m68k
(-O2 -m68040, `COUNT=1 sh tests/m68k/test_opendatatypes_mp4write.sh`; the
YUV conversion included, the test's own making of the film taken off):

| Film | `OIA_Compression` | First frame (and the encoder's start) | Each frame after |
| --- | --- | --- | --- |
| a box over stripes, 5 planes | 0 (default) | 27.6 M | 16.7 M |
| the same | 1 | 33.4 M | 16.9 M |
| the same | 2 | 27.6 M | 43.6 M |
| the same | 9 | 33.4 M | 113.2 M |
| bands of colour sliding, 5 planes | 0 | 39.6 M | 20.9 M |
| HAM8 | 0 | 40.8 M | 29.4 M |

The YUV conversion alone is 2.24 M a frame through a palette (27
instructions a pixel) and 6.88 M for HAM8. Estimates, not measurements:
at 1.5 to 2.5 cycles an instruction a 25 MHz 68040 takes 1.0 to 1.7 s for
a frame of the first film at level 0, a 50 MHz 68060 about half that; a
second of PAL lowres film at 25 frames, half a minute or more. Level 2
and up (quarter-pixel motion search) cost 2.6 to 7 times as much for
files no smaller on films like these, so 0 is the default.

`dtsave SRC DST MP4` (`Datatypes/tests/dtsave.c`) saves a film as an MP4
on an Amiga and reads its boxes back; FFmpeg on a host decodes it.

## Not yet

- ANIM ops 1-4 and 7J (Eric Graham's J), per-frame CMAP (alf_CMap).
- ANIM sound chunks; ANIMs from the clipboard.
- More than 8 planes, HAM remap.
- Dragging the panel's knob (a click jumps), ScrollRaster, DTM_PRINT.
- A shared audio channel; samples over 131070 bytes are cut.
- An ANIM is read whole on OM_NEW, not streamed.
- The shells and ABI test are not yet built or run on the stove; the cores
  are tested under qemu (m68k-linux-gnu-gcc 13, -O0/-O2 for 68000/68020/68040).
