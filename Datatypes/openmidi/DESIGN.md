# gfopenmidi.datatype

A sound.datatype subclass that plays Standard MIDI Files (formats 0, 1
and 2, and the RIFF "RMID" wrapper) through `gfopensynth.library`, which
is all our own code. It replaces the earlier MIDI descriptor, which sent
the file to the Cradle to be played by a third-party synth.

## 1. Where the sound is made

The datatype never makes sound itself. It opens `gfopensynth.library`
(version 1 or newer, from the GFOpenSynth part) and hands it the file:

- With an AkikoPlus MIDI port on the board (`GSOUT_AUTO`), the library
  writes the MIDI bytes to the port and the host's synth sounds them in
  the board's own audio.
- Without a port the library's 68k engine plays the file through
  `ahi.device`.
- When a program asks for the sound itself (`OIA_Reader`), the datatype
  opens the synth with `GSOUT_CALLER` at `OIA_MixRate` and
  `OIM_READSOUND` hands out 16-bit stereo frames from `GSRenderS16`,
  with a one second tail after the last event.

`ENV:GFOpenImage/MidiEngine` set to `mt32` makes the synth treat the
file as written for an MT-32 (`GSE_MT32`); anything else is General
MIDI.

If the library is not installed the object still opens, so the file is
named and described, and playing it does nothing.

## 2. Streaming, the same shape as gfopenmodule

`SDTA_Sample` stays NULL. `SDTA_SampleLength` and `SDTA_SamplesPerSec`
give the length of the tune so players can show a time. Play, pause and
stop arrive as `DTM_TRIGGER` (`STM_PLAY`, `STM_PAUSE`, `STM_STOP`), and
by a click on the object.

Triggers can arrive on input.device's task, which may neither wait nor
call DOS, so each object has a small control process. A trigger stores
the wanted command in the instance and signals the process
(`SIGBREAKF_CTRL_F`); the process makes the library calls. A volume
change is `SIGBREAKF_CTRL_E`. While a tune plays the process wakes five
times a second on timer.device, and when the library says the tune has
ended it signals `SDTA_SignalTask` as sound.datatype does. Dispose sends
`SIGBREAKF_CTRL_C` and waits for the process's last message.

## 3. Recognition

| Descriptor | Mask |
| --- | --- |
| `MIDI` | `MThd`, then the length 0 0 0 6 |
| `MIDI (RIFF)` | `RIFF`, any four bytes, `RMID` |

Both are in the sound group, so any program that plays sounds through
datatypes plays them; OpenPlay needs no change.

## 4. Files

- `midifile.c`, `midifile.h`: reading the header (plain C, tested on the
  host by `tests/miditest.c`, run by `tests/test-midi.sh`).
- `midiclass.c`: the class.
- `build.sh`: the 68k build, called from `Datatypes/build.sh`.
