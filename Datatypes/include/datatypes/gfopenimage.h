/*
 * gfopenimage datatypes: attributes of their own, for players and
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
 * no tag the datatype's default. For an encoder that trades time for size
 * (webp's effort) 0 is the fastest and 9 the smallest; an MP4's H.264 is
 * fastest at 0, its default, and searches most at 9. A datatype whose
 * format has no choice ignores it. */
#define OIA_Compression (OIA_Dummy + 4)

/* ---- Pictures with more than one frame (gfopengif.datatype) ----------------- */

/* (ULONG, G) How many frames the picture has: 1 for a still. The object's
 * own picture (what PDTM_READPIXELARRAY gives) is frame 0, or the one
 * PDTA_WhichPicture named at OM_NEW. PDTA_GetNumPictures answers the same. */
#define OIA_Frames     (OIA_Dummy + 5)

/* (LONG, G) How many times the frames play: 0 for ever, -1 when the file
 * says nothing (play them once), else the number the file gives (a GIF's
 * NETSCAPE2.0 loop count: browsers show that many repeats after the first
 * time through). */
#define OIA_LoopCount  (OIA_Dummy + 6)

/* ---- Raw writes in another format (DTM_WRITE, DTWM_RAW) ------------------------ */

/* (ULONG, in DTM_WRITE's dtw_AttrList) The file a raw write makes, a FOURCC,
 * for the datatypes that write more than one kind:
 *   'MP4 '  (OIWF_MP4) any film: anim, cdxl, webm, gfopenvideo, an animated
 *           GIF. H.264 baseline video and the sound as 8-bit PCM, written
 *           by animation.datatype itself on this CPU, no service needed; a
 *           subclass with a raw writer of its own hands this one to its
 *           superclass.
 *   'HEIC'  gfopenpicture.
 *   'M4A ', 'AAC ' (ADTS) or 'WMA '  gfopensound.
 *   'HEVC' (in MP4) or 'WMV '  gfopenvideo.
 * 'HEIC' to 'WMV ' are written by the service's FFmpeg (media.encode/1),
 * never on the 68k: with no services card or paired Cradle whose FFmpeg
 * writes them they fail with ERROR_NOT_IMPLEMENTED. Without the tag: the
 * object's own format when the service writes it (an AAC as M4A, unless
 * its name ends in .aac), or for an object made with no source (DTST_RAM)
 * the first of the list; any other datatype's own format, as before. A
 * format the datatype doesn't write fails with ERROR_NOT_IMPLEMENTED; it
 * never writes another instead. Datatypes that write one kind only
 * (pictures, sounds) ignore the tag. */
#define OIA_WriteFormat (OIA_Dummy + 16)
#define OIWF_MP4       0x4D503420UL    /* 'MP4 ' */

/* (LONG, in DTM_WRITE's dtw_AttrList) A lossy raw write's quality, 1 (the
 * smallest file) to 100 (the best); 0, -1 or no tag: the format's default.
 * MP4: H.264's quantiser 10 + (100 - quality) x 41 / 100, rounded; the
 * default 60, QP 26. */
#define OIA_Quality    (OIA_Dummy + 17)

/* (BOOL, in DTM_WRITE's dtw_AttrList) FALSE leaves the sound out of a
 * film's raw write. MP4: TRUE by default, the film's sound as 8-bit PCM,
 * which browsers don't play. */
#define OIA_WriteSound (OIA_Dummy + 18)

/* Writes frame oirf_Frame, composited over the frames before it as web
 * browsers show it, into the program's pixels: the whole picture, four
 * bytes a pixel, 0xAARRGGBB words (A, R, G, B in memory, WebCore's ARGB32
 * and PBPAFMT_ARGB), rows oirf_Stride bytes apart. With OIRF_PREMULTIPLIED
 * the colour comes premultiplied by alpha. Out: the frame's delay in
 * milliseconds, as the file gives it (a program decides what a delay of 0
 * means), and whether any pixel is not fully opaque. TRUE, or FALSE with
 * IoErr() set (a frame out of range is ERROR_OBJECT_NOT_FOUND).
 *
 * Going forward from the frame read last is cheap; going back starts again
 * from frame 0. Send it with DoMethodA(), not DoDTMethodA() (see
 * PDTM_READPIXELARRAY in gfopenpng.datatype). One task at a time per
 * object. */
