/* GFOpenDatatypes class libraries: what the picture and sound families
 * (picture, ilbm, sound, 8svx) ask of the system beyond struct odt_cenv:
 * struct dtc_ext, what env->class_ext points at. Plain C, no Amiga
 * headers: the cores built with it run under qemu-m68k.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the NDK 3.2 headers' published layouts (graphics/gfx.h,
 * intuition/screens.h) and the autodocs, never from Kickstart, Hyperion or
 * AROS code. Each class's ndk_check.c compares the constants and layouts
 * its core uses with the NDK 3.2 headers on the os32-gcc16 stove.
 */
#ifndef OPENDATATYPES_CLASSES_DTC_ENV_H
#define OPENDATATYPES_CLASSES_DTC_ENV_H

#include "../../../dtclass_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* dos/dos.h's errors the classes set */
#define DTC_ERROR_NO_FREE_STORE        103
#define DTC_ERROR_REQUIRED_ARG_MISSING 116
#define DTC_ERROR_OBJECT_WRONG_TYPE    212
#define DTC_ERROR_NOT_IMPLEMENTED      236
#define DTC_ERROR_SEEK_ERROR           219

/* graphics/gfx.h */
struct odt_BitMap {
    odt_u16 BytesPerRow, Rows;
    odt_u8 Flags, Depth;
    odt_u16 pad;
    odt_u8 *Planes[8];
};
ODT_STATIC_ASSERT(sizeof(struct odt_BitMap) == 40, "BitMap");

/* intuition/screens.h: the head of DrawInfo, and its pens */
struct odt_DrawInfo {
    odt_u16 dri_Version, dri_NumPens;
    odt_u16 *dri_Pens;
};
#define DTC_DETAILPEN     0
#define DTC_BLOCKPEN      1
#define DTC_TEXTPEN       2
#define DTC_SHINEPEN      3
#define DTC_SHADOWPEN     4
#define DTC_FILLPEN       5
#define DTC_BACKGROUNDPEN 7

/* One request to audio.device: a channel role and a stretch of chip memory */
struct dtc_audio_seg {
    odt_u8 roles;                       /* 1 the first channel of the set, 2 the second, 3 both */
    odt_u8 pad;
    odt_u16 volume;                     /* 0..64 */
    odt_u16 period;
    odt_u16 cycles;                     /* 0: for ever */
    const odt_s8 *data;
    odt_u32 length;                     /* bytes, even, at most DTC_AUDIO_MAXLEN */
};
#define DTC_AUDIO_MAXLEN  131070UL
#define DTC_AUDIO_MAXSEGS 64
struct dtc_audio_plan {
    odt_u8 alloc[4];                    /* the channel sets to ask audio.device for, in order */
    odt_u16 nalloc;
    odt_u16 nseg;
    odt_u16 repeat;                     /* play the segments this many times, 0 for ever */
    odt_u16 pad;
    void *signal_task;
    odt_u32 signal_mask;
    struct dtc_audio_seg seg[DTC_AUDIO_MAXSEGS];
};

/* The family's services. A NULL call is a service that isn't there. */
struct dtc_ext {
    /* AllocVec(n, MEMF_CHIP | MEMF_CLEAR); env->free frees it */
    void *(*alloc_chip)(const struct odt_cenv *env, odt_u32 n);

    /* graphics: AllocBitMap (friend may be NULL) and FreeBitMap; chunky
     * pixels into it (WriteChunkyPixels, or WritePixelArray for argb,
     * four bytes a pixel) */
    struct odt_BitMap *(*alloc_bitmap)(const struct odt_cenv *env, odt_u32 w, odt_u32 h, odt_u32 depth,
                                       struct odt_BitMap *friend_bm);
    void (*free_bitmap)(const struct odt_cenv *env, struct odt_BitMap *bm);
    void (*write_lut8)(const struct odt_cenv *env, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                       odt_u32 mod);
    void (*write_argb)(const struct odt_cenv *env, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                       odt_u32 mod);
    /* the screen: its depth (more than 8: true colour), colour map, bitmap */
    odt_u32 (*screen_depth)(const struct odt_cenv *env, void *screen);
    void *(*screen_colormap)(const struct odt_cenv *env, void *screen);
    struct odt_BitMap *(*screen_bitmap)(const struct odt_cenv *env, void *screen);
    /* ObtainBestPen (32-bit guns, OBP_Precision, OBP_FailIfBad FALSE): the pen, -1 when none */
    odt_s32 (*obtain_pen)(const struct odt_cenv *env, void *cm, odt_u32 r, odt_u32 g, odt_u32 b, odt_s32 precision);
    void (*release_pen)(const struct odt_cenv *env, void *cm, odt_u32 pen);
    void (*get_rgb32)(const struct odt_cenv *env, void *cm, odt_u32 pen, odt_u32 rgb[3]);
    /* drawing into a RastPort: BltBitMapRastPort, or BltMaskBitMapRastPort with a mask */
    void (*blit)(const struct odt_cenv *env, void *rp, struct odt_BitMap *bm, const odt_u8 *mask, odt_s32 sx,
                 odt_s32 sy, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h);
    void (*fill)(const struct odt_cenv *env, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);
    void (*line)(const struct odt_cenv *env, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);

