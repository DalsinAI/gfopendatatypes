#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# Openpicture with the os32-gcc16 stove: picture.datatype (47.100), gfopenpicture.datatype (the same code
# under its own name, side by side) and tests/PictureDTClassABITest. See
# ../picture/build-class.sh.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$HERE/../picture/build-class.sh" "$HERE" picture picture.datatype PictureDTClassABITest
