#!/bin/sh
# The datatypes' connection to gfopenservice.device (common/dtservice.c) on
# the host, under AddressSanitizer and UBSan (tests/dtservicetest.c): one
# connection serving any task, the read-ahead taken by its sender or by
# another task, files sent once (and to an older host, whole every time),
# with fakes for exec, the device and the service. Then again with
# DT_MITCHY (gfopensound.datatype's build): mitchy.library missing, and there.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC     the host compiler (default cc)
#   SAN    sanitizer flags (default -fsanitize=address,undefined; SAN= for none)
#
# usage: sh tests/test-dtservice.sh [OUTDIR]       (default work/dtservice-test)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/work/dtservice-test"}
CC=${CC:-cc}
SAN=${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}
INC="$OUT/host"
for h in exec/types.h exec/io.h exec/memory.h exec/ports.h exec/tasks.h dos/dos.h dos/var.h \
         proto/exec.h proto/dos.h intuition/classes.h intuition/classusr.h; do
    mkdir -p "$INC/$(dirname "$h")"
    echo '#include "dtservice-host.h"' > "$INC/$h"
done
# shellcheck disable=SC2086
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $SAN -I"$INC" -I"$HERE/tests" -I"$HERE/common" -I"$HERE/include" \
    -o "$OUT/dtservicetest" "$HERE/tests/dtservicetest.c"
"$OUT/dtservicetest"
# DT_MITCHY: the real libraries/mitchy.h, and its calls as the test's fakes.
mkdir -p "$INC/inline"
cat > "$INC/inline/mitchy.h" <<'EOF'
struct MitchyService *fakeMitchyOpen(struct Library *, CONST_STRPTR, ULONG, LONG *);
void fakeMitchyClose(struct Library *, struct MitchyService *);
LONG fakeMitchyWhere(struct Library *, struct MitchyService *);
LONG fakeMitchyCall(struct Library *, struct MitchyService *, ULONG, ULONG, ULONG, struct MitchyBuffer *, ULONG *, ULONG *);
#define MitchyOpen(n, f, e) fakeMitchyOpen(MITCHY_BASE_NAME, n, f, e)
#define MitchyClose(s) fakeMitchyClose(MITCHY_BASE_NAME, s)
#define MitchyWhere(s) fakeMitchyWhere(MITCHY_BASE_NAME, s)
#define MitchyCall(s, o, f, a, b, e, r) fakeMitchyCall(MITCHY_BASE_NAME, s, o, f, a, b, e, r)
EOF
# shellcheck disable=SC2086
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $SAN -DDT_MITCHY -I"$INC" -I"$HERE/tests" -I"$HERE/common" -I"$HERE/include" \
    -o "$OUT/dtservicetest-mitchy" "$HERE/tests/dtservicetest.c"
"$OUT/dtservicetest-mitchy"
