#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE at the repository root).
# GFOpen8svx with the os32-gcc16 stove: 8svx.datatype (47.100), gfopen8svx.datatype (the same code
# under its own name, side by side) and tests/8SVXDTClassABITest. See
# ../picture/build-class.sh.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$HERE/../picture/build-class.sh" "$HERE" svx 8svx.datatype 8SVXDTClassABITest
