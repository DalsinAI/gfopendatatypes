/* GFOpenDatatypes animation.datatype core, in plain C with no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the animation_dtc autodoc, NDK 3.2's
 * datatypes/animationclass.h, pictureclass.h and soundclass.h, and OS
 * 3.2's answers measured by tests/AnimationClassABITest; never from
 * Kickstart, Hyperion, AROS or any third-party datatype's code.
 *
 * The class holds what a subclass (anim.datatype and the rest) tells it
 * (the ADTA_ attributes), asks the subclass for frames with ADTM_LOADFRAME
 * a few ahead of the one shown (ADTA_PreloadFrameCount) and gives them back
 * with ADTM_UNLOADFRAME, plays them at ADTA_FramesPerSecond on a player
 * process (ADTM_START, ADTM_PAUSE, ADTM_STOP, ADTM_LOCATE and DTM_TRIGGER's
 * STM_), remaps them to the screen's pens, plays the sound along (a
 * sample a frame, or ADTA_Sample from where the frame is) and draws a
 * control panel under the picture when DTA_ControlPanel is on. The system
 * comes in through struct odt_cenv (dtclass_env.h) and the animation
 * family's struct adt_ext (its class_ext).
 */
#ifndef OPENDATATYPES_ANIMATION_CORE_H
#define OPENDATATYPES_ANIMATION_CORE_H

#include "../text/dtc_util.h"

/* ---- datatypes/pictureclass.h, soundclass.h, animationclass.h ----------------- */

#define ADT_ANIMATIONDTCLASS "animation.datatype"

#define ADT_PDTA_Dummy          (ODT_DTA_Dummy + 199)   /* PDTA_ModeID is DTA_Dummy + 200 */
#define ADT_PDTA_ModeID         (ADT_PDTA_Dummy + 1)
#define ADT_PDTA_BitMap         (ADT_PDTA_Dummy + 3)
#define ADT_PDTA_ColorRegisters (ADT_PDTA_Dummy + 4)
#define ADT_PDTA_CRegs          (ADT_PDTA_Dummy + 5)
#define ADT_PDTA_GRegs          (ADT_PDTA_Dummy + 6)
#define ADT_PDTA_ColorTable     (ADT_PDTA_Dummy + 7)
#define ADT_PDTA_ColorTable2    (ADT_PDTA_Dummy + 8)
#define ADT_PDTA_Allocated      (ADT_PDTA_Dummy + 9)
#define ADT_PDTA_NumColors      (ADT_PDTA_Dummy + 10)
#define ADT_PDTA_NumAlloc       (ADT_PDTA_Dummy + 11)
#define ADT_PDTA_Remap          (ADT_PDTA_Dummy + 12)
#define ADT_PDTA_Screen         (ADT_PDTA_Dummy + 13)
#define ADT_PDTA_NumSparse      (ADT_PDTA_Dummy + 18)
#define ADT_PDTA_SparseTable    (ADT_PDTA_Dummy + 19)

#define ADT_SDTA_Dummy          (ODT_DTA_Dummy + 500)
#define ADT_SDTA_Sample         (ADT_SDTA_Dummy + 2)
#define ADT_SDTA_SampleLength   (ADT_SDTA_Dummy + 3)
#define ADT_SDTA_Period         (ADT_SDTA_Dummy + 4)
#define ADT_SDTA_Volume         (ADT_SDTA_Dummy + 5)
#define ADT_SDTA_Cycles         (ADT_SDTA_Dummy + 6)
#define ADT_SDTA_SamplesPerSec  (ADT_SDTA_Dummy + 11)
#define ADT_SDTA_LeftSample     (ADT_SDTA_Dummy + 13)
#define ADT_SDTA_RightSample    (ADT_SDTA_Dummy + 14)

