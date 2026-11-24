#!/bin/sh
# The datatypes' connection to gfopenservice.device (common/dtservice.c) on
# the host, under AddressSanitizer and UBSan (tests/dtservicetest.c): one
# connection serving any task, the read-ahead taken by its sender or by
# another task, files sent once (and to an older host, whole every time),
# with fakes for exec, the device and the service. Then again with
# DT_MITCHY (gfopensound.datatype's build): mitchy.library missing, and there;
# and akikoplus.library 3, every datatype's first way in (DT_NO_AKIKOPLUS: without it).
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
# DT_AKIKOPLUS (on unless DT_NO_AKIKOPLUS): the real libraries/akikoplus.h, and its calls as the test's fakes.
mkdir -p "$INC/inline"
cat > "$INC/inline/akikoplus.h" <<'EOF'
APTR fakeAPOpen(struct Library *, CONST_STRPTR, ULONG, LONG *);
void fakeAPClose(struct Library *, APTR);
LONG fakeAPWhere(struct Library *, APTR);
LONG fakeAPCall(struct Library *, APTR, const struct APServiceCall *, ULONG *);
APTR fakeAPSend(struct Library *, APTR, const struct APServiceCall *, struct MsgPort *);
LONG fakeAPWait(struct Library *, APTR, ULONG *);
LONG fakeAPAbort(struct Library *, APTR, ULONG *);
#define AP_OpenService(n, f, e) fakeAPOpen(AKIKOPLUS_BASE_NAME, n, f, e)
#define AP_CloseService(s) fakeAPClose(AKIKOPLUS_BASE_NAME, s)
#define AP_ServiceWhere(s) fakeAPWhere(AKIKOPLUS_BASE_NAME, s)
#define AP_CallService(s, c, r) fakeAPCall(AKIKOPLUS_BASE_NAME, s, c, r)
#define AP_SendService(s, c, p) fakeAPSend(AKIKOPLUS_BASE_NAME, s, c, p)
#define AP_WaitService(q, r) fakeAPWait(AKIKOPLUS_BASE_NAME, q, r)
#define AP_AbortService(q, r) fakeAPAbort(AKIKOPLUS_BASE_NAME, q, r)
EOF
# shellcheck disable=SC2086
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $SAN -I"$INC" -I"$HERE/tests" -I"$HERE/common" -I"$HERE/include" \
    -o "$OUT/dtservicetest" "$HERE/tests/dtservicetest.c"
"$OUT/dtservicetest"
# DT_NO_AKIKOPLUS: the device alone, as before akikoplus.library 3.
# shellcheck disable=SC2086
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $SAN -DDT_NO_AKIKOPLUS -I"$INC" -I"$HERE/tests" -I"$HERE/common" -I"$HERE/include" \
    -o "$OUT/dtservicetest-noakiko" "$HERE/tests/dtservicetest.c"
"$OUT/dtservicetest-noakiko"
# DT_MITCHY: the real libraries/mitchy.h, and its calls as the test's fakes.
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
