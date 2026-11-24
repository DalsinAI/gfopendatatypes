/* (A copy of DalsinAI/gfosautoconfigboards cards/akikoplus/guest/include/libraries/akikoplus.h at 3fcced6, for the datatypes' services: common/dtservice.c.) */
/* Copyright (c) 2026 AmiCradle Team. MIT licence.
 *
 * akikoplus.library: helpers done natively on AmiCradle's AkikoPlus board,
 * and in 68k code where there is no board, so the same program runs anywhere.
 * The two give the same bytes (the zlib streams excepted: each side's
 * decompresses to the same data on both).
 *
 * Version 2 (9 November 2026) keeps version 1's calls, their offsets and
 * their structures, and goes to the board through mitchy.library (which
 * goes through gfopenservice.device, the owner of every ACSV board's rings).
 * New in 2: AP_Compress and AP_Decompress on the 68k too, AP_SetMode and
 * AP_Query. A build with AP_NO_FALLBACK (for machines that always have the
 * board) leaves the 68k versions out: AP_Status says so, and the calls answer
 * AP_ERR_NOBOARD without the board.
 *
 * Version 3 (24 November 2026) keeps version 2's calls, offsets and
 * structures, and adds the services: AP_OpenService, AP_CloseService,
 * AP_CallService, AP_ServiceWhere, AP_SendService, AP_WaitService and
 * AP_AbortService, so a program (the GFOpen datatypes) reaches
 * media.decode/1 and the other named services through akikoplus.library.
 *
 * A call small enough to be quicker on the 68k stays there (AP_SetThreshold).
 * Every call returns 0, or a negative AP_ERR_ (AP_Quantise returns the
 * colours, AP_Compress and AP_Decompress the bytes written).
 * The autodoc is akikoplus.doc; the calls' offsets are sfd/akikoplus_lib.sfd's.
 */
#ifndef LIBRARIES_AKIKOPLUS_H
#define LIBRARIES_AKIKOPLUS_H

#include <exec/types.h>
#include <exec/ports.h>

#define AKIKOPLUS_NAME "akikoplus.library"
#define AKIKOPLUS_VERSION 3
#define AKIKOPLUS_SERVICES_VERSION 3   /* the first with AP_OpenService ... AP_AbortService */

#define AP_ERR_BADARGS  (-1)
#define AP_ERR_NOBOARD  (-2)   /* a board-only build (AP_NO_FALLBACK) on a machine without the board */
#define AP_ERR_TOOSMALL (-4)   /* the destination is too small */
#define AP_ERR_NOMEM    (-5)

/* AP_Status */
#define APSF_BOARD    1        /* the board's services are open */
#define APSF_FALLBACK 2        /* (2) the 68k versions are built in */

/* AP_SetMode (2): where the work runs. */
#define APMODE_AUTO   0        /* the board when fitted and the job reaches AP_SetThreshold's bytes */
#define APMODE_68K    1        /* the 68k always */
#define APMODE_BOARD  2        /* the board always, when fitted (else the 68k) */

/* AP_Query (2) */
#define APQ_SERVICES  1        /* APF_* of the board's services that are open */
#define APQ_METHOD    2        /* the board's METHOD: 0 plain C, 1 SSSE3, 2 AVX2, 3 NEON; -1 no board */
#define APQ_MODE      3        /* APMODE_* */
#define APQ_THRESHOLD 4        /* AP_SetThreshold's bytes */
#define APQ_LASTRAN   5        /* the last call, from any task: 1 the board, 0 the 68k, -1 none yet */

#define APF_SWAP     (1UL << 0)   /* akiko.swap/1 */
#define APF_C2P      (1UL << 1)   /* akiko.c2p/1 */
#define APF_FILL     (1UL << 2)   /* akiko.fill/1 */
#define APF_SCALE    (1UL << 3)   /* akiko.scale/1 */
#define APF_CONVERT  (1UL << 4)   /* akiko.convert/1 */
#define APF_Z        (1UL << 5)   /* akiko.z/1 */

