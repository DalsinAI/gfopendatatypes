# gfopenmodule.datatype: design

8 October 2026. Music modules (ProTracker MOD and its relatives, MED and
OctaMED, Oktalyzer, DigiBooster, FastTracker 2, Scream Tracker 3, Impulse
Tracker and the rest libxmp reads) played on the Amiga itself, through any
datatypes program: GFOpenPlay, MultiView, a game's sound loader.

## 1. What AmigaOS 3.2 offers

AmigaOS 3.2 ships no module datatype: a `.mod` file does not open at all.
Its `sound.datatype` (47.6) plays one sample through audio.device. The
NDK 3.2 autodoc (`sound_dtc.doc`) gives two ways to play something long:

1. **Continuous data** (V40, cleaned up in V44): an application creates a
   sound object with `SDTA_Continuous, TRUE` and keeps setting new
   `SDTA_Sample` buffers; with `SDTA_SyncSampleChange` the setter waits until
   the previous buffer has played, and `SDTA_SignalTask` is signalled when
   the next one is wanted. This is meant for an *application* feeding a
   sound object. The behaviour lives in sound.datatype's closed player
   process, and it differs between versions (AROS's re-implementation,
   SoundDT41, wraps buffers in its own way, and 3.2's sound.datatype 47
   converts anything over 8 bits "poorly" to 8-bit, by its own autodoc).
2. **A streaming subclass** (V44): "Subclasses which support streaming data
   access ... will always return NULL when their SDTA_LeftSample,
   SDTA_RightSample and SDTA_Sample attributes are queried. However, they
   will never return 0 for the SDTA_SampleLength and SDTA_SamplesPerSec
   attributes ... Streaming subclasses will respond to the DTM_TRIGGER
   method, to start, stop playback, etc. but may not support any other
   methods which rely upon the entire sample to reside in memory."

Decoding a whole module into one PCM sample (as `gfopensound.datatype` does
with what the Cradle sends back) costs a long wait before the first note
and the whole song in RAM: three minutes at 28 kHz in 8-bit stereo is
about 10 MB, more than most Amigas have.

## 2. The choice: a streaming subclass with its own player

gfopenmodule.datatype is the second kind. Each object has a **player
process** of its own (`gfopenmodule player`, priority 2, 64 KB of stack):

- At `OM_NEW` the datatype reads the file into memory and starts the
  process, which loads the module with libxmp
  (`xmp_load_module_from_memory`) and answers with its title, kind and
  length. A file libxmp can't read fails `OM_NEW` (`DTERROR_INVALID_DATA`),
  as any datatype does. It then chooses what mixes the module (a cores
  board core, media.decode/1, or this CPU: section 7) and where the sound
  goes: AHI when it is installed, else Paula (section 3).
- **AHI** (the Team, 8 October 2026: "all mods should open AHI"): the
  player opens `ahi.device` unit 0, the unit AHI prefs sets up for music
  (on AmiCradle, ACAHI's Host mix mode), once for the object's life, and
  mixes 16-bit stereo at that unit's own rate (its `AHIU` chunk in
  `ENV:Sys/ahi.prefs`, 44100 Hz here), so AHI does not resample. On
  `STM_PLAY` it sends two `CMD_WRITE` requests of half a second, the
  second linked to the first (`ahir_Link`); each time one comes back, the
  next half second is mixed into it and it is sent again, linked to the
  one playing. That is AHI's own double buffering through its device
  interface. Why the device and not the library (`AHI_AllocAudio`,
  `AHI_LoadSound` with `AHIST_DYNAMICSAMPLE`): the device shares the
  unit with every other program and uses the mode and rate the user chose
  in AHI prefs, while the library takes a mode for itself, mostly
  exclusively, and needs a SoundFunc running in AHI's interrupt to keep
  the buffer fed. Replies are taken with `GetMsg` from the player's port
  and played-out requests refilled in the order they were sent. Found on
  3.2 with AHI 6.6: a third request linked to one still waiting (not yet
  playing) was never played, so it stays at two of half a second (a
  second queued).
