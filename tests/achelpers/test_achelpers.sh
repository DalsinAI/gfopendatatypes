#!/bin/sh
# openamigaimage's use of AC090's native helpers (achelpers/), with an m68k
# cross GCC and qemu-m68k, since the Amiga compiler and the upstream
# tarballs are on the build PC. At -O0 and -O2 for the 68040:
# achelpers.sh builds the helpers from amigachrome-guest at the pinned
# commit; zlibsums.sh, run on stand-ins for zlib's crc32.c and adler32.c
# (zstub/, with the host's zlib.h), gives zlib's results and sends long
# buffers to the helpers (zlib_test.c); the forced header takes zlib's,
# libjpeg's and libpng's ways of calling memcpy, memmove and memset
# (string_test.c); and Datatypes/common/dtlib.c's calloc and realloc work
# with the helpers and without (dtmem_test.c).
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   AMIGACHROME_GUEST  as for build.sh
#   M68K_CC, M68K_PREFIX, QEMU_M68K  the cross tools (default m68k-linux-gnu-gcc,
#                m68k-linux-gnu-, qemu-m68k)
#   ZLIB_INCLUDE folder with zlib.h and zconf.h (default /usr/include)
#
# usage: sh tests/achelpers/test_achelpers.sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$here/../..
cc=${M68K_CC:-m68k-linux-gnu-gcc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
zinc=${ZLIB_INCLUDE:-/usr/include}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
command -v "$qemu" >/dev/null || { echo "skipped: no $qemu"; exit 0; }
[ -f "$zinc/zlib.h" ] && [ -f "$zinc/zconf.h" ] || { echo "skipped: no zlib.h in $zinc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
say() { echo "$1"; case "$1" in ok*) ;; *) fail=1 ;; esac; }
wrap="-Wl,--wrap=ac_memcpy,--wrap=ac_memmove,--wrap=ac_memset"

for flags in "-O0 -m68040" "-O2 -m68040"; do
    cpu=m$(echo "$flags" | sed 's/.*-m\(680[0-9]0\).*/\1/')
    cf="-std=gnu99 -Wall -Wextra -Werror -fno-delete-null-pointer-checks $flags"
    t=$tmp/$(echo "$flags" | tr -d ' -'); mkdir -p "$t"
    if ! CC=$cc AR=${prefix}ar CFLAGS="$cf" sh "$root/achelpers/achelpers.sh" "$t/ach" > "$t/log" 2>&1; then
        cat "$t/log"; say "FAIL $flags: achelpers.sh"; continue
    fi
    acflags="-I$t/ach/src -include $root/achelpers/ac_string.h"

    # zlib's checksums over the stand-ins, as build.sh compiles them
    mkdir -p "$t/z"; cp "$here"/zstub/*.c "$zinc/zlib.h" "$zinc/zconf.h" "$t/z/"
    msg=$(cd "$t/z" && CC=$cc CFLAGS="$cf -I. $acflags" sh "$root/achelpers/zlibsums.sh" "$t/zo")
    case "$msg" in *"gzip crc too"*) ;; *) say "FAIL $flags: zlibsums.sh missed copy_with_crc: $msg" ;; esac
    # shellcheck disable=SC2086
    "$cc" $cf -static -I"$t/z" "$here/zlib_test.c" "$t"/zo/*.o -L"$t/ach" -lachelpers -o "$t/zlib_test"
    say "$("$qemu" -cpu "$cpu" "$t/zlib_test" || true) (zlib's checksums, $flags)"

    # the forced header
    # shellcheck disable=SC2086
    "$cc" $cf $acflags -static "$here/string_test.c" -L"$t/ach" -lachelpers $wrap -o "$t/string_test"
    say "$("$qemu" -cpu "$cpu" "$t/string_test" || true) (memcpy, memmove and memset, $flags)"

    # dtlib.c's memory functions on their own, with the helpers and without
    for on in 1 0; do
        {
            echo '#include "dtmem_stub.h"'
            echo '#include <string.h>'
            sed -n '1,/^struct ExecBase/p' "$root/Datatypes/common/dtlib.c" | sed -n '/^#ifdef OAI_AC_HELPERS/,/^#endif/p'
            printf '#define malloc dt_malloc\n#define calloc dt_calloc\n#define free dt_free\n#define realloc dt_realloc\n'
            sed -n '/^\/\* --- task-safe memory/,/^\/\* A small vsnprintf/p' "$root/Datatypes/common/dtlib.c" | sed '$d'
        } > "$t/dtmem.c"
        d=; [ $on = 1 ] && d="-DOAI_AC_HELPERS -I$t/ach/src $wrap"
        # shellcheck disable=SC2086
        "$cc" $cf $d -static -I"$here" "$t/dtmem.c" "$here/dtmem_test.c" -L"$t/ach" -lachelpers -o "$t/dtmem_test"
        say "$("$qemu" -cpu "$cpu" "$t/dtmem_test" || true) (dtlib.c, $flags)"
    done
done

# a crc32.c without Chromium's copy_with_crc: zlib's own keeps it
mkdir -p "$tmp/plain"; cp "$here"/zstub/adler32.c "$zinc/zlib.h" "$zinc/zconf.h" "$tmp/plain/"
sed '/^void copy_with_crc/,$d' "$here/zstub/crc32.c" > "$tmp/plain/crc32.c"
CC=$cc CFLAGS="-O2 -m68040 -fno-delete-null-pointer-checks -I. -I$tmp/O2m68040/ach/src" sh -c 'cd "$1" && sh "$2" "$1/zo"' sh "$tmp/plain" "$root/achelpers/zlibsums.sh" > "$tmp/plain/msg" 2>&1 || true
if grep -q "stays zlib's own" "$tmp/plain/msg" && ! "${prefix}nm" "$tmp/plain/zo/ac_zlib.o" | grep -q copy_with_crc; then
    say "ok   zlibsums.sh leaves deflate's gzip crc to zlib when crc32.c has no plain copy_with_crc"
else
    cat "$tmp/plain/msg"; say "FAIL zlibsums.sh without copy_with_crc"
fi
exit $fail
