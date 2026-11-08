# GFOpenSound: an open sound.datatype

sound.datatype 47.100 for AmigaOS 3.2, API and ABI compatible, clean-room
(from the sound_dtc autodoc, `datatypes/soundclass.h`'s layouts in NDK
3.2, the audio.device autodoc and OS 3.2's answers measured by
`tests/SoundDTClassABITest`; never from Kickstart, Hyperion or AROS code).
The family's shell, env and build are in [`../picture`](../picture/README.md).

| File | What it is |
| --- | --- |
| `sound_core.cpp`, `.h` | The class, lean C++ with no Amiga headers. |
| `opensound_dtclass.c`, `sound.lvo` | Its shell (`../picture/dtc_shell.h`, with the audio player process) and its one call, ObtainEngine at -30. |
| `build-amiga.sh` | `sound.datatype`, `gfopensound.datatype` and the ABI test. |
| `tests/SoundDTClassABITest.c` | Sounds made in memory, through OS 3.2's class and ours; PLAY plays them. |

## What it does

- Every SDTA_ attribute of V44: the sample (`SDTA_Sample`, or
  `SDTA_LeftSample` and `SDTA_RightSample`), length, period and
  `SDTA_SamplesPerSec` (each sets the other from the audio clock, PAL or
  NTSC), volume (0-64), cycles, the signal task with `SDTA_SignalBit` (a
  mask, V44) or `SDTA_SignalBitNumber`, `SDTA_ReplayPeriod`, `SDTA_Pan`,
  `SDTA_FreeSampleData`, `SDTA_Continuous`, `SDTA_SyncSampleChange`; and the
  sample-type extra `SDTA_SampleType` (8 or 16 bits, mono or interleaved
  stereo).
- To play (`DTM_TRIGGER` STM_PLAY, a click, `DTA_Immediate` at the first
  layout) it copies the sample into chip memory as 8 bits and makes a plan
  for audio.device: the channel sets to ask for (stereo: 3, 5, 10, 12;
  mono: any one channel, or by `SDTA_Pan` the left pair, the right pair,
  or both for the centre), then CMD_WRITE requests of at most 131070 bytes
  at the period, volume and cycles (one request: its cycles; more: the
  whole sequence `cycles` times; 0 is for ever, as is `DTA_Repeat`). The
  shell's player process carries it out with ADIOF_PERVOL and signals the
  task at the end; STM_STOP aborts it and waits until it has. The player
  (`../picture/dtc_audio.h`) is double buffered: two IORequests a channel,
  the next segment queued behind the one playing, so a long sample plays
  without a gap at each 131,070-byte segment's end.
- The chip copy is made once (a loop for each sample layout: 8 or 16
  bits, interleaved or not) and kept for the next play; the waveform's
  columns are worked out once for the gadget's width. Both are made again
  when the sound's attributes are set (`SDTA_Sample`, `SDTA_LeftSample`,
  `SDTA_RightSample`, `SDTA_SampleLength`, `SDTA_SampleType`), even to the
  same values: a program that changes the samples where they are sets
  `SDTA_Sample` again.
- As a gadget it draws the waveform (each column's lowest and highest
  sample) and, with `DTA_ControlPanel`, a play and a stop button under it.
- `DTM_WRITE` (`DTWM_IFF`) writes 8SVX: VHDR, CHAN 6 for stereo, BODY.

## Edge cases

| Case | What we do | Known from |
| --- | --- | --- |
| Defaults | volume 64, cycles 1, pan 0x8000, `SDTA_FreeSampleData` TRUE | ABI test |
| A period under 124 (the DMA's limit) | played at 124 | ABI test |
| Volume over 64 | 64 | ABI test |
| No rate and no period | 8000 samples a second | ABI test |
| An odd length | padded with a 0 to even | documented (audio.device takes words) |
| A sample longer than the plan holds (64 requests) | cut | ABI test |
| `SDTA_Pan` on a mono sample | under 0x8000 the left channels, over the right, 0x8000 both | ABI test |
| STM_PAUSE / STM_RESUME | stop / play from the start | ABI test |
| `DTA_Repeat` | cycles 0, for ever | ABI test |
| 16-bit samples | their high byte | ABI test |
| Signal when stopped | none, only at the natural end | ABI test |
| `SDTA_SampleType` | taken; ABI test whether OS 3.2 knows it | ABI test |
| `DTM_WRITE` `DTWM_RAW` on sound.datatype itself | fails, `ERROR_NOT_IMPLEMENTED` | ABI test |
| The control panel's height | 14 pixels | ABI test |
| Player priority, allocation precedence | 21, 0 | ABI test |
| `SDTA_SampleLength` set to 0 while it plays | the replay stopped before OM_SET returns | documented |
| `SDTA_VoiceHeader` set | copied; its `vh_SamplesPerSec` (when not 0) and `vh_Volume` taken as rate and volume (tags after it win) | documented (V44) |
| Waveform of a very long sample | column positions from quotient and remainder, no 32-bit overflow | ours |
| Samples changed where they are, attribute not set again | the kept chip copy and waveform are the old ones until a sound attribute is set | ours |
| A sound attribute set while it plays | the playing copy stays until the replay stops; the next play copies afresh | ours |

## Not yet

- `SDTA_Continuous` streaming (double buffers, `SDTA_SyncSampleChange`
  waits): kept, not acted on.
- A true pause; the panel's buttons follow the mouse only while pressed.
- `DTM_COPY` to the clipboard (dt-d2's datatypesclass).

## Tests

`sh tests/m68k/test_opendatatypes_picsound.sh`: the plan, request by
request with a checksum of each, for mono, stereo, 8 and 16 bits,
interleaved, long samples split, pans and cycles; each plan run by the
player's own code against a simulated audio.device (every request in
order, none sent to a channel that has gone quiet); the chip copy and
waveform kept between plays and drawings and made afresh when the sample
is set again; the drawing calls of the waveform and the panel; see
[`../picture`](../picture/README.md).
