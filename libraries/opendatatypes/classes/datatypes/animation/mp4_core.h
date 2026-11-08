/* GFOpenDatatypes animation.datatype: films written as MP4 (H.264 video,
 * and the film's sound as 8-bit PCM), in plain C with no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The Team's ruling of 6 November 2026 (docs/DESIGN.md, section 3): "mp4
 * we merge in". Any animation.datatype subclass (anim, cdxl, webm,
 * gfopenvideo, an animated GIF) is written as an MP4 by DTM_WRITE in raw
 * mode with OIA_WriteFormat OIWF_MP4 in its dtw_AttrList
 * (datatypes/gfopenimage.h): the class asks the subclass for each frame as
 * the IFF ANIM writer does, turns its planes and colours (with HAM6, HAM8
 * and Extra Half-Brite) into 4:2:0 YUV and encodes it with the H.264
 * encoder in mp4_h264.c, here on this CPU: no service, no FPU, no
 * 64-bit multiplies or divides.
 *
 * Clean-room, from ISO/IEC 14496-12 (the ISO base media file format) and
 * 14496-15 (AVC in it: avc1, avcC), QuickTime's 'twos' sound description,
 * ITU-R BT.601 and the Amiga Hardware Reference Manual (HAM and EHB).
 */
#ifndef OPENDATATYPES_MP4_CORE_H
#define OPENDATATYPES_MP4_CORE_H

#include "animation_core.h"

/* ---- datatypes/gfopenimage.h's DTM_WRITE choices ------------------------------- */

#define ADT_OIA_Dummy       (ODT_TAG_USER + 0x0DA15000UL)
#define ADT_OIA_Compression (ADT_OIA_Dummy + 4)    /* 0 (fastest) to 9 (smallest); -1 the default */
#define ADT_OIA_WriteFormat (ADT_OIA_Dummy + 16)   /* what DTWM_RAW writes: a FOURCC */
#define ADT_OIA_Quality     (ADT_OIA_Dummy + 17)   /* 1 to 100; 0, -1 or none the default */
#define ADT_OIA_WriteSound  (ADT_OIA_Dummy + 18)   /* FALSE: the sound left out */
#define ADT_OIWF_MP4        0x4D503420UL           /* 'MP4 ' */
#define ADT_ERROR_NOT_IMPLEMENTED 236UL            /* dos/dos.h: a format the class can't write */

/* The defaults: quality 60 (H.264's QP 26) and level 0 (minih264e's speed 10, the fastest) */
#define ADT_MP4_QUALITY 60
#define ADT_MP4_LEVEL   0

/* OIA_Quality to H.264's quantiser: 100 is QP 10, 1 is QP 51; 0 or less the default */
odt_u32 adt_mp4_qp(odt_s32 quality);
/* OIA_Compression to minih264e's speed: 0 is speed 10 (the fastest), 1 is
 * 9, 2 is 8, 3 to 7 are 7, 8 is 1, 9 is speed 0 (the slowest) */
odt_u32 adt_mp4_speed(odt_s32 level);

/* ---- a frame in 4:2:0 YUV ------------------------------------------------------ */

#define ADT_MODE_EHB 0x0080UL               /* graphics/modeid.h's EXTRAHALFBRITE_KEY */

/* A frame's colours, and each one's Y, U and V terms */
struct adt_mp4_pal {
    odt_u8 rgb[768];                    /* R, G, B */
    odt_u8 y[256];                      /* BT.601 studio-range Y */
    odt_s16 u[256], v[256];             /* -38R - 74G + 112B, 112R - 94G - 18B: four add up to U and V */
    odt_u16 yr[256], yg[256], yb[256];  /* 66R + 128, 129G, 25B: Y for HAM's pixels */
};

/* The 256 colours a frame's pixels index from ncolors R, G, B (the rest
 * black); with Extra Half-Brite (EHB in modeid, six planes, no HAM) 32 to
 * 63 are 0 to 31 at half brightness. */
