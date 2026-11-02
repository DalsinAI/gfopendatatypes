/*
 * gfopenimage tests: what Datatypes/common/dtlib.c's memory functions
 * need from exec, for test_achelpers.sh, which compiles them on their own
 * (renamed dt_malloc, dt_calloc, dt_free and dt_realloc) with this header.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stddef.h>
typedef unsigned long ULONG;
#define MEMF_ANY 0UL
#define MEMF_CLEAR (1UL << 16)
void *AllocVec(ULONG size, ULONG flags);
void FreeVec(void *block);