struct APC2P {
    const UBYTE *ap_Chunky;
    UBYTE *ap_Planes;
    ULONG ap_ChunkyLen, ap_PlanesLen;
    UWORD ap_Width, ap_Height, ap_Depth, ap_Pad;   /* depth 1-8 */
    ULONG ap_ChunkyPitch;
    ULONG ap_RowStride;        /* bytes from a row of a plane to its next */
    ULONG ap_PlaneStride;      /* bytes from a plane to the next */
};
struct APFill {
    UBYTE *ap_Dest;
    ULONG ap_DestLen;
    UWORD ap_Width, ap_Height, ap_BytesPerPixel, ap_Pad;   /* 1-4 */
    ULONG ap_Pitch;
    ULONG ap_Pixel;            /* big-endian in its low bytes */
};
struct APCopy {
    const UBYTE *ap_Src;
    UBYTE *ap_Dest;
    ULONG ap_SrcLen, ap_DestLen;
    UWORD ap_Width, ap_Height, ap_BytesPerPixel, ap_Pad;
    ULONG ap_SrcPitch, ap_DestPitch;
};
#define APSCALE_NEAREST 1
#define APSCALE_BILINEAR 2     /* 3 or 4 bytes a pixel */
struct APScale {
    const UBYTE *ap_Src;
    UBYTE *ap_Dest;
    ULONG ap_SrcLen, ap_DestLen;
    UWORD ap_SrcWidth, ap_SrcHeight, ap_DestWidth, ap_DestHeight;
    ULONG ap_SrcPitch, ap_DestPitch;
    UWORD ap_BytesPerPixel, ap_Filter;
};
struct APYUV {                 /* 4:2:0, BT.601, studio range, to ARGB32 */
    const UBYTE *ap_Y, *ap_U, *ap_V;
    UBYTE *ap_Dest;
    ULONG ap_YLen, ap_ULen, ap_VLen, ap_DestLen;
    UWORD ap_Width, ap_Height;
    ULONG ap_YPitch, ap_UVPitch, ap_DestPitch;
};
struct APQuant {               /* ARGB32 to 8-bit, median cut */
    const UBYTE *ap_Src;
    UBYTE *ap_Dest;
    UBYTE *ap_Palette;         /* 4 bytes a colour: 0, R, G, B */
    ULONG ap_SrcLen, ap_DestLen, ap_PaletteLen;
    UWORD ap_Width, ap_Height;
    ULONG ap_SrcPitch, ap_DestPitch;
    ULONG ap_Colours;          /* 2-256 */
};

/* ---- Services (3) ---------------------------------------------------------
 * Any service by name and version ("media.decode/1", "media.encode/1",
 * "media.cdxl/1", "doc.render/1"), opened with AP_OpenService: on the
 * AkikoPlus board when it is fitted (the media.* services are the board's
 * then), else on Mitchy, the services card, or a paired Cradle on the LAN.
 * The library goes through mitchy.library (1), and so through
 * gfopenservice.device, which owns the boards' rings. A call is the
 * service's op, flags (buffer i written by the service when APSF_WRITTEN(i)
 * is set), arg, four buffers and four op-specific longs, as on the card's
 * ring (AmiCradle's SERVICES_CARD.md); the answer is a status (APS_OK, an
 * APSERR_ or the service's own negative one), a result and an aux. */
#define APOF_BOARDONLY   (1UL << 0)   /* AP_OpenService: a board in this machine only, never the LAN */

#define AP_WHERE_NONE    0            /* AP_ServiceWhere: nowhere any more (its Cradle was lost) */
#define AP_WHERE_BOARD   1            /* a board in this machine: its host does the work */
#define AP_WHERE_LAN     2            /* a paired Cradle on the LAN */

#define APS_OK             0
#define APSERR_NOSERVICE (-1)         /* no board or Cradle offers it */
#define APSERR_BADREQUEST (-2)
#define APSERR_CANCELLED (-3)         /* AP_AbortService */
#define APSERR_TOOSMALL  (-4)         /* a written buffer is too small: the result says how much is needed */
#define APSERR_HOST      (-5)         /* the host failed or is busy (retry) */
#define APSERR_LOST      (-6)         /* the board or the Cradle was lost */
#define APSERR_NODEVICE  (-100)       /* gfopenservice.device would not open */
#define APSERR_NOMEMORY  (-101)
#define APSERR_BUSY      (-102)
#define APSERR_NOTFITTED (-103)       /* no ACSV board in this machine, and no paired Cradle offers it */
#define APSERR_NOSIGNAL  (-104)
#define APSERR_NOMITCHY  (-110)       /* mitchy.library 1 is not installed */

#define APSF_WRITTEN(i)  (1UL << (i))

struct APBuffer {
    APTR  ab_Data;             /* any memory, Chip or Fast; length 0: unused */
    ULONG ab_Length;
};

struct APServiceCall {
    UWORD asc_Op;              /* the service's operation */
    UWORD asc_Pad;
    ULONG asc_Flags;           /* APSF_WRITTEN(i) */
    ULONG asc_Arg;
    struct APBuffer asc_Buf[4];
    ULONG asc_Extra[4];        /* op-specific */
};

#endif