    /* audio (sound.datatype): the player takes a whole plan and plays it
     * through audio.device on its own process, then signals the plan's
     * task; *player is the shell's own record of it, NULL when none.
     * stop aborts it, waits until it is done with the plan and clears
     * *player. 1 started, 0 not. */
    int (*audio_play)(const struct odt_cenv *env, void **player, const struct dtc_audio_plan *plan);
    void (*audio_stop)(const struct odt_cenv *env, void **player);
    odt_u32 audio_clock;                /* 3546895 PAL, 3579545 NTSC */
};
#define DTC_EXT(env) ((const struct dtc_ext *)(env)->class_ext)

/* ---- shared helpers (header only, no data) -------------------------------- */

static inline odt_u32 dtc_be32(const odt_u8 *p)
{ return ((odt_u32)p[0] << 24) | ((odt_u32)p[1] << 16) | ((odt_u32)p[2] << 8) | p[3]; }
static inline odt_u16 dtc_be16(const odt_u8 *p) { return (odt_u16)((p[0] << 8) | p[1]); }
static inline void dtc_put32(odt_u8 *p, odt_u32 v)
{ p[0] = (odt_u8)(v >> 24); p[1] = (odt_u8)(v >> 16); p[2] = (odt_u8)(v >> 8); p[3] = (odt_u8)v; }
static inline void dtc_put16(odt_u8 *p, odt_u32 v) { p[0] = (odt_u8)(v >> 8); p[1] = (odt_u8)v; }

/* utility.library's NextTagItem and GetTagData */
static inline struct odt_TagItem *dtc_next_tag(struct odt_TagItem **tp)
{
    struct odt_TagItem *t = *tp;
    for (;;) {
        if (!t)
            return 0;
        switch (t->ti_Tag) {
        case ODT_TAG_DONE: *tp = 0; return 0;
        case ODT_TAG_MORE: t = (struct odt_TagItem *)t->ti_Data; continue;
        case ODT_TAG_IGNORE: t++; continue;
        case ODT_TAG_SKIP: t += 1 + t->ti_Data; continue;
        default: *tp = t + 1; return t;
        }
    }
}

/* One attribute of obj through its class, as GetDTAttrs: 1 when answered */
static inline int dtc_get(const struct odt_cenv *env, void *obj, odt_u32 attr, odt_u32 *out)
{
    struct odt_opGet g;
    g.MethodID = ODT_OM_GET;
    g.opg_AttrID = attr;
    g.opg_Storage = out;
    return env->do_method(env, obj, &g) != 0;
}
/* A superclass's attribute, from inside a class */
static inline int dtc_get_super(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, odt_u32 attr,
                                odt_u32 *out)
{
    struct odt_opGet g;
    g.MethodID = ODT_OM_GET;
    g.opg_AttrID = attr;
    g.opg_Storage = out;
    return env->do_super(env, cl, obj, &g) != 0;
}
/* Attributes set on the superclass (SetSuperAttrs) */
static inline odt_u32 dtc_set_super(const struct odt_cenv *env, struct odt_IClass *cl, void *obj,
                                    struct odt_TagItem *tags)
{
    struct odt_opSet s;
    s.MethodID = ODT_OM_SET;
    s.ops_AttrList = tags;
    s.ops_GInfo = 0;
    return env->do_super(env, cl, obj, &s);
}

#ifdef __cplusplus
}
#endif
#endif