void adt_mp4_palette(const odt_u8 *rgb, odt_u32 ncolors, odt_u32 depth, odt_u32 modeid, struct adt_mp4_pal *pal);

/* adt_mp4_yuv's row: bytes for a frame cw pixels wide */
#define ADT_MP4_ROW(cw) (6 * (cw) + 32)

/* Frame bm's w x h pixels (depth planes; past the bitmap's depth, rows or
 * bytes, 0; a plane pointer of ~0 is all ones) through pal, or held and
 * modified (HAM in modeid: HAM6 up to six planes, else HAM8; each row
 * starts from colour 0, a modified gun is its bits repeated, as 4 bits to
 * 8 do: v << 4 | v), into BT.601 studio-range Y (cw x ch, rows ys apart)
 * and U and V (cw/2 x ch/2, rows ys/2 apart), cw and ch w and h rounded up
 * to even: an odd last column or row is the one before it again. row is
 * ADT_MP4_ROW(cw) bytes. */
void adt_mp4_yuv(const struct adt_BitMap *bm, odt_u32 w, odt_u32 h, odt_u32 depth, odt_u32 modeid,
                 const struct adt_mp4_pal *pal, odt_u8 *y, odt_u8 *u, odt_u8 *v, odt_u32 ys, odt_u8 *row);

/* ---- writing ------------------------------------------------------------------- */

/* What a frame brings beyond its planes (from ADTM_LOADFRAME's adtFrame) */
struct adt_mp4_frame {
    odt_u32 duration;                   /* alf_Duration: in 1/fps seconds; 0 is 1 */
    const odt_u8 *rgb;                  /* alf_CMap's colours as R, G, B, or NULL: the film's */
    odt_u32 ncolors;
    const odt_s8 *left, *right;         /* its sound: left alone is mono; NULL, none */
    odt_u32 sound_len;                  /* bytes of each */
    odt_u32 rate;                       /* its samples a second; 0, the film's */
};

struct adt_mp4_spec {
    odt_u32 width, height, depth;       /* depth 1 to 8 */
    odt_u32 frames;                     /* 1 or more */
    odt_u32 fps;                        /* 1 or more */
    odt_u32 ncolors;                    /* ADTA_ColorRegisters */
    const struct adt_ColorRegister *colors;
    odt_u32 modeid;                     /* ADTA_ModeID (HAM, EHB); 0 none */
    odt_u32 qp;                         /* 10 to 51 */
    odt_u32 speed;                      /* 0 to 10 */
    odt_u32 gop;                        /* a key frame every gop frames; 0 the first only */
    odt_u8 sound;                       /* 0: no sound track, whatever the film has */
    odt_u8 pad[3];
    /* the film's sound as one sample (ADTA_Sample, or ADTA_LeftSample and
     * ADTA_RightSample), from frame 0: NULL when there is none, or when
     * the frames bring their own */
    const odt_s8 *left, *right;
    odt_u32 sound_len;
    odt_u32 rate;                       /* samples a second (clock / ADTA_Period, or ADTA_SamplesPerSec) */
};

struct adt_mp4_io {
    struct adt_aw_io aw;                /* alloc, free, put, patch, frame, done: as the ANIM writer's */
    /* frame f's extras, between aw.frame(f) and aw.done(f); NULL: none */
    void (*info)(void *h, odt_u32 f, struct adt_mp4_frame *fi);
};

/* Writes the film as an MP4: ftyp, then mdat (each frame's H.264 access
 * unit, the SPS and PPS taken out to the avcC, then its sound), then moov:
 * an avc1 track (stss lists the key frames; stts each frame's duration),
 * and when there is sound and s->sound a 'twos' track (8-bit signed PCM,
 * mono or stereo, at the first rate given), a chunk a frame. The mdat's
 * size is patched last, so the file must be seekable. 0, or an IoErr()
 * code. */
odt_s32 adt_mp4_write(const struct adt_mp4_spec *s, const struct adt_mp4_io *io);

#endif
