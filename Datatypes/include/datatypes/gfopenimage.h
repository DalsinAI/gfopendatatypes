/*
 * openamigaimage datatypes: attributes of their own, for players and
 * viewers that want to say more than the standard ones do. A datatype that
 * doesn't know one answers OM_GET with FALSE, so a program asks and falls
 * back. TAG_USER with the AmiCradle Team's manufacturer number ($DA15) in the middle.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DATATYPES_OPENIMAGE_H
#define DATATYPES_OPENIMAGE_H

#include <utility/tagitem.h>

#define OIA_Dummy      (TAG_USER + 0x0DA15000)

/* (STRPTR, G) What decodes or plays this object, in words for a status
 * line: "a cores board core, through AHI", "media.decode/1 (the Nursery),
 * through AHI", "this Amiga's CPU, through Paula (no AHI)". Valid while
 * the object lives; it may change once it plays (gfopenmodule.datatype
 * moves to this CPU when a core fails it). */
#define OIA_DecodedBy  (OIA_Dummy + 1)

/* (STRPTR, G) How that has gone, measured: "22% of a 68040 at 27928 Hz",
 * "48 jobs on core 1, 3.1 ms each". For an info window or a log. */
#define OIA_Stats      (OIA_Dummy + 2)

/* (ULONG, G) gfopenmodule.datatype: who mixes it, 1 this CPU, 2 a cores
 * board core, 3 a service (media.decode/1). (Numbers, not the order they
 * are tried in: a core, then the service, then this CPU.) */
#define OIA_Rung       (OIA_Dummy + 3)

/* (LONG, in DTM_WRITE's dtw_AttrList) How much a raw write compresses: 0
 * stores (gfopentga.datatype: no run-length encoding; gfopenpng.datatype:
 * deflate level 0), 1 to 9 a deflate level where the format has one, -1 or
 * no tag the datatype's default. A datatype whose format has no choice
 * ignores it. */
#define OIA_Compression (OIA_Dummy + 4)

/* ---- gfopenpng.datatype: PNG out --------------------------------------------- */

/* Encodes 32-bit ARGB pixels to a PNG file in memory (canvas toDataURL and
 * toBlob): four bytes a pixel, A, R, G, B (0xAARRGGBB on the 68k, WebCore's
 * ARGB32 and PBPAFMT_ARGB), rows oiep_Stride bytes apart. With
 * oiep_Pixels NULL the object's own picture is encoded. The deflate is
 * z.library's. TRUE and the file in oiep_Data (AllocVec'd: the caller
 * FreeVec()s it) and oiep_Size, or FALSE with the reason in oiep_Error and
 * IoErr(). Any gfopenpng.datatype object answers it, one made with no source
 * too, and can be used again and again:
 *
 *     o = NewObject(NULL, "gfopenpng.datatype", DTA_SourceType, DTST_RAM, TAG_DONE);
 *         (after OpenLibrary("datatypes/gfopenpng.datatype", 47), which makes
 *         the class public; or NewDTObject(NULL, DTA_SourceType, DTST_RAM,
 *         DTA_BaseName, "gfopenpng", DTA_GroupID, GID_PICTURE, TAG_DONE))
 *     struct oiEncodePNG m = { OIM_ENCODEPNG, NULL, pixels, w, h, stride,
 *                              OIEF_PREMULTIPLIED, -1 };
 *     if (DoMethodA(o, (Msg)&m)) { use m.oiep_Data, m.oiep_Size; FreeVec(m.oiep_Data); }
 *
 * DTM_WRITE with DTWM_RAW writes the object's picture as PNG to a file the
 * same way. */
#define OIM_ENCODEPNG  (TAG_USER + 0x0DA15901)
struct oiEncodePNG {
    ULONG MethodID;
    struct GadgetInfo *oiep_GInfo;     /* NULL */
    const UBYTE *oiep_Pixels;          /* NULL: the object's picture */
    ULONG oiep_Width, oiep_Height;     /* ignored with oiep_Pixels NULL */
    ULONG oiep_Stride;                 /* bytes from one row to the next */
    ULONG oiep_Flags;                  /* OIEF_ */
    LONG oiep_Level;                   /* deflate level 0-9, or -1 for the default */
    UBYTE *oiep_Data;                  /* out */
    ULONG oiep_Size;                   /* out */
    LONG oiep_Error;                   /* out: an IoErr() code */
};
#define OIEF_PREMULTIPLIED 1UL         /* colour premultiplied by alpha: undone first */
#define OIEF_OPAQUE        2UL         /* leave alpha out: an RGB file */

#endif
