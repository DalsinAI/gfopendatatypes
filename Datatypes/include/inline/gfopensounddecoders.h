/* gfopensound-decoders.library (include/libraries/gfopensounddecoders.h):
 * inline calls for bebbo's m68k-amigaos-gcc.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef _INLINE_GFOPENSOUNDDECODERS_H
#define _INLINE_GFOPENSOUNDDECODERS_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif
#ifndef SNDDEC_BASE_NAME
#define SNDDEC_BASE_NAME SndDecBase
#endif
#define SndDecKind(data, size) \
    LP2(0x1e, ULONG, SndDecKind, APTR, data, a0, ULONG, size, d0, , SNDDEC_BASE_NAME)
#define SndDecOpen(data, size, info) \
    LP3(0x24, APTR, SndDecOpen, APTR, data, a0, ULONG, size, d0, struct SndDecInfo *, info, a1, , SNDDEC_BASE_NAME)
#define SndDecRead(dec, out, frames) \
    LP3(0x2a, ULONG, SndDecRead, APTR, dec, a0, WORD *, out, a1, ULONG, frames, d0, , SNDDEC_BASE_NAME)
#define SndDecRewind(dec) \
    LP1(0x30, LONG, SndDecRewind, APTR, dec, a0, , SNDDEC_BASE_NAME)
#define SndDecClose(dec) \
    LP1NR(0x36, SndDecClose, APTR, dec, a0, , SNDDEC_BASE_NAME)
#endif
