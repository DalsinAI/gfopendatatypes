/*
 * acmedia.datatype: the route-opaque media stream class.
 *
 * A program feeds encoded bytes and controls an ordinary animation object.
 * The class hands the stream to akikoplus.device.  The device, not the
 * program or datatype API, chooses the implementation rung and output routes.
 * There is deliberately no force-rung, selected-rung, path or route attribute.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef DATATYPES_ACMEDIACLASS_H
#define DATATYPES_ACMEDIACLASS_H

#include <exec/types.h>
#include <utility/tagitem.h>

#define ACMEDIADTCLASS "acmedia.datatype"

/* Signed microseconds without compiler-dependent 64-bit structure layout. */
struct amdtTime { LONG hi; ULONG lo; };

#define AMDTA_Dummy          (TAG_USER + 0x0DA15200)
#define AMDTA_BufferLimit    (AMDTA_Dummy + 1)  /* I/G: admitted encoded bytes */
#define AMDTA_BufferedBytes  (AMDTA_Dummy + 2)  /* G */
#define AMDTA_LowWater       (AMDTA_Dummy + 3)  /* I/G: NEED edge */
#define AMDTA_HighWater      (AMDTA_Dummy + 4)  /* I/G: FEED limit */
#define AMDTA_Generation     (AMDTA_Dummy + 5)  /* G: non-zero object generation */
#define AMDTA_CurrentTime    (AMDTA_Dummy + 6)  /* G: device clock, microseconds */
#define AMDTA_Rate           (AMDTA_Dummy + 7)  /* I/S/G: 16.16 */
#define AMDTA_Volume         (AMDTA_Dummy + 8)  /* I/S/G */
#define AMDTA_Muted          (AMDTA_Dummy + 9)  /* I/S/G */
#define AMDTA_Overlay        (AMDTA_Dummy + 10) /* I/S, copied atomically */
#define AMDTA_NotifyHook     (AMDTA_Dummy + 11) /* I/S, called by class process */
#define AMDTA_VideoWidth     (AMDTA_Dummy + 12) /* G */
#define AMDTA_VideoHeight    (AMDTA_Dummy + 13) /* G */

struct amdtRect { LONG left, top, width, height; };
struct amdtOverlay {
    struct amdtRect box;
    const struct amdtRect *clip;
    ULONG clips, visible;
};

#define AMDTM_Dummy          (TAG_USER + 0x0DA15900)

/* MIME includes codecs= when known. The class copies the string. */
#define AMDTM_ADDTRACK       (AMDTM_Dummy + 1)
struct amdtAddTrack {
    ULONG MethodID;
    struct GadgetInfo *amat_GInfo;
    CONST_STRPTR amat_Mime;
    ULONG amat_Flags;
};
#define AMDT_TRACK_VIDEO     (1UL << 0)
#define AMDT_TRACK_AUDIO     (1UL << 1)

/* The class copies Data before returning. Generation must equal the object's
 * current generation. End and append-window times are exclusive. FEED_END is
 * a zero-length terminal marker; use AMDTM_END for the equivalent explicit
 * per-track operation. */
#define AMDTM_FEED           (AMDTM_Dummy + 2)
struct amdtFeed {
    ULONG MethodID;
    struct GadgetInfo *amf_GInfo;
    ULONG amf_Track, amf_Generation;
    CONST_APTR amf_Data;
    ULONG amf_Length, amf_Flags;
    ULONG amf_OffsetHi, amf_OffsetLo;
    struct amdtTime amf_Start, amf_End;
    struct amdtTime amf_TimestampOffset;
    struct amdtTime amf_AppendStart, amf_AppendEnd;
};
#define AMDT_FEED_INIT          (1UL << 0)
#define AMDT_FEED_DISCONTINUITY (1UL << 1)
#define AMDT_FEED_END           (1UL << 2)
#define AMDT_FEED_RESET_TRACK   (1UL << 3) /* MSE abort/changeType */

#define AMDTM_REMOVE         (AMDTM_Dummy + 3)
struct amdtRemove {
    ULONG MethodID;
    struct GadgetInfo *amr_GInfo;
    ULONG amr_Track;
    struct amdtTime amr_Start, amr_End;
};

/* Increments generation, cancels queued old work and retains object/session. */
#define AMDTM_SEEK           (AMDTM_Dummy + 4)
struct amdtSeek {
    ULONG MethodID;
    struct GadgetInfo *ams_GInfo;
    struct amdtTime ams_Time;
    ULONG ams_Flags;
};
#define AMDT_SEEK_EXACT      (1UL << 0)

/* Terminal input for one track, unlike temporary starvation. */
#define AMDTM_END            (AMDTM_Dummy + 5)
struct amdtEnd {
    ULONG MethodID;
    struct GadgetInfo *ame_GInfo;
    ULONG ame_Track, ame_Generation;
};

#define AMDTE_NEED           (1UL << 0)
#define AMDTE_FORMAT         (1UL << 1)
#define AMDTE_READY          (1UL << 2)
#define AMDTE_SEEKED         (1UL << 3)
#define AMDTE_END            (1UL << 4)
#define AMDTE_ERROR          (1UL << 5)
#define AMDTE_METADATA       (1UL << 6)
struct amdtEvent {
    ULONG MethodID, ame_Events, ame_Track, ame_Generation;
    LONG ame_Error;
};

#endif