#define ADT_ADTA_Dummy           (ODT_DTA_Dummy + 600)
#define ADT_ADTA_ModeID          ADT_PDTA_ModeID
#define ADT_ADTA_KeyFrame        ADT_PDTA_BitMap
#define ADT_ADTA_ColorRegisters  ADT_PDTA_ColorRegisters
#define ADT_ADTA_CRegs           ADT_PDTA_CRegs
#define ADT_ADTA_GRegs           ADT_PDTA_GRegs
#define ADT_ADTA_ColorTable      ADT_PDTA_ColorTable
#define ADT_ADTA_ColorTable2     ADT_PDTA_ColorTable2
#define ADT_ADTA_Allocated       ADT_PDTA_Allocated
#define ADT_ADTA_NumColors       ADT_PDTA_NumColors
#define ADT_ADTA_NumAlloc        ADT_PDTA_NumAlloc
#define ADT_ADTA_Remap           ADT_PDTA_Remap
#define ADT_ADTA_Screen          ADT_PDTA_Screen
#define ADT_ADTA_NumSparse       ADT_PDTA_NumSparse
#define ADT_ADTA_SparseTable     ADT_PDTA_SparseTable
#define ADT_ADTA_Width           (ADT_ADTA_Dummy + 1)
#define ADT_ADTA_Height          (ADT_ADTA_Dummy + 2)
#define ADT_ADTA_Depth           (ADT_ADTA_Dummy + 3)
#define ADT_ADTA_Frames          (ADT_ADTA_Dummy + 4)
#define ADT_ADTA_Frame           (ADT_ADTA_Dummy + 5)
#define ADT_ADTA_FramesPerSecond (ADT_ADTA_Dummy + 6)
#define ADT_ADTA_FrameIncrement  (ADT_ADTA_Dummy + 7)
#define ADT_ADTA_PreloadFrameCount (ADT_ADTA_Dummy + 8)
#define ADT_ADTA_Sample          ADT_SDTA_Sample
#define ADT_ADTA_SampleLength    ADT_SDTA_SampleLength
#define ADT_ADTA_Period          ADT_SDTA_Period
#define ADT_ADTA_Volume          ADT_SDTA_Volume
#define ADT_ADTA_Cycles          ADT_SDTA_Cycles
#define ADT_ADTA_LeftSample      ADT_SDTA_LeftSample
#define ADT_ADTA_RightSample     ADT_SDTA_RightSample
#define ADT_ADTA_SamplesPerSec   ADT_SDTA_SamplesPerSec

#define ADT_ADTM_Dummy                0x700UL
#define ADT_ADTM_LOADFRAME            0x701UL
#define ADT_ADTM_UNLOADFRAME          0x702UL
#define ADT_ADTM_START                0x703UL
#define ADT_ADTM_PAUSE                0x704UL
#define ADT_ADTM_STOP                 0x705UL
#define ADT_ADTM_LOCATE               0x706UL
#define ADT_ADTM_LOADNEWFORMATFRAME   0x707UL
#define ADT_ADTM_UNLOADNEWFORMATFRAME 0x708UL

/* struct adtFrame and the V44 adtNewFormatFrame (the first ten fields alike) */
struct adt_Frame {
    odt_u32 MethodID;
    odt_u32 alf_TimeStamp;
    odt_u32 alf_Frame;
    odt_u32 alf_Duration;
    void *alf_BitMap;                   /* struct BitMap * */
    void *alf_CMap;                     /* struct ColorMap * */
    odt_s8 *alf_Sample;
    odt_u32 alf_SampleLength;
    odt_u32 alf_Period;
    void *alf_UserData;
    odt_u32 alf_Size;                   /* V44 from here */
    odt_s8 *alf_LeftSample;
    odt_s8 *alf_RightSample;
    odt_u32 alf_SamplesPerSec;
};
ODT_STATIC_ASSERT(__builtin_offsetof(struct adt_Frame, alf_Size) == 40, "adtFrame");
ODT_STATIC_ASSERT(sizeof(struct adt_Frame) == 56, "adtNewFormatFrame");
struct adt_Start { odt_u32 MethodID; odt_u32 asa_Frame; };

/* graphics/gfx.h */
struct adt_BitMap {
    odt_u16 BytesPerRow, Rows;
    odt_u8 Flags, Depth;
    odt_u16 pad;
    odt_u8 *Planes[8];
};
ODT_STATIC_ASSERT(sizeof(struct adt_BitMap) == 40, "BitMap");

struct adt_ColorRegister { odt_u8 red, green, blue; };

/* graphics/modeid.h's HAM_KEY bit */
#define ADT_HAM 0x0800UL

