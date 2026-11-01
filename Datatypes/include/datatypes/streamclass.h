/*
 * stream.datatype: the "data" class of the datatypes system, beside
 * picture.datatype and sound.datatype. An object of it turns one format of
 * bytes into another: gzip.datatype (gzip, zlib and raw deflate) and
 * brotli.datatype are its first subclasses. A program reads the plain
 * bytes either all at once, or a piece at a time as the coded bytes arrive
 * (an HTTP body), with memory that stays bounded however long the stream.
 *
 * Whole buffer: open the source as any datatype, from a file or from
 * memory, naming the datatype when the data has no magic bytes (Brotli has
 * none, so a browser names it from the Content-Encoding header):
 *
 *     o = NewDTObject(name, DTA_GroupID, GID_DATA, TAG_DONE);
 *     o = NewDTObject(NULL, DTA_SourceType, DTST_MEMORY,
 *                     DTA_SourceAddress, buf, DTA_SourceSize, len,
 *                     DTA_BaseName, "brotli", DTA_GroupID, GID_DATA, TAG_DONE);
 *     GetDTAttrs(o, SDTA_Data, &data, SDTA_DataSize, &size, TAG_DONE);
 *
 * NewDTObject() fails on damaged data, with the reason in IoErr(). The
 * object owns SDTA_Data; it goes with DisposeDTObject().
 *
 * Streaming: create the object with no source (DTST_RAM) and feed it, or
 * open a file or memory source with SDTA_Streaming set, or give it a hook
 * that hands it the coded bytes when it needs them (SDTA_InputHook):
 *
 *     o = NewDTObject(NULL, DTA_SourceType, DTST_RAM, DTA_BaseName, "gzip",
 *                     DTA_GroupID, GID_DATA, TAG_DONE);
 *     for each piece of the body:
 *         DoMethod(o, SDTM_FEED, NULL, piece, length);
 *         while ((n = DoMethod(o, SDTM_TAKE, NULL, buf, sizeof buf, 0, 0)) > 0)
 *             use n bytes;
 *     DoMethod(o, SDTM_END, NULL);
 *     take what is left, until sdtt_Flags has SDTF_END (or SDTF_ERROR)
 *
 * Every message has a GadgetInfo pointer second, as DTM_WRITE's has, so
 * DoMethod() and DoDTMethod() both deliver it as written (pass NULL).
 *
 * DTM_WRITE with DTWM_RAW writes the plain bytes to dtw_FileHandle, so a
 * viewer can save a .gz file decoded. With SDTA_WriteCoded TRUE in its
 * dtw_AttrList it writes them coded again, in the format they came in,
 * where the subclass can (gzip.datatype: gzip, zlib or raw deflate, at
 * OIA_Compression's level when given); a subclass that can't refuses
 * (brotli.datatype). DTWM_IFF is refused.
 *
 * The group, GID_DATA, is new: our own datatypes library accepts it by
 * design; that AmigaOS 3.2's datatypes.library accepts a group it does not
 * know is still to be checked on the bench.
 *
 * Writing a decoder: subclass "stream.datatype" (version 47) and answer
 * SDTM_CODEC. The class does the rest: the sources, the input queue, the
 * whole-buffer decode, SDTM_FEED, SDTM_TAKE and SDTM_END, DTM_WRITE and the
 * errors.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef DATATYPES_STREAMCLASS_H
#define DATATYPES_STREAMCLASS_H

#include <exec/types.h>
#include <utility/tagitem.h>

#define STREAMDTCLASS "stream.datatype"

/* The group of data streams, beside GID_PICTURE and GID_SOUND */
#ifndef GID_DATA
#define GID_DATA (((ULONG)'d' << 24) | ((ULONG)'a' << 16) | ((ULONG)'t' << 8) | (ULONG)'a')
#endif

/* ---- attributes --------------------------------------------------------- */

/* TAG_USER with the AmiCradle Team's number ($DA15) in the middle, as
 * datatypes/gfopenimage.h; the stream class's are $DA151xx. */
#define SDTA_StreamDummy   (TAG_USER + 0x0DA15100)

/* (UBYTE *, G) The plain bytes of a whole-buffer object, owned by it.
 * NULL for a streaming object. */
#define SDTA_Data          (SDTA_StreamDummy + 1)

/* (ULONG, G) How many there are. */
#define SDTA_DataSize      (SDTA_StreamDummy + 2)

/* (BOOL, I) TRUE: a file or memory source is read a piece at a time by
 * SDTM_TAKE instead of being decoded whole by NewDTObject(). Default FALSE. */
#define SDTA_Streaming     (SDTA_StreamDummy + 3)

/* (struct Hook *, I) Pulls coded bytes: when SDTM_TAKE has used up all
 * that was fed, the hook is called with the object and a struct
 * sdtInputMsg, fills up to sim_Length bytes at sim_Buffer and returns how
 * many; 0 means the stream has ended, (ULONG)-1 an error (its IoErr() is
 * taken as the reason). For a DTST_RAM object only. */
#define SDTA_InputHook     (SDTA_StreamDummy + 4)

/* (ULONG, G) Coded bytes the decoder has used, and plain bytes it has made,
 * so far. */
#define SDTA_TotalIn       (SDTA_StreamDummy + 5)
#define SDTA_TotalOut      (SDTA_StreamDummy + 6)

/* (STRPTR, G) The format, in a word: "gzip", "zlib", "deflate", "brotli".
 * NULL until the decoder knows. */
#define SDTA_Format        (SDTA_StreamDummy + 7)

