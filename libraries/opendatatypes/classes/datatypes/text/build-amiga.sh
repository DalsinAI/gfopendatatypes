#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# GFOpenDatatypes' text.datatype and ascii.datatype with the os32-gcc16
# stove: the .lvo files against an fd if the NDK has one, then
#   text.datatype, ascii.datatype            the replacements (47.100)
#   gfopentext.datatype, gfopenascii.datatype    the same code under their own
#                                            names, for the side-by-side test
#   TextClassABITest                         OS 3.2's classes and ours
# Copy the classes to SYS:Classes/DataTypes/ (or LIBS:datatypes/).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec sh "$HERE/build-family.sh" "$HERE/.." TextClassABITest text ascii
