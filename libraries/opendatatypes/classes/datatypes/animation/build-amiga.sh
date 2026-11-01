#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes' animation.datatype and anim.datatype with the os32-gcc16
# stove: the .lvo files against an fd if the NDK has one, then
#   animation.datatype, anim.datatype            the replacements (47.100)
#   gfopenanimation.datatype, gfopenanim.datatype    side by side
#   AnimationClassABITest                        OS 3.2's classes and ours
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$HERE/../text/build-family.sh" "$HERE/.." AnimationClassABITest animation anim
