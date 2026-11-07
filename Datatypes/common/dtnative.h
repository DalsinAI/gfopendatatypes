/*
 * gfopenimage datatypes: our own picture decoders run on the services card
 * (media.decode/1's NDECODE, gfopenservice docs/MEDIA_DECODE.md), built on
 * the host from these same sources, so the pixels are the ones this CPU
 * would make, byte for byte, in a fraction of the time. Only on the card
 * (OSCMD_WHERE): over the LAN the file and the pixels each way cost more
 * than the decoding saves. ENV:GFOpenImage/CardDecode set to 0 keeps every
 * picture on this CPU.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTNATIVE_H
#define DTNATIVE_H

#include <exec/types.h>

#define DT_NATIVE_PNG  0x504e4720UL   /* 'PNG ' */
#define DT_NATIVE_TGA  0x54474120UL   /* 'TGA ' */
#define DT_NATIVE_GIF  0x47494620UL   /* 'GIF ': mode 0 the screen after a frame as ARGB, 1 frame 0's indexes */
#define DT_NATIVE_WEBP 0x57454250UL   /* 'WEBP' */

#define DT_NATIVE_ALPHA 1UL           /* info[2] bit 0 */

/* The file decoded on the card by decoder (mode, frame as NDECODE takes
 * them) into pixels (room bytes): ARGB words, or a GIF's indexes. info:
 * width, height, flags, one more (a GIF frame's pixels without a colour;
 * a WebP animation's first frame's place, x << 16 | y); *pw x *ph the
 * pixels' own size. TRUE when it was; FALSE to decode here (no card, an
 * older service, a file it refuses: the class's own decoder says why). */
BOOL dt_card_decode(ULONG decoder, ULONG mode, ULONG frame, const UBYTE *file, ULONG size, APTR pixels,
                    ULONG room, ULONG info[4], ULONG *pw, ULONG *ph);

#endif