#define OIM_READFRAME  (TAG_USER + 0x0DA15902)
struct oiReadFrame {
    ULONG MethodID;
    struct GadgetInfo *oirf_GInfo;     /* NULL */
    ULONG oirf_Frame;
    UBYTE *oirf_Pixels;
    ULONG oirf_Stride;                 /* bytes from one row to the next */
    ULONG oirf_Flags;                  /* OIRF_ */
    ULONG oirf_Delay;                  /* out: milliseconds */
    ULONG oirf_HasAlpha;               /* out: TRUE when any pixel is not opaque */
};
#define OIRF_PREMULTIPLIED 1UL

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

/* ---- Sounds read and written by programs (gfopensound, gfopenmodule,
 * gfopenaiff and gfopenwave.datatype) --------------------------------------- */

/* A converter's way in and out. Opened with OIA_Reader, a sound sets up
 * no playing: its PCM comes, a piece at a time, from OIM_READSOUND,
 * streamed from the file at the sound's own rate (a module: mixed at
 * OIA_MixRate), so nothing is decoded whole. A program asks OIA_Channels
 * first: a datatype that answers it answers OIM_READSOUND; one that
 * doesn't has the sound in SDTA_Sample as sound.datatype has always kept
 * it. Saving goes the other way: a DTST_RAM object of gfopenaiff or
 * gfopenwave.datatype made with OIA_PCM, written with DTM_WRITE (DTWM_RAW). */

/* (BOOL, I) Open for reading: OIM_READSOUND, no playing. */
#define OIA_Reader     (OIA_Dummy + 7)

/* (ULONG, I) gfopenmodule.datatype read with OIA_Reader: the rate it
 * mixes at, 4000 to 96000 (default 44100). */
#define OIA_MixRate    (OIA_Dummy + 8)

/* (ULONG, I) gfopenmodule.datatype read with OIA_Reader: 0 nearest,
 * 1 linear, 2 spline (default 2). */
#define OIA_MixInterp  (OIA_Dummy + 9)

/* (ULONG, IG) Channels in OIM_READSOUND's frames and in OIA_PCM: 1 or 2. */
#define OIA_Channels   (OIA_Dummy + 10)

/* (WORD *, I) gfopenaiff and gfopenwave.datatype with DTA_SourceType
 * DTST_RAM: the sound, 16-bit, interleaved when stereo, SDTA_SampleLength
 * frames at SDTA_SamplesPerSec; the datatype copies it. */
#define OIA_PCM        (OIA_Dummy + 11)

/* (ULONG, in DTM_WRITE's dtw_AttrList) Bits a sample in the file written,
 * 8 or 16 (default 16). The program dithers; the datatype takes the high
 * byte. */
#define OIA_SampleBits (OIA_Dummy + 12)

/* (BOOL, in DTM_WRITE's dtw_AttrList) Only the samples, no header: raw
 * PCM, big-endian from gfopenaiff.datatype, little-endian from gfopenwave
 * (8-bit: signed from gfopenaiff, unsigned from gfopenwave, as each format
 * keeps them). */
#define OIA_Headerless (OIA_Dummy + 13)

/* Writes up to oirs_Frames frames of the sound, 16-bit signed big-endian,
 * interleaved when OIA_Channels is 2, at oirs_Buffer, from where the last
 * call stopped. Answers how many it wrote: 0 at the end, and 0 with
 * IoErr() set when it can't. */
#define OIM_READSOUND  (TAG_USER + 0x0DA15903)
struct oiReadSound {
    ULONG MethodID;
    struct GadgetInfo *oirs_GInfo;     /* NULL */
    WORD *oirs_Buffer;
    ULONG oirs_Frames;
};

/* sound.datatype V44's own (NDK 3.5 and later), for headers that lack them */
#ifndef SDTA_SampleType
#define SDTA_SampleType (DTA_Dummy + 500 + 30)
#define SDTA_Frequency  (DTA_Dummy + 500 + 32)
#define SDTST_M8S  0
#define SDTST_S8S  1
#define SDTST_M16S 2
#define SDTST_S16S 3
#endif

#endif
