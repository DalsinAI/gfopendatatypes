# acmedia.datatype — first route-opaque slice

This is the first reviewable slice of the contract in
`DalsinAI/amicradle-teams/designs/ACMEDIA-DATATYPE-20261114.md`.

`include/datatypes/acmediaclass.h` defines the animation-class feed and control
API. It deliberately has no route, path, selected-rung or force-rung attribute:
programs hand over the stream; `akikoplus.device` / ACMedia chooses rung A, B
or C and the available output.

`amcore.c` is the OS-free boundary between public `AMDTM_FEED` and the device's
`CMD_WRITE`. It copies each caller buffer, bounds data by high water, emits a
low-water NEED edge, preserves partial device writes, invalidates old seek
generations, resets a single track parser, and distinguishes explicit END from
starvation. `AMDTM_REMOVE` removes still-private queued chunks; the device
wrapper must also send REMOVE for bytes the device already accepted. Run
`sh Datatypes/tests/test-acmedia.sh`; with the current stove,
`sh Datatypes/tests/test-acmedia-amiga.sh` also fixes every public message size
at `-O0` and `-O2` and links the core into AmigaOS loadseg binaries.

This slice does **not** build or install `acmedia.datatype`. The next stack adds
the Amiga BOOPSI wrapper after the public message layout is reviewed. Its
minimum device dependency is gfosautoconfigboards #10: that draft adds raw
`CMD_WRITE`, short-write and EOF handling, but presently has no wire fields for
track/generation/timestamp/append-window metadata, and its host source broker
still does not admit FEED sources. Those are explicit integration blockers,
not datatype route choices. Then come asynchronous device events, AHI/ACAHI
clocking and animation bitmap fallback. There is no Amiga, browser, worker, Pi,
card or lab proof.