/* ---- the animation family's services (odt_cenv.class_ext) --------------------- */

#define ADT_GLYPH_PLAY  1
#define ADT_GLYPH_PAUSE 2
#define ADT_GLYPH_STOP  3

struct adt_ext {
    /* AllocBitMap(w, h, depth, BMF_CLEAR, friend) and FreeBitMap */
    struct adt_BitMap *(*alloc_bitmap)(const struct odt_cenv *env, odt_u32 w, odt_u32 h, odt_u32 depth, void *friend_bm);
    void (*free_bitmap)(const struct odt_cenv *env, struct adt_BitMap *bm);
    /* ObtainBestPen on the screen's ColorMap (32-bit guns, PRECISION_IMAGE)
     * and ReleasePen; the screen's depth */
    odt_s32 (*obtain_pen)(const struct odt_cenv *env, void *screen, odt_u32 r, odt_u32 g, odt_u32 b);
    void (*release_pen)(const struct odt_cenv *env, void *screen, odt_u32 pen);
    odt_u32 (*screen_depth)(const struct odt_cenv *env, void *screen);
    /* BltBitMapRastPort, minterm 0xc0 */
    void (*blit)(const struct odt_cenv *env, struct adt_BitMap *bm, odt_s32 sx, odt_s32 sy, void *rp, odt_s32 dx, odt_s32 dy,
                 odt_s32 w, odt_s32 h);
    void (*fill)(const struct odt_cenv *env, void *rp, odt_s32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);
    /* a bevelled box (raised, or recessed when pressed) and a glyph in it */
    void (*bevel)(const struct odt_cenv *env, void *rp, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h, int recessed);
    void (*glyph)(const struct odt_cenv *env, void *rp, odt_u32 kind, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h,
                  odt_s32 pen);
    odt_s32 (*pen)(const struct odt_cenv *env, struct odt_GadgetInfo *gi, odt_u32 which);   /* DrawInfo pens */
    odt_u32 (*font_ysize)(const struct odt_cenv *env, struct odt_GadgetInfo *gi);
    /* audio.device: play 8-bit signed data (from the start of data) at
     * period and volume (0-64), cycles times (0: until stopped); stop */
    int (*sound_play)(const struct odt_cenv *env, const odt_s8 *data, odt_u32 len, odt_u32 period, odt_u32 volume,
                      odt_u32 cycles);
    void (*sound_stop)(const struct odt_cenv *env);
    odt_u32 (*clock)(const struct odt_cenv *env);  /* the colour clock: 3579545 NTSC, 3546895 PAL */
    /* a process ticking fps times a second, calling adt_tick(env, cl, obj)
     * each time until stopped */
    void *(*player_start)(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, odt_u32 fps);
    void (*player_stop)(const struct odt_cenv *env, void *player);
    /* WaitBlit, before the CPU writes into a bitmap a blit may still read */
    void (*wait_blit)(const struct odt_cenv *env);
};

/* ---- playing: frames against time -------------------------------------------- */

#define ADT_STOPPED 0
#define ADT_PLAYING 1
#define ADT_PAUSED  2

struct adt_play {
    odt_u32 frames;
    odt_u32 frame;                      /* the one shown */
    odt_u32 state;
    odt_u32 left;                       /* ticks the frame has still to stay */
    odt_u32 increment;                  /* ADTA_FrameIncrement */
    odt_u32 ticks;                      /* since frame 0, for the sound */
    odt_u8 repeat;                      /* from the last frame back to the first */
    odt_u8 pad[3];
};

void adt_play_init(struct adt_play *p, odt_u32 frames);
/* ADTM_START at frame: duration is that frame's */
void adt_play_start(struct adt_play *p, odt_u32 frame, odt_u32 duration, odt_u32 ticks);
/* One tick of the clock: 1 when another frame is to be shown (p->frame),
 * whose duration then comes in with adt_play_shown */
int adt_play_tick(struct adt_play *p);
void adt_play_shown(struct adt_play *p, odt_u32 duration);

/* Where in a sample frame time ticks starts, in bytes: ticks / fps
 * seconds at clock / period samples a second (or sps when given) */
odt_u32 adt_sound_offset(odt_u32 ticks, odt_u32 fps, odt_u32 period, odt_u32 sps, odt_u32 clock, odt_u32 len);