- Paula, without AHI (or with `ENV:GFOpenImage/ModuleOutput` set to
  `paula`): on `STM_PLAY` it allocates one left and one right Paula channel
  (audio.device, allocation map 3, 5, 10, 12), mixes four buffers of an
  eighth of a second each (`xmp_play_buffer`, 8-bit stereo), splits each
  into a left and a right chip RAM buffer, queues them with `CMD_WRITE` on
  stopped channels and starts both at once (`CMD_START`), so left and right
  stay in step. Each time a pair of buffers has played, the next eighth of
  a second is mixed into it and queued behind the others: audio.device
  chains queued writes through Paula's own double buffering, with no gap.
  Half a second is always queued, so a busy moment elsewhere does not
  break the sound.
- `STM_PAUSE` stops Paula's channels (`CMD_STOP`) and `STM_PLAY` starts
  them again. On AHI, pause drops the two requests (what they held, up to
  a second, is skipped: `CMD_STOP` would stop the whole unit, other
  programs' sound too) and play sends new ones from where the mixing is.
  `STM_STOP` returns the queue (`CMD_FLUSH` on Paula, `AbortIO` on AHI),
  frees Paula's channels for other programs and rewinds the song; `STM_REWIND` does both. Commands come
  through a word in the object and `SIGBREAKF_CTRL_F`, so they can be given
  from any task (a click on the gadget comes from input.device's) and
  never wait for the mixer; the latest one wins.
- `SDTA_Volume` is AHI's request volume (0 to 64 as 0 to 1.0), heard from
  the next request (within a second), or Paula's channel volume, changed at
  once with `ADCMD_PERVOL` (only the first write of a start carries a
  volume, so the queued ones do not undo it). Neither costs mixing. `DTA_Repeat` loops the
  song (libxmp's loop count); without it the song ends at its end, the
  channels are freed, the song rewinds and `SDTA_SignalTask` is
  signalled.
- `SDTA_SampleLength` and `SDTA_SamplesPerSec` give the song's length in
  sample frames at the mixing rate (libxmp's scan of the song), so GFOpenPlay
  shows the duration. `SDTA_Sample` stays NULL. `DTA_ObjName` is the
  module's title (the file name when it has none) and `DTA_ObjAnnotation`
  the kind libxmp names ("Protracker M.K.", "OctaMED 3.00 MMD0").
- `DTA_TriggerMethods` lists Play, Pause and Stop. `DTM_WRITE` in raw
  mode saves the module file as it came; there is no 8SVX of a module, so
  IFF mode and `DTM_COPY` say "not implemented".
- `DTA_Immediate` starts playing after the first layout, and a click on
  the object plays, as with a sound.

All of libxmp's work happens in the player process. That matters because
libxmp uses some floating point (periods, per tick) and the datatype is
built for the 68040 with soft float (no FPU code): libnix's soft floating point calls the
ROM math libraries, which set up the FPU of the task that opens them. The
player process opens them for itself, so the calling program's FPU state
is never touched (the other datatypes here avoid the math libraries
altogether for that reason).

## 3. 16-bit through AHI, 8-bit through Paula

With AHI, libxmp mixes 16-bit stereo (`AHIST_S16S`) and the sound keeps
its depth: on AmiCradle ACAHI takes it to the host as it is. Without AHI,
sound.datatype 47 has no 16-bit playback either (`SDTA_BitsPerSample`
converts to 8-bit), so gfopenmodule plays through audio.device itself and
Paula plays 8-bit samples: libxmp mixes in 32 bits and clips once to 8
bits (`XMP_FORMAT_8BIT`), better than mixing to 16 and dropping the low
byte later, and Paula's 6-bit volume scales it for free. The status line
says which ("..., through AHI" or "..., through Paula (no AHI)").

## 4. Rate and interpolation

| Setting | Default | Read from |
| --- | --- | --- |
| Where it plays | AHI unit 0 when installed, else Paula | `ENV:GFOpenImage/ModuleOutput`: `paula` |
| Mixing rate, AHI | AHI unit 0's rate (44100 Hz here); 48000 Hz asked of media.decode/1 | `ENV:Sys/ahi.prefs` |
| Mixing rate, Paula | 28000 Hz on a 68040 or 68060; 16000 Hz on a 68020 or 68030 | `ENV:GFOpenImage/SoundRate` (shared with gfopensound.datatype) |
| Interpolation | linear on a 68040 or 68060; none (nearest) on a 68020 or 68030 | `ENV:GFOpenImage/ModuleMix`: `nearest`, `linear` or `spline` |
| Who mixes | the ladder (section 7) | `ENV:GFOpenImage/ModulePlayer`: `auto`, `cpu`, `cores` or `service` |

For Paula the rate is turned into a whole Paula period (PAL or NTSC, from the
E clock) of at least 124, and libxmp mixes at exactly the rate that
period plays, so pitch is exact: 28000 Hz becomes 27928 Hz (period 127)
on PAL.

## 5. Formats and descriptors

libxmp 4.7.3 with all its loaders (some 60 formats). Left out: its
depackers and ProWizard, which write temporary files (PowerPacker or XPK
packed modules don't open), and stb_vorbis (an XM with Ogg Vorbis samples
doesn't load).

Descriptors (`DEVS:DataTypes`), all group `soun`, priority 0:

| File | Matches |
| --- | --- |
| `ProTracker` | `M.K.` at byte 1080 |
| `ProTracker-100` | `M!K!` at 1080 (over 64 patterns) |
| `StarTrekker`, `StarTrekker-8` | `FLT4`, `FLT8` at 1080 |
| `MOD-xCHN` | `?CHN` at 1080 (2 to 9 channels, FastTracker) |
| `MOD-xxCH` | `??CH` at 1080 (10 to 32 channels, TakeTracker) |
| `SoundTracker` | the name `mod.#?` or `#?.mod`, no mask |
| `OctaMED` | `MMD0` to `MMD3` (and `MMDC`) |
| `MED-2`, `MED-3`, `MED-4` | `MED` and 2, 3 or 4 |
| `Oktalyzer` | `OKTASONG` |
| `DigiBooster`, `DigiBooster-Pro` | `DIGI Booster module`, `DBM0` |
| `Module-XM`, `Module-S3M`, `Module-IT` | gfopensound's `XM`, `S3M`, `IT` masks and one more byte (any) |

**How AmigaOS 3.2 picks a descriptor.** Measured on AmigaOS 3.2.3
(datatypes.library 47.3, AddDataTypes 47.2), 8 October 2026, with pairs
of test descriptors matching the same file, added in both orders:

- a longer mask is tried first, whichever was added first;
- a descriptor with no mask (a name only) comes after every one with a
  mask;
- between equal masks, priority 0 came before 1, 5, 10 and 256: the
  reverse of AROS, where a higher priority is tried first. A priority of
  -1 came after 0, and a name-only descriptor at -1 was never reached (the
  `binary` fallback came first).

So priority can't be used to put one datatype before another on 3.2; the
length of the mask can. That decided three things:

- **MOD signatures sit at byte 1080.** datatypes.library reads as much of
  a file as its longest mask, so these masks are 1084 bytes long, and they
  matched on 3.2.3. Every file examined is then read that far (one read,
  usually the same disk block).
- **The 15-instrument SoundTracker MOD has no signature.** Its header is
  20 bytes of title, 15 sample headers and an order list, all of which can
  hold almost anything. A mask could ask for the high byte of each sample's
  volume word (always 0 in Ultimate SoundTracker), but later 15-instrument
  editors put a finetune there, which libxmp accepts. So it goes by name,
  in the Amiga's way (`mod.name`) or the PC's (`name.mod`), with no mask:
  it is tried after every descriptor that has one, so a file of another
  kind that happens to be called `mod.something` still opens as what it
  is when its own datatype knows it. libxmp then checks the header; a file
  that is not a module fails to open with "invalid data" (DTERROR 2008),
  where it would otherwise have had no datatype.
- **XM, S3M and IT are in both** gfopenmodule and gfopensound (played by
  libopenmpt on the services card or a paired Cradle). gfopenmodule's masks
  are one byte longer (an ANY after the signature; no such module is that
  short), and their names differ (`FastTracker 2 module` against
  `FastTracker module`; datatypes.library keeps one descriptor per name),
  so with both installed these play on the Amiga, and gfopensound's stay as
  the way to play them where gfopenmodule is not installed.

## 6. CPU

libxmp's mixer is C: for each voice and each output frame, a fetch, a
step, a multiply by the volume and an add, per side. It costs about the
same per voice for every format; what differs is how many voices play and
how often the player's per-tick work (in floating point) runs.
`tests/modbench.c` times `xmp_play_buffer` over ten seconds of each test
module (`tests/make-modules.py`), 8-bit stereo, an eighth of a second a
call, as the datatype calls it. On AC090's 68040 (JIT, the modlab scratch
copy of AmigaOS 3.2.3, 8 October 2026), as a share of one CPU while the
module plays:

| Module | Voices | 28 kHz linear | 28 kHz nearest | 16 kHz nearest |
| --- | --- | --- | --- | --- |
| ProTracker M.K. | 4 | 22.6 % (26.3 % on a second run) | 12.4 % | 8.9 % |
| FastTracker 8CHN MOD | 8 | 33.9 % | 14.8 % | 12.2 % |
| OctaMED MMD0 | 4 | 21.7 % | | 8.4 % |
| Oktalyzer | 4 | 24.6 % | | 8.7 % |
| FastTracker 2 XM | 6 | 29.4 % | | 10.6 % |

The ProTracker module also took 11.8 % with spline interpolation at
28 kHz and 14.5 % linear at 16 kHz. That spline cost less than linear is
AC090's JIT (it interprets 32-bit multiplies, section 7), not libxmp (on
a PC spline costs about 10 % more than linear); AC090's figures are its own and say little about a real 68040,
nor about a stock 68020, which is many times slower. The 68020 and 68030
defaults (16 kHz, nearest) are chosen to be safe, not yet measured.

Where AC090's native helpers could take the work later
(`../../achelpers/`, amigachrome-guest's magic functions): the mixer's
inner loops (`src/mix_all.c`, one function per sample format and
interpolation) and the 32-to-8-bit downmix (`src/mixer.c`) are each a
single call over a whole tick's worth of frames with plain arrays in and
out: the shape the helpers already handle for copies and checksums. A
`mix_voice` helper, run as host code on AmiCradle, would leave the 68k
only the per-tick player logic. On a real Amiga, the same routines
hand-written for the 68020 and 68040 (as the helpers' `memcpy` is) would
gain over GCC's code.

A 4-channel MOD could be played with no mixing at all, as ProTracker
itself does: libxmp's player computes each channel's period, volume and
sample position every tick, which could be handed straight to Paula's four
channels. That would cost a 68000 almost nothing, but leaves no channel for
other sounds and gives up stereo placement; it is the next step if the
mixing costs too much on a stock 68020.

## 7. The ladder: another core, a service, this CPU

The Team, 8 October 2026: heavy modules on slow Amigas should go to
"other cores first, then network services", and then "all playback
should offload to Cradle / other cores". So every module, light or heavy,
is mixed by the first of these that is there, chosen when it opens:

1. **A cores board core** (AmiCradle's AmiCradle Team $DA15 product 7, up to
   eight translated 68040 cores, `cpu.m68k/1`), through
   `gfopenmulticore.library` (DalsinAI/openamigamulticore) on
   gfopenservice.device. Each chunk (half a second for AHI, an eighth for
   Paula) is one job: `om_job_mix`, which calls `xmp_play_buffer` on a
   core, by `OMC_Run68k`. The board's rules (AmiCradle `CORES_BOARD.md`)
   are that a job calls no OS, touches no Chip RAM and writes only its
   written buffers. libxmp allocates nothing while it mixes, so the player
   loads and starts it with an arena in use (`xmpglue.h`): every
   allocation libxmp makes comes from one block of Fast RAM, and that block
   (160 to 360 KB for the test modules) is the job's one written buffer.
   Reads are permissive: the code, libxmp's tables and the ROM math
   libraries its floating point calls are read where they are. The main
   CPU sleeps in `OMC_Run68k` while the core mixes. The first job is the
   test: if the board refuses it or it faults, the next rung plays; a job
   that fails later stops the song, which plays again on this CPU.
2. **media.decode/1** on a services card or a paired Cradle (libopenmpt,
   as gfopensound.datatype uses), when there is no cores board: `DECODE`
   two seconds at a time from a frame offset (`MEDIA_DECODE.md`: "a long
   sound can come in pieces"), 48000 Hz for AHI (the service's rate for
   Paula). Each call takes 0.1 to 0.5 s, so a second is always queued.
3. **This CPU**, only when there is neither.

`ENV:GFOpenImage/ModulePlayer` (`cores`, `service`, `cpu`) forces one; a
forced rung that is not there falls to the next. The status line says
which rung plays and where the sound goes: `OIA_DecodedBy` ("a cores
board core, through AHI", "media.decode/1 (the Nursery), through AHI",
"this Amiga's CPU, through Paula (no AHI)") and `OIA_Stats` (the measured
load), openamigaimage attributes in `../include/datatypes/gfopenimage.h`.
GFOpenPlay shows them.

**Measured** on the modlab scratch copy (AmigaOS 3.2.3 on AC090, a 68040,
with the cores board fitted, `"coresBoard": true`, eight translated
cores, and AHI 6.6 with ACAHI's Host mix mode on unit 0 at 44100 Hz),
8 October 2026, with `tests/dtsound.c LOAD=1`, which counts at the lowest
priority while a module plays: "main CPU" is how much of that counting
playing took away. "Work" is the core's job time, the service's call time
or this CPU's mixing time, as a share of the sound's time. Sound recorded
from ACAHI's ring (`/dev/shm/amigachrome-native-8888.ahi`): no gaps but
the pause the test gives, on every module and rung except the 32-channel
XM forced onto this CPU; every recording has the same spectrum as libxmp's
render on the PC (correlation of log band energies 0.99) and the same
left and right balance.

| Module | Rung 1, a core (auto) | | Rung 2, media.decode/1 (forced) | | Rung 3, this CPU (forced) | |
| --- | --- | --- | --- | --- | --- | --- |
| | work | main CPU | a 2 s call | main CPU | mixing | main CPU |
| ProTracker M.K., 4 voices | 4-20 % | 0-2 % | 91-116 ms | 0 % | 26-27 % | 19-23 % |
| FastTracker 8CHN MOD, 8 voices | 11-16 % | 0-3 % | 122-152 ms | 0-3 % | 43-44 % | 36-38 % |
| OctaMED MMD0, 4 voices | 8-10 % | 0-5 % | 90-127 ms | 0-3 % | 28 % | 22 % |
| FastTracker 2 XM, 6 voices | 7-20 % | 0-5 % | 105-196 ms | 2-6 % | 37 % | 29-34 % |
| FastTracker 2 XM, 32 voices | 27-74 % | 0-3 % | 340-469 ms | 3-5 % | 152-153 % | 87 %, breaks up |

Two runs each (the first rung's numbers vary: a job's completion is
published by the main core's emulation thread at the end of a line,
`CORES_BOARD.md`, so it depends on what the main core is doing). Through
Paula, without AHI (`ModuleOutput` `paula`), a 4-channel MOD on a core
took 10-25 % a job and 2-6 % of the main CPU. A song played to its end
signals `SDTA_SignalTask` after its 30.7 s; volume 16 of 64 set during a
pause brings AHI's level to 0.24.

AC090 interprets 32-bit multiplies (`MULS.L`, `MULU.L`) inline rather
than translating them (its own log shows them, 35 million every five
seconds in a loop of them). libxmp's linear interpolation does one per
sample, which is why linear mixing costs more than spline here (section
6), on the main core and on the board's cores alike. Translating them is
the largest gain for this datatype on AmiCradle.

**What it needs:** `gfopenmulticore.library` in `LIBS:`, built with the os32
stove (GCC 6.5) as its own `library/build.sh` does: built with os32-gcc16
it passes jobs the wrong arguments (OMCTest's first job ran for minutes,
even on the main CPU). It is not in GFOpenUp yet, so the first rung is there
only where it is installed by hand.

**Not yet:** the job runs the soft-float libxmp, whose floating point goes
through the ROM math libraries (library code, read and run where it is).
That works on the board, but it bends rule 1 (no library calls), and over
the LAN (strict jobs) it would not. A libxmp built with `-m68881` for the
core (the board's cores are 68040s with an FPU) would keep to the rule
and be faster. A module bigger than about 4.5 MB needs an arena over the
15 MB a job may write, so it skips the first rung. A Cradle on the LAN as
the second rung is not measured. The rung is chosen when a module opens.
It does not move while the module plays, except from a core that fails.
On a real Amiga with neither a cores board nor a services card or Cradle,
every module plays on its own CPU, as before.
