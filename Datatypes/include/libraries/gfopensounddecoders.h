#ifndef LIBRARIES_GFOPENSOUNDDECODERS_H
#define LIBRARIES_GFOPENSOUNDDECODERS_H
/*
 * gfopensound-decoders.library: gfopensound.datatype's decoders on this CPU,
 * on disk (LIBS:). Ogg Vorbis (stb_vorbis), MP3 (dr_mp3) and FLAC (dr_flac),
 * from a file in memory, a piece at a time, to 16-bit samples
 * (opensound/snddec.c, the same code a datatype built with SNDDEC=builtin
 * carries in itself).
 *
 * gfopensound.datatype as built for the ROM carries no decoder: it sends
 * these formats to media.decode/1 through mitchy.library (the services card)
 * and, without the card, opens this library. Without either it says no.
 *
 * Every call runs on the caller's stack; dr_mp3 wants some 16 KB of it for
 * a frame (the datatype calls with 64 KB, dt_call_with_stack). The library
 * opens the ROM's IEEE maths libraries (stb_vorbis and dr_mp3 use floats).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. stb_vorbis and dr_libs are public
 * domain (or MIT, MIT-0).
 */
#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#define SNDDECNAME    "gfopensound-decoders.library"
#define SNDDECVERSION 47

/* SndDecKind's answers */
#define SNDDEC_NONE   0
#define SNDDEC_VORBIS 1
#define SNDDEC_MP3    2
#define SNDDEC_FLAC   3

/* SndDecOpen fills it */
struct SndDecInfo {
    ULONG sdi_Channels;
    ULONG sdi_Rate;
    ULONG sdi_Frames;        /* 0: not known */
};

/* LVOs (bias 30, after Open, Close, Expunge and the reserved one):
 *   -30 ULONG SndDecKind(CONST APTR data (a0), ULONG size (d0))
 *   -36 APTR  SndDecOpen(CONST APTR data (a0), ULONG size (d0), struct SndDecInfo *info (a1))
 *           NULL when no decoder takes it or it is broken; data stays the
 *           caller's and must live until SndDecClose
 *   -42 ULONG SndDecRead(APTR dec (a0), WORD *out (a1), ULONG frames (d0))
 *           up to frames frames, interleaved; 0 at the end
 *   -48 LONG  SndDecRewind(APTR dec (a0))    0, or -1
 *   -54 VOID  SndDecClose(APTR dec (a0))     NULL is allowed
 */

#endif
