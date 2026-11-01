#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# Opensound with the os32-gcc16 stove: sound.datatype (47.100), gfopensound.datatype (the same code
# under its own name, side by side) and tests/SoundDTClassABITest. See
# ../picture/build-class.sh.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$HERE/../picture/build-class.sh" "$HERE" sound sound.datatype SoundDTClassABITest
