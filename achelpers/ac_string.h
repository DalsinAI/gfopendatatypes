/*
 * openamigaimage: forced into every compile of zlib, libpng and libjpeg
 * (-include) when build.sh runs with AC_HELPERS=1. Their memcpy, memmove and
 * memset calls go through the front doors of AC090's native helpers
 * (amigachrome-guest's ac_helpers.h): under 64 bytes, GCC's own code as
 * before; from 64 bytes, the tagged functions, which run as host code on
 * AmiCradle and as 68k code written for the 68020 and 68040 elsewhere.
 * zlib's zmemcpy and zmemzero, and libjpeg's MEMCOPY and MEMZERO, are
 * memcpy and memset underneath. <string.h> comes first, so including it
 * again changes nothing, and a later declaration of memcpy declares the
 * front door again, which C allows.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OAI_AC_STRING_H
#define OAI_AC_STRING_H
#include <string.h>
#include "ac_helpers.h"
#undef memcpy
#undef memmove
#undef memset
#define memcpy(d, s, n) ac_memcpy_auto(d, s, n)
#define memmove(d, s, n) ac_memmove_auto(d, s, n)
#define memset(d, c, n) ac_memset_auto(d, c, n)
#endif
