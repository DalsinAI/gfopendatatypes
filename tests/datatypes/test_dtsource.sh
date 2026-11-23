#!/bin/sh
# Datatypes/tests/test-source.sh: which source the datatypes' dt_read_source()
# reads, a file or memory (DTST_MEMORY). GF-70.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$HERE/Datatypes/tests/test-source.sh" "$@"