/* (LONG, I) gzip.datatype: which wrapper to expect. SDTW_AUTO (the
 * default) tells gzip and zlib by their first bytes and takes anything
 * else as raw deflate, as browsers do for HTTP's "deflate". */
#define SDTA_Wrapper       (SDTA_StreamDummy + 8)
#define SDTW_AUTO    0
#define SDTW_GZIP    1
#define SDTW_ZLIB    2
#define SDTW_RAW     3

/* (BOOL, in DTM_WRITE's dtw_AttrList) TRUE: write the bytes coded again in
 * the format they came in, rather than plain. Only a whole-buffer object
 * can: a streaming one has handed its bytes out. Refused with
 * ERROR_NOT_IMPLEMENTED by a subclass that has no encoder. Default FALSE. */
#define SDTA_WriteCoded    (SDTA_StreamDummy + 9)

#define SDTM_INPUT   (TAG_USER + 0x0DA15805)
struct sdtInputMsg {
    ULONG MethodID;                 /* SDTM_INPUT */
    UBYTE *sim_Buffer;
    ULONG sim_Length;
};

/* ---- methods ------------------------------------------------------------ */

#define SDTM_Dummy   (TAG_USER + 0x0DA15800)

/* The next coded bytes. They are copied, so the buffer may be reused at
 * once. TRUE, or FALSE when memory runs out or the stream has already
 * ended (SDTM_END) or failed. */
#define SDTM_FEED    (SDTM_Dummy + 1)
struct sdtFeed {
    ULONG MethodID;
    struct GadgetInfo *sdtf_GInfo;  /* NULL */
    CONST_APTR sdtf_Data;
    ULONG sdtf_Length;
};

/* As many plain bytes as fit in sdtt_Buffer. Returns the count, also put in
 * sdtt_Actual, and sets sdtt_Flags:
 *   SDTF_END     the stream has ended and every plain byte has been taken
 *   SDTF_HUNGRY  nothing more without more coded bytes (SDTM_FEED or SDTM_END)
 *   SDTF_ERROR   the data is damaged or cut short, or memory ran out; the
 *                reason is in IoErr() and in sdtt_Error
 * Bytes taken before an error stay good. */
#define SDTM_TAKE    (SDTM_Dummy + 2)
struct sdtTake {
    ULONG MethodID;
    struct GadgetInfo *sdtt_GInfo;  /* NULL */
    APTR sdtt_Buffer;
    ULONG sdtt_Length;
    ULONG sdtt_Actual;              /* out */
    ULONG sdtt_Flags;               /* out */
    LONG sdtt_Error;                /* out: an IoErr() code, 0 if none */
};
#define SDTF_END     (1UL << 0)
#define SDTF_HUNGRY  (1UL << 1)
#define SDTF_ERROR   (1UL << 2)

/* No more coded bytes will come. What is left can still be taken; a stream
 * that stops short then fails with DTERROR_NOT_ENOUGH_DATA. */
#define SDTM_END     (SDTM_Dummy + 3)
struct sdtEnd {
    ULONG MethodID;
    struct GadgetInfo *sdte_GInfo;  /* NULL */
};

/* ---- for subclasses ----------------------------------------------------- */

/* The one method a decoder answers. The class sends it with sdtc_Op:
 *
 *   SDTCO_INIT  once, from the class's OM_NEW, before anything else, with
 *               the OM_NEW tags in sdtc_Tags. The subclass's own OM_NEW has
 *               not returned yet: keep the decoder's state in the instance
 *               data (it starts cleared) or allocate it here. Return
 *               SDTC_ERROR (with sdtc_Error) to refuse the object.
 *   SDTCO_RUN   decode: take coded bytes from sdtc_In (sdtc_InLen of them),
 *               put plain ones at sdtc_Out (room for sdtc_OutLen), and say
 *               how many of each in sdtc_InUsed and sdtc_OutMade.
 *               SDTCF_LAST in sdtc_Flags: no coded bytes after these.
 *               Return SDTC_MORE when it stopped because input or room ran
 *               out (use all the input you can before saying so),
 *               SDTC_DONE once the stream has ended and every plain byte is
 *               out (input after the end is left), SDTC_ERROR on damaged
 *               data, with an IoErr() code in sdtc_Error.
 *   SDTCO_FREE  once, from the class's OM_DISPOSE: free the state. It may
 *               come without an INIT that succeeded.
 *
 * MORE with nothing used and nothing made, given SDTCF_LAST, means the
 * stream was cut short: the class says DTERROR_NOT_ENOUGH_DATA. */
#define SDTM_CODEC   (SDTM_Dummy + 4)
struct sdtCodec {
    ULONG MethodID;
    struct GadgetInfo *sdtc_GInfo;  /* NULL */
    ULONG sdtc_Op;
    ULONG sdtc_Flags;
    const UBYTE *sdtc_In;
    ULONG sdtc_InLen;
    UBYTE *sdtc_Out;
    ULONG sdtc_OutLen;
    ULONG sdtc_InUsed;              /* out */
    ULONG sdtc_OutMade;             /* out */
    LONG sdtc_Error;                /* out */
    struct TagItem *sdtc_Tags;      /* SDTCO_INIT only */
    STRPTR sdtc_Format;             /* out, any time: SDTA_Format's word (static text) */
};
#define SDTCO_INIT   0
#define SDTCO_RUN    1
#define SDTCO_FREE   2

#define SDTCF_LAST   (1UL << 0)

#define SDTC_MORE    0
#define SDTC_DONE    1
#define SDTC_ERROR   2

#endif
