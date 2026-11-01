/*
 * exec/types.h for building the datatypes' decoders on x86 or ARM64 cores
 * (tests/tgatest.c): the Amiga's sizes, from <stdint.h>.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H

#include <stdint.h>

typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef int16_t BOOL;
typedef void *APTR;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#endif