/* ---- remapping ---------------------------------------------------------------- */

/* Each pixel of src (w x h) through table into dst (planes as BitMaps
 * have them), eight pixels at a time; pixels past src's depth read as 0,
 * dst planes past 8 and dst's bits past w are left alone. */
void adt_remap(const struct adt_BitMap *src, odt_u32 w, odt_u32 h, const odt_u8 *table, struct adt_BitMap *dst);

/* ---- the control panel -------------------------------------------------------- */

struct adt_panel {
    odt_s16 x, y, w, h;                 /* the panel, under the picture */
    odt_s16 play_x, stop_x, bw;         /* the buttons, each bw wide */
    odt_s16 sx, sw, kw;                 /* the slider's track and knob */
};
#define ADT_HIT_NONE   0
#define ADT_HIT_PLAY   1
#define ADT_HIT_STOP   2
#define ADT_HIT_SLIDER 3

void adt_panel_layout(struct adt_panel *p, odt_s16 left, odt_s16 top, odt_s16 width, odt_s16 height, odt_u32 ysize);
int adt_panel_hit(const struct adt_panel *p, odt_s32 x, odt_s32 y, odt_u32 frames, odt_u32 *frame);
void adt_panel_draw(const struct odt_cenv *env, const struct adt_ext *x, void *rp, const struct adt_panel *p,
                    odt_u32 state, odt_u32 frame, odt_u32 frames, odt_s32 back, odt_s32 fill, odt_s32 text);
odt_s32 adt_knob_x(const struct adt_panel *p, odt_u32 frame, odt_u32 frames);

/* ---- the class ------------------------------------------------------------------ */

#define ADT_MAXPRELOAD 64

struct adt_slot {
    struct adt_Frame msg;               /* as the subclass filled it */
    struct adt_BitMap *remapped;        /* ours, on the screen's pens; kept for the slot's next frame */
    odt_u8 used;
    odt_u8 pad;
    odt_u16 remapped_w;                 /* the width remapped was made for */
};

struct adt_inst {
    struct odt_IClass *cl;              /* for the player process */
    odt_u32 width, height, depth, frames, fps, increment, preload;
    odt_u32 mode_id;
    struct adt_BitMap *key_frame;       /* ADTA_KeyFrame, the subclass's */
    odt_u32 num_colors;
    struct adt_ColorRegister *colregs;  /* ADTA_ColorRegisters: ours, the subclass fills them */
    odt_u32 *cregs, *gregs;             /* 3 a colour */
    odt_u8 *color_table, *color_table2; /* 256 each */
    odt_u8 *allocated;                  /* 256 */
    odt_u32 num_alloc;
    odt_u8 remap, panel, repeat, pad0;
    void *screen;                       /* ADTA_Screen */
    odt_u32 num_sparse;
    odt_u8 *sparse;
    odt_s8 *sample, *left, *right;
    odt_u32 sample_len, period, volume, cycles, sps;
    struct adt_play play;
    struct adt_slot slots[ADT_MAXPRELOAD];
    odt_u32 nslots;
    odt_u8 newformat;                   /* ADTM_LOADNEWFORMATFRAME answered */
    odt_u8 pens_got;                    /* the pens are obtained */
    odt_u8 sound_on;                    /* ADTA_Sample is playing */
    odt_u8 remap_stale;                 /* Remap, Screen or NumColors changed: the cache's remaps go */
    void *pens_screen;                  /* the screen they are from */
    void *player;
    struct odt_GadgetInfo gi;           /* a copy, for drawing from the player */
    odt_u8 have_gi;
    odt_u8 pad2[3];
    struct adt_panel pnl;
};

/* The frames wanted from the shown one on (preload of them): unloads the
 * others, loads the missing. Returns the shown frame's slot, or NULL. */
struct adt_slot *adt_cache(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 frame);
void adt_cache_flush(const struct odt_cenv *env, struct adt_inst *in, void *obj);

/* The player process's tick */
void adt_tick(const struct odt_cenv *env, struct odt_IClass *cl, void *obj);

odt_u32 adt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
odt_u32 adt_inst_size(void);

extern const odt_u32 adt_methods[];

#endif
