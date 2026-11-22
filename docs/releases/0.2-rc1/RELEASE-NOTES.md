# GFOpenDatatypes 0.2-rc1

The first release candidate with films from games: Smacker on the Amiga
itself, and Bink through the services card.

## New

- **smacker.datatype 47.1** (`Datatypes/smacker`). Smacker films, SMK2
  and SMK4, decoded on the 68k with no card or Cradle. All four block
  types, SMK4's three ways of drawing a full block, the palette changes,
  and the first sound track (Smacker's own coding or PCM, 8 or 16-bit,
  mono or stereo), played as 8-bit mono with each frame. It opens a film
  from a file, or from memory (`DTST_MEMORY`), so a game can play the
  films packed in its own archives. The descriptor is `DEVS:DataTypes/Smacker`.
- **Bink films through gfopenvideo.datatype.** A new descriptor,
  `DEVS:DataTypes/Bink`, sends Bink 1 films to `media.decode/1`, whose
  FFmpeg decodes the pictures and the sound on the host. Bink 2 does not
  open.

## Tested

On the host (`Datatypes/tests/test-smacker.sh`, CI runs it):

- Eight test films made by `tests/make-smacker.py`, decoded by the core
  and by FFmpeg: every pixel and sample the same.
- 7,725 of them cut short or with bytes changed, under AddressSanitizer
  and UBSan: nothing read or written outside the film or the picture.
- The core built for the 68040 and run under qemu-m68k: the same pictures
  and sound.
- On daletop, with `SMACKER_FILMS` set to Might and Magic VII's films,
  extracted from `Might7.vid` and `Magic7.vid`: all 163 Smacker films the
  same as FFmpeg decodes them. Those films are not in the repository.

`tests/recognition/test_recognition.py`: a Smacker film (SMK2 and SMK4)
goes to `Smacker film` and a Bink film to `Bink film`, in both orders.
`tests/pack/test_pack.py`: smacker.datatype is a choice in the
Installer, after CDXL.

In lab t5-oolite-20261119 (a copy of Instance-32, AC090), 22 November 2026,
built by the kitchen (job j20261122T020851-aea01a):

- `Tests/dtanim` loaded every frame of five test films and MM7's "Human
  Magic Shop" film (460 x 344, 41 frames) through `animation.datatype`.
  Each frame's checksum is the one the core gives on the host.
- `Tests/dtanim` on MM7's `JVC` Bink film: 320 x 240, 10 frames a second,
  90 frames, the count FFmpeg gives.
- GFOpenView opens the MM7 film and shows its first frame in its colours,
  with the animation's controls.

Not yet checked: the sound as heard, playback speed on a real 68k, real
Amiga hardware, and a Pi or the x86 appliance.

## Installing on GFOS

The classes open their superclass as `datatypes/animation.datatype`, so
`LIBS:` must reach `SYS:Classes` and animation.datatype must be on disk
there. The lab copy of Instance-32 had neither: cdxl.datatype, which it
already had, failed to open for the same reason. The pack's Installer
puts the Team's datatypes.library and its system classes in when that is
ticked (the default).

## Files

| Part | Change |
| --- | --- |
| `Classes/DataTypes/smacker.datatype` | new, 47.1 |
| `Devs/DataTypes/Smacker` | new |
| `Devs/DataTypes/Bink` | new, for gfopenvideo.datatype |
| `GFOpenDatatypes.readme`, the Installer | 0.2-rc1, Smacker and Bink named |
