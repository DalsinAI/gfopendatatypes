/* GFOpenDatatypes animation.datatype core.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 * Clean-room; see animation_core.h for what this is written from.
 */
#include "animation_core.h"

/* DTA_TriggerMethods: what DTM_TRIGGER does (labels: ours; ABI test) */
static const struct odt_DTMethod adt_triggers[] = {
    { "Play", "PLAY", ODT_STM_PLAY },
    { "Pause", "PAUSE", ODT_STM_PAUSE },
    { "Stop", "STOP", ODT_STM_STOP },
    { "Rewind", "REWIND", ODT_STM_REWIND },
    { "Fast Forward", "FASTFORWARD", ODT_STM_FASTFORWARD },
    { 0, 0, 0 }
};

const odt_u32 adt_methods[] = {
    ODT_OM_NEW, ODT_OM_DISPOSE, ODT_OM_SET, ODT_OM_GET, ODT_OM_UPDATE,
    ODT_GM_LAYOUT, ODT_GM_RENDER, ODT_GM_HITTEST, ODT_GM_GOACTIVE, ODT_GM_HANDLEINPUT, ODT_GM_GOINACTIVE,
    ODT_DTM_PROCLAYOUT, ODT_DTM_ASYNCLAYOUT, ODT_DTM_FRAMEBOX, ODT_DTM_TRIGGER, ODT_DTM_REMOVEDTOBJECT,
    ODT_DTM_OBTAINDRAWINFO, ODT_DTM_DRAW, ODT_DTM_RELEASEDRAWINFO,
    ADT_ADTM_LOADFRAME, ADT_ADTM_UNLOADFRAME, ADT_ADTM_START, ADT_ADTM_PAUSE, ADT_ADTM_STOP, ADT_ADTM_LOCATE,
    ADT_ADTM_LOADNEWFORMATFRAME, ADT_ADTM_UNLOADNEWFORMATFRAME, ODT_DTM_WRITE, ~0UL
};

/* Defaults (ABI test, each) */
#define DEF_FPS     10
#define DEF_PRELOAD 4
#define DEF_VOLUME  64

/* ---- playing ------------------------------------------------------------------- */

void adt_play_init(struct adt_play *p, odt_u32 frames)
{
    p->frames = frames;
    p->frame = 0;
    p->state = ADT_STOPPED;
    p->left = 0;
    p->increment = 1;
    p->ticks = 0;
    p->repeat = 1;
}

void adt_play_start(struct adt_play *p, odt_u32 frame, odt_u32 duration, odt_u32 ticks)
{
    if (!p->frames) return;
    if (frame >= p->frames) frame = p->frames - 1;
    p->frame = frame;
    p->state = ADT_PLAYING;
    p->left = duration ? duration : 1;
    p->ticks = ticks;
}

int adt_play_tick(struct adt_play *p)
{
    odt_u32 next;
    if (p->state != ADT_PLAYING || !p->frames) return 0;
    p->ticks++;
    if (p->left > 1) {
        p->left--;
        return 0;
    }
    next = p->frame + (p->increment ? p->increment : 1);
    if (next >= p->frames) {
        if (!p->repeat) {
            /* the last frame stays (ABI test) */
            p->state = ADT_STOPPED;
            p->left = 0;
            return 0;
        }
        next %= p->frames;
        p->ticks = next;                /* time starts again (durations taken as 1) */
    }
    p->frame = next;
    p->left = 1;
    return 1;
}

void adt_play_shown(struct adt_play *p, odt_u32 duration) { p->left = duration ? duration : 1; }

odt_u32 adt_sound_offset(odt_u32 ticks, odt_u32 fps, odt_u32 period, odt_u32 sps, odt_u32 clock, odt_u32 len)
{
    odt_u32 rate = sps ? sps : (period ? clock / period : 0), off;
    if (!fps || !rate) return 0;
    off = (ticks / fps) * rate + ((ticks % fps) * rate) / fps;
    return off > len ? len : off;
}

/* ---- remapping ----------------------------------------------------------------- */

/* An 8 x 8 matrix of bits turned over. hi holds rows 0 to 3 (row 0 in its
 * top byte), lo rows 4 to 7; a row's top bit is column 0. Afterwards row j
 * holds what column j held, row 0's bit at the top. Eight plane bytes in
 * (plane 7 first) give eight pixels out, and eight pixels give the planes. */
static inline void adt_turn8(odt_u32 *hi, odt_u32 *lo)
{
    odt_u32 x = *hi, y = *lo, t;
    t = (x ^ (x >> 7)) & 0x00aa00aaUL;
    x ^= t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00aa00aaUL;
    y ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000ccccUL;
    x ^= t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000ccccUL;
    y ^= t ^ (t << 14);
    t = (x & 0xf0f0f0f0UL) | ((y >> 4) & 0x0f0f0f0fUL);
    *lo = ((x << 4) & 0xf0f0f0f0UL) | (y & 0x0f0f0f0fUL);
    *hi = t;
}

/* Four pixels (a byte each) through the pen table */
static inline odt_u32 adt_pens4(const odt_u8 *table, odt_u32 v)
{
    return (odt_u32)table[v >> 24] << 24 | (odt_u32)table[(v >> 16) & 255] << 16 | (odt_u32)table[(v >> 8) & 255] << 8 |
           table[v & 255];
}

void adt_remap(const struct adt_BitMap *src, odt_u32 w, odt_u32 h, const odt_u8 *table, struct adt_BitMap *dst)
{
    odt_u32 x, y, p, sd = src->Depth > 8 ? 8 : src->Depth, dd = dst->Depth > 8 ? 8 : dst->Depth, nd = 0;
    odt_u32 mask[2] = { 0, 0 }, any = 8, at[8];
    if (w > (odt_u32)src->BytesPerRow * 8) w = (odt_u32)src->BytesPerRow * 8;
    if (w > (odt_u32)dst->BytesPerRow * 8) w = (odt_u32)dst->BytesPerRow * 8;
    if (h > src->Rows) h = src->Rows;
    if (h > dst->Rows) h = dst->Rows;
    /* a missing source plane reads another plane's row and is masked off
     * (none at all reads as 0); missing destination planes are skipped */
    for (p = 0; p < sd; p++)
        if (src->Planes[p]) {
            if (any == 8) any = p;
            mask[p >> 2] |= 0xffUL << (8 * (p & 3));
        }
    if (any == 8) sd = 0;
    for (p = 0; p < dd; p++)
        if (dst->Planes[p]) at[nd++] = p;
    for (y = 0; y < h; y++) {
        const odt_u8 *s[8];
        odt_u8 *d[8];
        for (p = 0; p < sd; p++)
            s[p] = (src->Planes[p] ? src->Planes[p] : src->Planes[any]) + y * src->BytesPerRow;
        for (p = 0; p < nd; p++)
            d[p] = dst->Planes[at[p]] + y * dst->BytesPerRow;
        /* a byte of each plane at a time: its 8 pixels out of the planes,
         * through the table and back into them */
        for (x = 0; x < w; x += 8) {
            odt_u32 byte = x >> 3, v[2] = { 0, 0 };
            odt_u8 keep = (odt_u8)(w - x >= 8 ? 0 : 0xff >> (w - x)); /* bits past w stay as they are */
            switch (sd) {
            case 8: v[1] |= (odt_u32)s[7][byte] << 24; /* fall through */
            case 7: v[1] |= (odt_u32)s[6][byte] << 16; /* fall through */
            case 6: v[1] |= (odt_u32)s[5][byte] << 8;  /* fall through */
            case 5: v[1] |= s[4][byte];                /* fall through */
            case 4: v[0] |= (odt_u32)s[3][byte] << 24; /* fall through */
            case 3: v[0] |= (odt_u32)s[2][byte] << 16; /* fall through */
            case 2: v[0] |= (odt_u32)s[1][byte] << 8;  /* fall through */
            case 1: v[0] |= s[0][byte];                /* fall through */
            default: break;
            }
            v[0] &= mask[0];
            v[1] &= mask[1];
            adt_turn8(&v[1], &v[0]);    /* v[1]: pixels 0-3, v[0]: pixels 4-7 */
            if (table) {
                v[0] = adt_pens4(table, v[0]);
                v[1] = adt_pens4(table, v[1]);
            }
            adt_turn8(&v[1], &v[0]);    /* v[0]: planes 3-0, v[1]: planes 7-4 */
            for (p = 0; p < nd; p++) {
                odt_u8 b = (odt_u8)(v[at[p] >> 2] >> (8 * (at[p] & 3)));
                d[p][byte] = (odt_u8)((d[p][byte] & keep) | (b & ~keep));
            }
        }
    }
}

/* ---- the control panel ------------------------------------------------------------- */

void adt_panel_layout(struct adt_panel *p, odt_s16 left, odt_s16 top, odt_s16 width, odt_s16 height, odt_u32 ysize)
{
    odt_s32 h = (odt_s32)ysize + 6, sw;
    if (h < 12) h = 12;
    if (h > height) h = height > 0 ? height : 0;
    p->x = left;
    p->w = width;
    p->h = (odt_s16)h;
    p->y = (odt_s16)(top + height - h);
    p->bw = (odt_s16)(h * 3 / 2);
    p->play_x = (odt_s16)(left + 1);
    p->stop_x = (odt_s16)(p->play_x + p->bw + 1);
    p->sx = (odt_s16)(p->stop_x + p->bw + 2);
    sw = left + width - 1 - p->sx;
    p->sw = (odt_s16)(sw > 0 ? sw : 0);
    p->kw = (odt_s16)(h > p->sw ? p->sw : h);
}

int adt_panel_hit(const struct adt_panel *p, odt_s32 x, odt_s32 y, odt_u32 frames, odt_u32 *frame)
{
    if (y < p->y || y >= p->y + p->h || x < p->x || x >= p->x + p->w) return ADT_HIT_NONE;
    if (x >= p->play_x && x < p->play_x + p->bw) return ADT_HIT_PLAY;
    if (x >= p->stop_x && x < p->stop_x + p->bw) return ADT_HIT_STOP;
    if (p->sw > 0 && frames && x >= p->sx && x < p->sx + p->sw) {
        odt_u32 f = (odt_u32)(x - p->sx) * frames / (odt_u32)p->sw;
        *frame = f >= frames ? frames - 1 : f;
        return ADT_HIT_SLIDER;
    }
    return ADT_HIT_NONE;
}

odt_s32 adt_knob_x(const struct adt_panel *p, odt_u32 frame, odt_u32 frames)
{
    if (frames < 2 || p->sw <= p->kw) return p->sx;
    if (frame >= frames) frame = frames - 1;
    return p->sx + (odt_s32)(frame * (odt_u32)(p->sw - p->kw) / (frames - 1));
}

void adt_panel_draw(const struct odt_cenv *env, const struct adt_ext *x, void *rp, const struct adt_panel *p,
                    odt_u32 state, odt_u32 frame, odt_u32 frames, odt_s32 back, odt_s32 fill, odt_s32 text)
{
    odt_s32 in = p->h > 6 ? 3 : 1;
    if (p->h <= 2 || p->w <= 0) return;
    x->fill(env, rp, back, p->x, p->y, p->x + p->w - 1, p->y + p->h - 1);
    x->bevel(env, rp, p->play_x, p->y + 1, p->bw, p->h - 2, state == ADT_PLAYING);
    x->glyph(env, rp, state == ADT_PLAYING ? ADT_GLYPH_PAUSE : ADT_GLYPH_PLAY, p->play_x + in, p->y + in,
             p->bw - 2 * in, p->h - 2 * in, text);
    x->bevel(env, rp, p->stop_x, p->y + 1, p->bw, p->h - 2, 0);
    x->glyph(env, rp, ADT_GLYPH_STOP, p->stop_x + in, p->y + in, p->bw - 2 * in, p->h - 2 * in, text);
    if (p->sw > 0) {
        odt_s32 kx = adt_knob_x(p, frame, frames);
        x->bevel(env, rp, p->sx, p->y + 1, p->sw, p->h - 2, 1);
        if (p->kw > 2 && p->h > 4) {
            x->fill(env, rp, fill, kx + 1, p->y + 3, kx + p->kw - 2, p->y + p->h - 4);
            x->bevel(env, rp, kx, p->y + 2, p->kw, p->h - 4, 0);
        }
    }
}

/* ---- frames: loading ahead, giving back ---------------------------------------- */

#define EXT(env) ((const struct adt_ext *)(env)->class_ext)
#define NF_UNKNOWN 0
#define NF_NEW     1
#define NF_OLD     2

/* The frame goes back to the subclass; the slot keeps its remapped bitmap
 * for the next frame loaded into it (remap_slot) */
static void unload(const struct odt_cenv *env, struct adt_inst *in, void *obj, struct adt_slot *s)
{
    s->msg.MethodID = in->newformat == NF_NEW ? ADT_ADTM_UNLOADNEWFORMATFRAME : ADT_ADTM_UNLOADFRAME;
    env->do_method(env, obj, &s->msg);
    s->used = 0;
}

static void drop_remapped(const struct odt_cenv *env, struct adt_slot *s)
{
    if (s->remapped && EXT(env)->free_bitmap) EXT(env)->free_bitmap(env, s->remapped);
    s->remapped = 0;
}

static void obtain_pens(const struct odt_cenv *env, struct adt_inst *in)
{
    const struct adt_ext *x = EXT(env);
    odt_u32 i;
    if (in->pens_got || !in->screen || !x->obtain_pen || !in->color_table) return;
    in->num_alloc = 0;
    for (i = 0; i < in->num_colors && i < 256; i++) {
        odt_s32 pen = x->obtain_pen(env, in->screen, in->cregs[3 * i], in->cregs[3 * i + 1], in->cregs[3 * i + 2]);
        /* a colour no pen could take shows as pen 0, which is not ours to give back */
        in->color_table[i] = in->color_table2[i] = (odt_u8)(pen < 0 ? 0 : pen);
        if (pen >= 0)
            in->allocated[in->num_alloc++] = (odt_u8)pen;
    }
    in->pens_got = 1;
    in->pens_screen = in->screen;
}

static void release_pens(const struct odt_cenv *env, struct adt_inst *in)
{
    odt_u32 i;
    if (!in->pens_got) return;
    for (i = 0; i < in->num_alloc; i++)
        if (EXT(env)->release_pen) EXT(env)->release_pen(env, in->pens_screen, in->allocated[i]);
    in->num_alloc = 0;
    in->pens_got = 0;
}

static int can_remap(const struct adt_inst *in)
{
    /* HAM can't go through a pen table: shown as it is (ABI test) */
    return in->remap && in->screen && in->color_table && in->num_colors && !(in->mode_id & ADT_HAM) &&
           in->depth <= 8;
}

static void remap_slot(const struct odt_cenv *env, struct adt_inst *in, struct adt_slot *s)
{
    const struct adt_ext *x = EXT(env);
    const struct adt_BitMap *f = s->msg.alf_BitMap;
    struct adt_BitMap *bm = s->remapped;
    odt_u32 d;
    if (!can_remap(in) || !f || !x->alloc_bitmap) {
        drop_remapped(env, s);
        return;
    }
    obtain_pens(env, in);
    d = x->screen_depth ? x->screen_depth(env, in->screen) : 8;
    if (d > 8) d = 8;                   /* deeper screens: pens up to 255 */
    /* the slot's bitmap from an earlier frame is written over when a new
     * one would be the same (the size, the depth, every pixel remapped) */
    if (bm && (s->remapped_w != in->width || bm->Rows != in->height || bm->Depth != d || f->Rows < in->height ||
               (odt_u32)f->BytesPerRow * 8 < in->width)) {
        drop_remapped(env, s);
        bm = 0;
    }
    if (bm) {
        if (x->wait_blit) x->wait_blit(env); /* a blit of the earlier frame may still read it */
    } else if (!(bm = x->alloc_bitmap(env, in->width, in->height, d, 0)))
        return;
    s->remapped = bm;
    s->remapped_w = (odt_u16)in->width;
    adt_remap(f, in->width, in->height, in->color_table, bm);
}

static int load(const struct odt_cenv *env, struct adt_inst *in, void *obj, struct adt_slot *s, odt_u32 f)
{
    odt_u8 *z = (odt_u8 *)&s->msg;
    odt_u32 i, r = 0;
    for (i = 0; i < sizeof s->msg; i++) z[i] = 0;
    s->msg.alf_TimeStamp = f;           /* timestamps count frames (ABI test) */
    s->msg.alf_Frame = f;
    s->msg.alf_Size = sizeof s->msg;
    if (in->newformat != NF_OLD) {
        s->msg.MethodID = ADT_ADTM_LOADNEWFORMATFRAME;
        r = env->do_method(env, obj, &s->msg);
        if (r || s->msg.alf_BitMap) in->newformat = NF_NEW;
        else if (in->newformat == NF_UNKNOWN) in->newformat = NF_OLD;
    }
    if (in->newformat == NF_OLD) {
        for (i = 0; i < sizeof s->msg; i++) z[i] = 0;
        s->msg.alf_TimeStamp = f;
        s->msg.alf_Frame = f;
        s->msg.MethodID = ADT_ADTM_LOADFRAME;
        r = env->do_method(env, obj, &s->msg);
    }
    if (!r && !s->msg.alf_BitMap) return 0;
    s->used = 1;
    remap_slot(env, in, s);
    return 1;
}

static odt_u32 window(const struct adt_inst *in)
{
    odt_u32 n = in->preload ? in->preload : 1;
    if (n > ADT_MAXPRELOAD) n = ADT_MAXPRELOAD;
    if (n > in->frames) n = in->frames;
    return n;
}

/* Is f among the n frames from frame on? */
static int wanted(const struct adt_inst *in, odt_u32 frame, odt_u32 n, odt_u32 f)
{
    odt_u32 d;
    if (f >= frame) d = f - frame;
    else if (in->play.repeat) d = f + in->frames - frame;
    else return 0;
    return d < n;
}

struct adt_slot *adt_cache(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 frame)
{
    odt_u32 n = window(in), i, k;
    struct adt_slot *shown = 0;
    if (!in->frames || frame >= in->frames) return 0;
    for (k = 0; k < ADT_MAXPRELOAD; k++)
        if (in->slots[k].used && !wanted(in, frame, n, in->slots[k].msg.alf_Frame)) unload(env, in, obj, &in->slots[k]);
    for (i = 0; i < n; i++) {
        odt_u32 f = frame + i;
        struct adt_slot *s = 0, *fr = 0;
        if (f >= in->frames) {
            if (!in->play.repeat) break;
            f -= in->frames;
        }
        for (k = 0; k < ADT_MAXPRELOAD; k++) {
            if (in->slots[k].used && in->slots[k].msg.alf_Frame == f) { s = &in->slots[k]; break; }
            if (!in->slots[k].used && !fr) fr = &in->slots[k];
        }
        if (!s && fr && load(env, in, obj, fr, f)) s = fr;
        if (i == 0) shown = s;
    }
    return shown;
}

void adt_cache_flush(const struct odt_cenv *env, struct adt_inst *in, void *obj)
{
    odt_u32 k;
    for (k = 0; k < ADT_MAXPRELOAD; k++) {
        if (in->slots[k].used) unload(env, in, obj, &in->slots[k]);
        drop_remapped(env, &in->slots[k]);
    }
}

/* ---- the class ---------------------------------------------------------------- */

static void notify(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi, struct odt_TagItem *tags)
{
    struct odt_opUpdate u;
    u.MethodID = ODT_OM_NOTIFY;
    u.opu_AttrList = tags;
    u.opu_GInfo = gi;
    u.opu_Flags = 0;
    env->do_method(env, obj, &u);
}

static void tell_frame(const struct odt_cenv *env, struct adt_inst *in, void *obj, struct odt_GadgetInfo *gi)
{
    struct odt_TagItem t[2];
    t[0].ti_Tag = ADT_ADTA_Frame;
    t[0].ti_Data = in->play.frame;
    t[1].ti_Tag = ODT_TAG_DONE;
    t[1].ti_Data = 0;
    notify(env, obj, gi, t);
}

static void free_colors(const struct odt_cenv *env, struct adt_inst *in)
{
    release_pens(env, in);
    env->free(env, in->colregs);
    env->free(env, in->cregs);
    env->free(env, in->gregs);
    env->free(env, in->color_table);
    env->free(env, in->color_table2);
    env->free(env, in->allocated);
    in->colregs = 0;
    in->cregs = in->gregs = 0;
    in->color_table = in->color_table2 = in->allocated = 0;
    in->num_colors = 0;
}

/* ADTA_NumColors: the tables for that many, cleared, for the subclass to fill */
static int set_colors(const struct odt_cenv *env, struct adt_inst *in, odt_u32 n)
{
    free_colors(env, in);
    if (n > 256) n = 256;
    if (!n) return 1;
    in->colregs = env->alloc(env, n * sizeof(struct adt_ColorRegister));
    in->cregs = env->alloc(env, n * 3 * sizeof(odt_u32));
    in->gregs = env->alloc(env, n * 3 * sizeof(odt_u32));
    in->color_table = env->alloc(env, 256);
    in->color_table2 = env->alloc(env, 256);
    in->allocated = env->alloc(env, 256);
    if (!in->colregs || !in->cregs || !in->gregs || !in->color_table || !in->color_table2 || !in->allocated) {
        free_colors(env, in);
        return 0;
    }
    in->num_colors = n;
    return 1;
}

static void sound_from(const struct odt_cenv *env, struct adt_inst *in)
{
    const struct adt_ext *x = EXT(env);
    odt_u32 off, clock;
    if (!in->sample || !in->sample_len || !x->sound_play) return;
    clock = x->clock ? x->clock(env) : 3546895UL;
    off = adt_sound_offset(in->play.ticks, in->fps, in->period, in->sps, clock, in->sample_len);
    if (off >= in->sample_len) return;
    x->sound_play(env, in->sample + off, in->sample_len - off, in->period ? in->period : 394, in->volume, in->cycles);
    in->sound_on = 1;
}

static void sound_off(const struct odt_cenv *env, struct adt_inst *in)
{
    if (in->sound_on && EXT(env)->sound_stop) EXT(env)->sound_stop(env);
    in->sound_on = 0;
}

static void frame_sound(const struct odt_cenv *env, struct adt_inst *in, struct adt_slot *s)
{
    const struct adt_ext *x = EXT(env);
    if (!s || !s->msg.alf_Sample || !s->msg.alf_SampleLength || !x->sound_play) return;
    x->sound_play(env, s->msg.alf_Sample, s->msg.alf_SampleLength,
                  s->msg.alf_Period ? s->msg.alf_Period : (in->period ? in->period : 394), in->volume, 1);
    in->sound_on = 1;
}

static int set_attrs(const struct odt_cenv *env, struct adt_inst *in, const struct odt_TagItem *tags)
{
    const struct odt_TagItem *t, *list = tags;
    int changed = 0;
    while ((t = dtc_next_tag(&list)) != 0) {
        odt_u32 d = t->ti_Data;
        switch (t->ti_Tag) {
        case ADT_ADTA_Width: in->width = d; changed = 1; break;
        case ADT_ADTA_Height: in->height = d; changed = 1; break;
        case ADT_ADTA_Depth: in->depth = d; break;
        case ADT_ADTA_Frames: in->frames = d; in->play.frames = d; changed = 1; break;
        case ADT_ADTA_Frame: if (d < in->frames) in->play.frame = d; changed = 1; break;
        case ADT_ADTA_FramesPerSecond: in->fps = d ? d : 1; break;
        case ADT_ADTA_FrameIncrement: in->increment = d ? d : 1; in->play.increment = in->increment; break;
        case ADT_ADTA_PreloadFrameCount: in->preload = d ? d : 1; break;
        case ADT_ADTA_ModeID: in->mode_id = d; break;
        case ADT_ADTA_KeyFrame: in->key_frame = (struct adt_BitMap *)d; changed = 1; break;
        case ADT_ADTA_NumColors:
            /* the pens go back before their table does; the frames are remapped again */
            release_pens(env, in);
            set_colors(env, in, d);
            in->remap_stale = 1;
            break;
        case ADT_ADTA_Remap:
            if ((d != 0) != in->remap) in->remap_stale = 1;
            in->remap = d != 0;
            break;
        case ADT_ADTA_Screen:
            if ((void *)d != in->screen) in->remap_stale = 1;
            in->screen = (void *)d;
            break;
        case ADT_ADTA_NumSparse: in->num_sparse = d; break;
        case ADT_ADTA_SparseTable: in->sparse = (odt_u8 *)d; break;
        case ADT_ADTA_Sample: in->sample = (odt_s8 *)d; break;
        case ADT_ADTA_SampleLength: in->sample_len = d; break;
        case ADT_ADTA_Period: in->period = d; break;
        case ADT_ADTA_Volume: in->volume = d > 64 ? 64 : d; break;
        case ADT_ADTA_Cycles: in->cycles = d; break;
        case ADT_ADTA_LeftSample: in->left = (odt_s8 *)d; break;
        case ADT_ADTA_RightSample: in->right = (odt_s8 *)d; break;
        case ADT_ADTA_SamplesPerSec: in->sps = d; break;
        case ODT_DTA_ControlPanel: in->panel = d != 0; changed = 1; break;
        case ODT_DTA_Repeat: in->repeat = d != 0; in->play.repeat = in->repeat; break;
        default: break;
        }
    }
    return changed;
}

static int get_attr(struct adt_inst *in, odt_u32 tag, odt_u32 *v)
{
    switch (tag) {
    case ADT_ADTA_Width: *v = in->width; return 1;
    case ADT_ADTA_Height: *v = in->height; return 1;
    case ADT_ADTA_Depth: *v = in->depth; return 1;
    case ADT_ADTA_Frames: *v = in->frames; return 1;
    case ADT_ADTA_Frame: *v = in->play.frame; return 1;
    case ADT_ADTA_FramesPerSecond: *v = in->fps; return 1;
    case ADT_ADTA_FrameIncrement: *v = in->increment; return 1;
    case ADT_ADTA_PreloadFrameCount: *v = in->preload; return 1;
    case ADT_ADTA_ModeID: *v = in->mode_id; return 1;
    case ADT_ADTA_KeyFrame: *v = (odt_u32)in->key_frame; return 1;
    case ADT_ADTA_ColorRegisters: *v = (odt_u32)in->colregs; return 1;
    case ADT_ADTA_CRegs: *v = (odt_u32)in->cregs; return 1;
    case ADT_ADTA_GRegs: *v = (odt_u32)in->gregs; return 1;
    case ADT_ADTA_ColorTable: *v = (odt_u32)in->color_table; return 1;
    case ADT_ADTA_ColorTable2: *v = (odt_u32)in->color_table2; return 1;
    case ADT_ADTA_Allocated: *v = (odt_u32)in->allocated; return 1;
    case ADT_ADTA_NumColors: *v = in->num_colors; return 1;
    case ADT_ADTA_NumAlloc: *v = in->num_alloc; return 1;
    case ADT_ADTA_Remap: *v = in->remap; return 1;
    case ADT_ADTA_Screen: *v = (odt_u32)in->screen; return 1;
    case ADT_ADTA_NumSparse: *v = in->num_sparse; return 1;
    case ADT_ADTA_SparseTable: *v = (odt_u32)in->sparse; return 1;
    case ADT_ADTA_Sample: *v = (odt_u32)in->sample; return 1;
    case ADT_ADTA_SampleLength: *v = in->sample_len; return 1;
    case ADT_ADTA_Period: *v = in->period; return 1;
    case ADT_ADTA_Volume: *v = in->volume; return 1;
    case ADT_ADTA_Cycles: *v = in->cycles; return 1;
    case ADT_ADTA_LeftSample: *v = (odt_u32)in->left; return 1;
    case ADT_ADTA_RightSample: *v = (odt_u32)in->right; return 1;
    case ADT_ADTA_SamplesPerSec: *v = in->sps; return 1;
    case ODT_DTA_Methods: *v = (odt_u32)adt_methods; return 1;
    case ODT_DTA_TriggerMethods: *v = (odt_u32)adt_triggers; return 1;
    default: return 0;
    }
}

static struct odt_IBox domain_of(const struct odt_cenv *env, void *obj)
{
    struct odt_IBox box = { 0, 0, 0, 0 };
    odt_u32 v = 0;
    if (dtc_get(env, obj, ODT_DTA_Domain, &v) && v) box = *(struct odt_IBox *)v;
    else {
        struct odt_Gadget *g = obj;
        box.Left = g->LeftEdge; box.Top = g->TopEdge; box.Width = g->Width; box.Height = g->Height;
    }
    return box;
}

/* The picture's part of the box; the panel's laid out under it */
static struct odt_IBox picture_box(const struct odt_cenv *env, struct adt_inst *in, struct odt_GadgetInfo *gi,
                                   struct odt_IBox box)
{
    if (in->panel) {
        odt_u32 ys = EXT(env)->font_ysize ? EXT(env)->font_ysize(env, gi) : 8;
        adt_panel_layout(&in->pnl, box.Left, box.Top, box.Width, box.Height, ys);
        box.Height = (odt_s16)(box.Height - in->pnl.h);
    } else in->pnl.h = 0;
    return box;
}

static void draw_frame(const struct odt_cenv *env, struct adt_inst *in, void *obj, struct odt_GadgetInfo *gi, void *rp,
                       struct odt_IBox box, odt_s32 top_vert, odt_s32 top_horiz, int panel)
{
    const struct adt_ext *x = EXT(env);
    struct adt_slot *s = adt_cache(env, in, obj, in->play.frame);
    struct adt_BitMap *bm = s ? (s->remapped ? s->remapped : s->msg.alf_BitMap) : in->key_frame;
    odt_s32 back = x->pen ? x->pen(env, gi, 7) : 0, w, h;
    struct odt_IBox pic = panel ? picture_box(env, in, gi, box) : box;
    if (top_vert < 0) top_vert = 0;
    if (top_horiz < 0) top_horiz = 0;
    w = (odt_s32)in->width - top_horiz;
    h = (odt_s32)in->height - top_vert;
    if (w > pic.Width) w = pic.Width;
    if (h > pic.Height) h = pic.Height;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (bm && w && h && x->blit) x->blit(env, bm, top_horiz, top_vert, rp, pic.Left, pic.Top, w, h);
    /* what the picture doesn't cover */
    if (w < pic.Width && pic.Height > 0)
        x->fill(env, rp, back, pic.Left + w, pic.Top, pic.Left + pic.Width - 1, pic.Top + pic.Height - 1);
    if (h < pic.Height && w > 0)
        x->fill(env, rp, back, pic.Left, pic.Top + h, pic.Left + w - 1, pic.Top + pic.Height - 1);
    if (panel && in->panel)
        adt_panel_draw(env, x, rp, &in->pnl, in->play.state, in->play.frame, in->frames, back,
                       x->pen ? x->pen(env, gi, 5) : 3, x->pen ? x->pen(env, gi, 2) : 1);
}

static void redraw(const struct odt_cenv *env, struct adt_inst *in, void *obj)
{
    struct odt_gpRender r;
    void *rp;
    if (!in->have_gi || !env->obtain_rp || !(rp = env->obtain_rp(env, &in->gi))) return;
    r.MethodID = ODT_GM_RENDER;
    r.gpr_GInfo = &in->gi;
    r.gpr_RPort = rp;
    r.gpr_Redraw = ODT_GREDRAW_UPDATE;
    env->do_method(env, obj, &r);
    env->release_rp(env, rp);
}

static void player_on(const struct odt_cenv *env, struct adt_inst *in, void *obj)
{
    if (!in->player && EXT(env)->player_start) in->player = EXT(env)->player_start(env, in->cl, obj, in->fps);
}

static void player_off(const struct odt_cenv *env, struct adt_inst *in)
{
    if (in->player && EXT(env)->player_stop) EXT(env)->player_stop(env, in->player);
    in->player = 0;
}

static void lock(const struct odt_cenv *env, void *obj)
{
    if (env->sem_obtain && dtc_si(obj)) env->sem_obtain(env, &dtc_si(obj)->si_Lock);
}

static void unlock(const struct odt_cenv *env, void *obj)
{
    if (env->sem_release && dtc_si(obj)) env->sem_release(env, &dtc_si(obj)->si_Lock);
}

static odt_u32 start(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 frame)
{
    struct adt_slot *s;
    if (!in->frames) return 0;
    if (frame >= in->frames) frame = in->frames - 1;
    lock(env, obj);
    sound_off(env, in);
    s = adt_cache(env, in, obj, frame);
    adt_play_start(&in->play, frame, s ? s->msg.alf_Duration : 1, frame);
    if (s && s->msg.alf_Sample) frame_sound(env, in, s);
    else sound_from(env, in);
    unlock(env, obj);
    player_on(env, in, obj);
    return 1;
}

static odt_u32 pause_(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 state)
{
    player_off(env, in);
    lock(env, obj);
    sound_off(env, in);
    in->play.state = state;
    /* stop goes back to the first frame (ABI test) */
    if (state == ADT_STOPPED) in->play.frame = 0;
    unlock(env, obj);
    return 1;
}

static odt_u32 locate(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 frame,
                      struct odt_GadgetInfo *gi)
{
    if (!in->frames) return 0;
    if (frame >= in->frames) frame = in->frames - 1;
    lock(env, obj);
    in->play.frame = frame;
    in->play.ticks = frame;
    if (in->play.state == ADT_PLAYING) {
        struct adt_slot *s = adt_cache(env, in, obj, frame);
        sound_off(env, in);
        adt_play_shown(&in->play, s ? s->msg.alf_Duration : 1);
        if (s && s->msg.alf_Sample) frame_sound(env, in, s);
        else sound_from(env, in);
    }
    unlock(env, obj);
    tell_frame(env, in, obj, gi ? gi : (in->have_gi ? &in->gi : 0));
    redraw(env, in, obj);
    return 1;
}

/* Play, pause or stop asked by the program (a method or a trigger): as the
 * panel's buttons do, the frame told (stop goes to frame 0) and redrawn */
static odt_u32 controlled(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 state, odt_u32 frame)
{
    odt_u32 was = in->play.frame, r;
    r = state == ADT_PLAYING ? start(env, in, obj, frame) : pause_(env, in, obj, state);
    if (in->play.frame != was) tell_frame(env, in, obj, in->have_gi ? &in->gi : 0);
    redraw(env, in, obj);
    return r;
}

void adt_tick(const struct odt_cenv *env, struct odt_IClass *cl, void *obj)
{
    struct adt_inst *in = ODT_INST_DATA(cl, obj);
    int changed;
    lock(env, obj);
    changed = adt_play_tick(&in->play);
    if (changed) {
        struct adt_slot *s = adt_cache(env, in, obj, in->play.frame);
        adt_play_shown(&in->play, s ? s->msg.alf_Duration : 1);
        if (s && s->msg.alf_Sample) frame_sound(env, in, s);
        else if (in->play.frame == 0 && in->play.ticks == 0) {
            sound_off(env, in);       /* round again: the sound from its start */
            sound_from(env, in);
        }
    } else if (in->play.state != ADT_PLAYING)
        sound_off(env, in);
    unlock(env, obj);
    if (changed) {
        tell_frame(env, in, obj, in->have_gi ? &in->gi : 0);
        redraw(env, in, obj);
    }
}

static odt_u32 layout(const struct odt_cenv *env, struct adt_inst *in, void *obj, struct odt_GadgetInfo *gi)
{
    struct odt_IBox box = domain_of(env, obj), pic;
    struct odt_DTSpecialInfo *si = dtc_si(obj);
    struct odt_TagItem t[8];
    if (gi) {
        dtc_copy(&in->gi, gi, sizeof in->gi);
        in->have_gi = 1;
        if (!in->screen) in->screen = gi->gi_Screen;
    }
    pic = picture_box(env, in, gi, box);
    if (si) {
        si->si_VertUnit = 1;
        si->si_VisVert = pic.Height;
        si->si_TotVert = (odt_s32)in->height;
        si->si_HorizUnit = 1;
        si->si_VisHoriz = pic.Width;
        si->si_TotHoriz = (odt_s32)in->width;
    }
    t[0].ti_Tag = ODT_DTA_VisibleVert;  t[0].ti_Data = (odt_u32)pic.Height;
    t[1].ti_Tag = ODT_DTA_TotalVert;    t[1].ti_Data = in->height;
    t[2].ti_Tag = ODT_DTA_NominalVert;  t[2].ti_Data = in->height + in->pnl.h;
    t[3].ti_Tag = ODT_DTA_VisibleHoriz; t[3].ti_Data = (odt_u32)pic.Width;
    t[4].ti_Tag = ODT_DTA_TotalHoriz;   t[4].ti_Data = in->width;
    t[5].ti_Tag = ODT_DTA_NominalHoriz; t[5].ti_Data = in->width;
    t[6].ti_Tag = ODT_DTA_Sync;         t[6].ti_Data = 1;
    t[7].ti_Tag = ODT_TAG_DONE;         t[7].ti_Data = 0;
    notify(env, obj, gi, t);
    return in->height;
}

odt_u32 adt_inst_size(void) { return sizeof(struct adt_inst); }

/* ---- writing: IFF ANIM ---------------------------------------------------------- */

static void aw_put32(odt_u8 *p, odt_u32 v)
{
    p[0] = (odt_u8)(v >> 24); p[1] = (odt_u8)(v >> 16); p[2] = (odt_u8)(v >> 8); p[3] = (odt_u8)v;
}

static void aw_put16(odt_u8 *p, odt_u32 v)
{
    p[0] = (odt_u8)(v >> 8); p[1] = (odt_u8)v;
}

/* ByteRun1 (the ILBM spec's): runs of 3 to 128 as -(n-1) and the byte,
 * else literals of up to 128 as n-1 and the bytes */
static odt_u32 aw_byterun1(const odt_u8 *in, odt_u32 n, odt_u8 *out)
{
    odt_u32 o = 0, i = 0;
    while (i < n) {
        odt_u32 r = 1, s, l = 0;
        while (i + r < n && r < 128 && in[i + r] == in[i]) r++;
        if (r >= 3) {
            out[o++] = (odt_u8)(257 - r);
            out[o++] = in[i];
            i += r;
            continue;
        }
        s = i;
        while (i < n && l < 128) {
            if (l && i + 2 < n && in[i] == in[i + 1] && in[i] == in[i + 2]) break;
            i++;
            l++;
        }
        out[o++] = (odt_u8)(l - 1);
        while (s < i) out[o++] = in[s++];
    }
    return o;
}

/* One column of a plane in method 5: its op count, then skips (1-127
 * rows), runs (0, count, byte) and literals (0x80 | n, n bytes), from the
 * top; rows left unchanged at the bottom need no op. The bytes written,
 * or 0 when the column would need more than 255 ops. */
static odt_u32 aw_column(const odt_u8 *cur, const odt_u8 *ref, odt_u32 rb, odt_u32 rows, odt_u8 *out)
{
    odt_u32 o = 1, ops = 0, y = 0;
    while (y < rows) {
        odt_u32 s = 0, r, n;
        while (y + s < rows && cur[(y + s) * rb] == ref[(y + s) * rb]) s++;
        if (y + s == rows) break;
        y += s;
        while (s) {                     /* a skip op takes 1 to 127 rows */
            odt_u32 k = s > 127 ? 127 : s;
            out[o++] = (odt_u8)k;
            ops++;
            s -= k;
        }
        r = 1;
        while (y + r < rows && r < 255 && cur[(y + r) * rb] == cur[y * rb]) r++;
        if (r >= 4) {                   /* a run: 3 bytes for 4 or more rows */
            out[o++] = 0;
            out[o++] = (odt_u8)r;
            out[o++] = cur[y * rb];
            ops++;
            y += r;
            continue;
        }
        /* a literal, up to two unchanged rows together or a run of 4 */
        n = 0;
        while (y + n < rows && n < 127) {
            odt_u32 k = y + n, q = 1;
            if (cur[k * rb] == ref[k * rb] && (k + 1 == rows || cur[(k + 1) * rb] == ref[(k + 1) * rb]))
                break;
            if (n) {
                while (k + q < rows && q < 4 && cur[(k + q) * rb] == cur[k * rb]) q++;
                if (q == 4) break;
            }
            n++;
        }
        out[o++] = (odt_u8)(0x80 | n);
        while (n--) out[o++] = cur[(y++) * rb];
        ops++;
    }
    if (ops > 255) return 0;
    out[0] = (odt_u8)ops;
    return o;
}

/* The same column with the fewest ops: a skip to the first changed row,
 * then literals of 127 to the last. 0 if even that needs more than 255. */
static odt_u32 aw_column_few(const odt_u8 *cur, const odt_u8 *ref, odt_u32 rb, odt_u32 rows, odt_u8 *out)
{
    odt_u32 o = 1, ops = 0, first = 0, last = rows;
    while (first < rows && cur[first * rb] == ref[first * rb]) first++;
    while (last > first && cur[(last - 1) * rb] == ref[(last - 1) * rb]) last--;
    while (first) {
        odt_u32 k = first > 127 ? 127 : first;
        out[o++] = (odt_u8)k;
        ops++;
        first -= k;
        cur += k * rb;
        ref += k * rb;
        last -= k;
    }
    while (last) {
        odt_u32 n = last > 127 ? 127 : last;
        out[o++] = (odt_u8)(0x80 | n);
        last -= n;
        while (n--) {
            out[o++] = *cur;
            cur += rb;
        }
        ops++;
    }
    if (ops > 255) return 0;
    out[0] = (odt_u8)ops;
    return o;
}

/* A plane's method-5 data, or 0 when it is the same as ref's; ~0 when a
 * column can't be put in 255 ops (more than 32,000 rows) */
static odt_u32 aw_plane(const odt_u8 *cur, const odt_u8 *ref, odt_u32 rb, odt_u32 rows, odt_u8 *out)
{
    odt_u32 x, o = 0, i;
    for (i = 0; i < rb * rows && cur[i] == ref[i]; i++)
        ;
    if (i == rb * rows) return 0;
    for (x = 0; x < rb; x++) {
        odt_u32 n = aw_column(cur + x, ref + x, rb, rows, out + o);
        if (!n && !(n = aw_column_few(cur + x, ref + x, rb, rows, out + o))) return ~0UL;
        o += n;
    }
    return o;
}

/* Frame f's planes into dst (depth planes of rows x rb, one after another) */
static int aw_take(const struct adt_aw_spec *s, const struct adt_aw_io *io, odt_u32 f, odt_u32 rb, odt_u8 *dst)
{
    const struct adt_BitMap *bm = io->frame(io->h, f);
    odt_u32 p, y, x, last = s->width & 7 ? (odt_u32)(0xFF00 >> (s->width & 7)) & 0xFF : 0xFF;
    odt_u32 used = (s->width + 7) >> 3;
    if (!bm) return 0;
    for (p = 0; p < s->depth; p++) {
        const odt_u8 *src = p < bm->Depth ? bm->Planes[p] : 0;
        if (src == (const odt_u8 *)~0UL) src = 0;   /* a plane of ones: read as 0 */
        for (y = 0; y < s->height; y++) {
            odt_u8 *d = dst + (p * s->height + y) * rb;
            for (x = 0; x < rb; x++)
                d[x] = (src && y < bm->Rows && x < bm->BytesPerRow && x < used) ? src[y * bm->BytesPerRow + x] : 0;
            if (used) d[used - 1] &= (odt_u8)last;
        }
    }
    io->done(io->h, f);
    return 1;
}

odt_s32 adt_anim_write(const struct adt_aw_spec *s, const struct adt_aw_io *io)
{
    odt_u32 rb, size, f, o, cap, ncol, pl, body, form;
    odt_u8 *buf[2] = { 0, 0 }, *cur = 0, *out = 0, *b;
    odt_s32 err = 0;

    if (!s || !io || !s->width || !s->height || s->width > 32767 || s->height > 32767 || !s->depth ||
        s->depth > 8 || !s->frames)
        return DTC_ERROR_REQUIRED_ARG_MISSING;
    rb = ((s->width + 15) >> 4) << 1;
    size = rb * s->height * s->depth;
    ncol = s->colors ? (s->ncolors > 256 ? 256 : s->ncolors) : 0;
    /* the most a frame can take: a literal for every row of every column
     * (and an op byte each 127 rows), or ByteRun1's row with a count byte
     * each 128 bytes and one more */
    cap = 12 + 8 + 20 + 8 + 768 + 2 + 12 + 8 + 48 + 8 + 64 + 2
        + s->depth * (s->height * (rb + rb / 128 + 2) + rb * (s->height / 127 + 3));
    buf[0] = io->alloc(io->h, size);
    buf[1] = io->alloc(io->h, size);
    cur = io->alloc(io->h, size);
    out = io->alloc(io->h, cap);
    if (!buf[0] || !buf[1] || !cur || !out) {
        err = DTC_ERROR_NO_FREE_STORE;
        goto done;
    }

    /* FORM ANIM, its size patched at the end */
    aw_put32(out, ODT_ID('F', 'O', 'R', 'M'));
    aw_put32(out + 4, 0);
    aw_put32(out + 8, ODT_ID('A', 'N', 'I', 'M'));
    if ((err = io->put(io->h, out, 12))) goto done;
    form = 12;

    for (f = 0; f < s->frames; f++) {
        odt_u8 *ref;
        if (!aw_take(s, io, f, rb, cur)) {
            err = ODT_DTERROR_COULDNT_SAVE;
            goto done;
        }
        b = out;
        aw_put32(b, ODT_ID('F', 'O', 'R', 'M'));
        aw_put32(b + 8, ODT_ID('I', 'L', 'B', 'M'));
        o = 12;
        if (f == 0) {
            odt_u32 y, p, c;
            aw_put32(b + o, ODT_ID('B', 'M', 'H', 'D'));
            aw_put32(b + o + 4, 20);
            aw_put16(b + o + 8, s->width);
            aw_put16(b + o + 10, s->height);
            aw_put16(b + o + 12, 0);
            aw_put16(b + o + 14, 0);
            b[o + 16] = (odt_u8)s->depth;
            b[o + 17] = 0;              /* no mask */
            b[o + 18] = 1;              /* ByteRun1 */
            b[o + 19] = 0;
            aw_put16(b + o + 20, 0);
            b[o + 22] = s->xaspect ? s->xaspect : 1;
            b[o + 23] = s->yaspect ? s->yaspect : 1;
            aw_put16(b + o + 24, s->width);
            aw_put16(b + o + 26, s->height);
            o += 28;
            if (ncol) {
                aw_put32(b + o, ODT_ID('C', 'M', 'A', 'P'));
                aw_put32(b + o + 4, ncol * 3);
                o += 8;
                for (c = 0; c < ncol; c++) {
                    b[o++] = s->colors[c].red;
                    b[o++] = s->colors[c].green;
                    b[o++] = s->colors[c].blue;
                }
                if (o & 1) b[o++] = 0;
            }
            if (s->has_modeid) {
                aw_put32(b + o, ODT_ID('C', 'A', 'M', 'G'));
                aw_put32(b + o + 4, 4);
                aw_put32(b + o + 8, s->modeid);
                o += 12;
            }
            body = o;
            aw_put32(b + o, ODT_ID('B', 'O', 'D', 'Y'));
            o += 8;
            for (y = 0; y < s->height; y++)       /* interleaved: each row's planes in turn */
                for (p = 0; p < s->depth; p++)
                    o += aw_byterun1(cur + (p * s->height + y) * rb, rb, b + o);
            aw_put32(b + body + 4, o - body - 8);
            if (o & 1) b[o++] = 0;
            for (p = 0; p < size; p++) buf[0][p] = cur[p];
        } else {
            odt_u32 dlta, p;
            ref = f == 1 ? buf[0] : buf[f & 1];    /* the frame two before; frame 1 on frame 0 */
            aw_put32(b + o, ODT_ID('A', 'N', 'H', 'D'));
            aw_put32(b + o + 4, 40);
            for (p = 0; p < 40; p++) b[o + 8 + p] = 0;
            b[o + 8] = 5;                         /* byte vertical delta */
            aw_put16(b + o + 10, s->width);
            aw_put16(b + o + 12, s->height);
            aw_put32(b + o + 18, f * s->reltime); /* abstime */
            aw_put32(b + o + 22, s->reltime);     /* reltime */
            /* interleave 0 (two frames back), bits 0: stored, not XORed */
            o += 48;
            dlta = o;
            aw_put32(b + o, ODT_ID('D', 'L', 'T', 'A'));
            o += 8;
            pl = o;
            for (p = 0; p < 64; p++) b[o + p] = 0;   /* 8 plane offsets, 8 unused */
            o += 64;
            for (p = 0; p < s->depth; p++) {
                odt_u32 n = aw_plane(cur + p * s->height * rb, ref + p * s->height * rb, rb, s->height, b + o);
                if (n == ~0UL) {
                    err = ODT_DTERROR_COULDNT_SAVE;
                    goto done;
                }
                if (n) {
                    aw_put32(b + pl + 4 * p, o - pl);
                    o += n;
                }
            }
            aw_put32(b + dlta + 4, o - dlta - 8);
            if (o & 1) b[o++] = 0;
            for (p = 0; p < size; p++) buf[f & 1][p] = cur[p];
        }
        aw_put32(b + 4, o - 8);
        if ((err = io->put(io->h, b, o))) goto done;
        form += o;
    }
    err = io->patch(io->h, 4, form - 8);
done:
    if (buf[0]) io->free(io->h, buf[0]);
    if (buf[1]) io->free(io->h, buf[1]);
    if (cur) io->free(io->h, cur);
    if (out) io->free(io->h, out);
    return err;
}

/* The class's side: frames from the subclass, the file through DOS */
struct aw_glue {
    const struct odt_cenv *env;
    struct adt_inst *in;
    void *obj;
    odt_u32 fh;
    odt_s32 start;                      /* where the file began */
    struct adt_Frame msg;
    odt_u8 newformat;
};

static void *aw_alloc(void *h, odt_u32 n)
{
    struct aw_glue *g = h;
    return g->env->alloc(g->env, n);
}

static void aw_free(void *h, void *p)
{
    struct aw_glue *g = h;
    g->env->free(g->env, p);
}

static odt_s32 aw_write(void *h, const odt_u8 *p, odt_u32 n)
{
    struct aw_glue *g = h;
    if (g->env->write(g->env, g->fh, p, (odt_s32)n) != (odt_s32)n)
        return g->env->io_err ? (g->env->io_err(g->env) ? g->env->io_err(g->env) : ODT_DTERROR_COULDNT_SAVE)
                              : ODT_DTERROR_COULDNT_SAVE;
    return 0;
}

static odt_s32 aw_patch(void *h, odt_u32 at, odt_u32 v)
{
    struct aw_glue *g = h;
    odt_u8 b[4];
    aw_put32(b, v);
    if (g->env->seek(g->env, g->fh, g->start + (odt_s32)at, -1) < 0) return ODT_DTERROR_COULDNT_SAVE;
    if (aw_write(h, b, 4)) return ODT_DTERROR_COULDNT_SAVE;
    if (g->env->seek(g->env, g->fh, 0, 1) < 0) return ODT_DTERROR_COULDNT_SAVE;
    return 0;
}

static void aw_done(void *h, odt_u32 f)
{
    struct aw_glue *g = h;
    (void)f;
    g->msg.MethodID = g->newformat == NF_NEW ? ADT_ADTM_UNLOADNEWFORMATFRAME : ADT_ADTM_UNLOADFRAME;
    g->env->do_method(g->env, g->obj, &g->msg);
}

static const struct adt_BitMap *aw_frame(void *h, odt_u32 f)
{
    struct aw_glue *g = h;
    odt_u8 *z = (odt_u8 *)&g->msg;
    odt_u32 i, r = 0;
    for (i = 0; i < sizeof g->msg; i++) z[i] = 0;
    g->msg.alf_TimeStamp = f;
    g->msg.alf_Frame = f;
    g->msg.alf_Size = sizeof g->msg;
    g->newformat = NF_OLD;
    if (g->in->newformat != NF_OLD) {
        g->msg.MethodID = ADT_ADTM_LOADNEWFORMATFRAME;
        r = g->env->do_method(g->env, g->obj, &g->msg);
        if (r || g->msg.alf_BitMap) g->newformat = NF_NEW;
    }
    if (g->newformat == NF_OLD) {
        for (i = 0; i < sizeof g->msg; i++) z[i] = 0;
        g->msg.alf_TimeStamp = f;
        g->msg.alf_Frame = f;
        g->msg.MethodID = ADT_ADTM_LOADFRAME;
        r = g->env->do_method(g->env, g->obj, &g->msg);
    }
    if (!g->msg.alf_BitMap) {
        if (r) aw_done(h, f);
        return 0;
    }
    return g->msg.alf_BitMap;
}

/* DTM_WRITE in IFF mode: the frames the subclass gives, as an IFF ANIM */
static odt_u32 write_anim(const struct odt_cenv *env, struct adt_inst *in, void *obj, odt_u32 fh)
{
    struct aw_glue g;
    struct adt_aw_spec s;
    struct adt_aw_io io;
    odt_s32 err;
    odt_u32 depth = in->depth;
    if (!fh || !env->write || !env->seek || !in->frames || !in->width || !in->height) {
        if (env->set_io_err) env->set_io_err(env, DTC_ERROR_REQUIRED_ARG_MISSING);
        return 0;
    }
    if (!depth || depth > 8) {           /* the colours say how many planes, else the key frame */
        depth = 1;
        while (depth < 8 && (1UL << depth) < in->num_colors) depth++;
        if (in->key_frame && in->key_frame->Depth > depth && in->key_frame->Depth <= 8) depth = in->key_frame->Depth;
    }
    g.env = env;
    g.in = in;
    g.obj = obj;
    g.fh = fh;
    g.newformat = NF_OLD;
    if ((g.start = env->seek(env, fh, 0, 0)) < 0) {
        if (env->set_io_err) env->set_io_err(env, ODT_DTERROR_COULDNT_SAVE);
        return 0;
    }
    s.width = in->width;
    s.height = in->height;
    s.depth = depth;
    s.frames = in->frames;
    s.ncolors = in->colregs ? in->num_colors : 0;
    s.colors = in->colregs;
    s.modeid = in->mode_id;
    s.has_modeid = in->mode_id != ~0UL;
    s.xaspect = s.yaspect = 1;
    s.pad = 0;
    s.reltime = in->fps ? (60 + in->fps / 2) / in->fps : 6;
    if (!s.reltime) s.reltime = 1;
    io.h = &g;
    io.alloc = aw_alloc;
    io.free = aw_free;
    io.put = aw_write;
    io.patch = aw_patch;
    io.frame = aw_frame;
    io.done = aw_done;
    lock(env, obj);                      /* the player waits while the frames are taken */
    err = adt_anim_write(&s, &io);
    unlock(env, obj);
    if (err) {
        if (env->set_io_err) env->set_io_err(env, err);
        return 0;
    }
    return 1;
}

odt_u32 adt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg;
    struct adt_inst *in = (id == ODT_OM_NEW) ? 0 : ODT_INST_DATA(cl, obj);
    switch (id) {
    case ODT_OM_NEW: {
        struct odt_opSet *ops = msg;
        odt_u32 v = 0;
        void *o = (void *)env->do_super(env, cl, obj, msg);
        if (!o) return 0;
        in = ODT_INST_DATA(cl, o);
        in->cl = cl;
        in->fps = DEF_FPS;
        in->increment = 1;
        in->preload = DEF_PRELOAD;
        in->remap = 1;
        in->volume = DEF_VOLUME;
        in->cycles = 1;
        in->repeat = 1;
        adt_play_init(&in->play, 0);
        /* on unless the creation tags say: datatypesclass always answers
         * DTA_ControlPanel (FALSE by default), so its answer can't tell */
        (void)v;
        in->panel = 1;                  /* (ABI test) */
        set_attrs(env, in, ops->ops_AttrList);
        in->remap_stale = 0;
        return (odt_u32)o;
    }
    case ODT_OM_DISPOSE:
        player_off(env, in);
        sound_off(env, in);
        adt_cache_flush(env, in, obj);
        free_colors(env, in);
        /* ADTA_KeyFrame stays the subclass's to free (ABI test) */
        return env->do_super(env, cl, obj, msg);
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        struct odt_opSet *ops = msg;
        odt_u32 r = env->do_super(env, cl, obj, msg), frame = in->play.frame;
        const struct odt_TagItem *t = dtc_find_tag(ops->ops_AttrList, ADT_ADTA_Frame);
        lock(env, obj);
        r += (odt_u32)set_attrs(env, in, ops->ops_AttrList);
        if (in->remap_stale) {
            /* frames remapped for the old pens or screen: loaded again */
            release_pens(env, in);
            adt_cache_flush(env, in, obj);
            in->remap_stale = 0;
        }
        unlock(env, obj);
        if (t && t->ti_Data != frame && t->ti_Data < in->frames) {
            in->play.frame = frame;
            locate(env, in, obj, t->ti_Data, ops->ops_GInfo);
        }
        return r;
    }
    case ODT_OM_GET: {
        struct odt_opGet *g = msg;
        if (get_attr(in, g->opg_AttrID, g->opg_Storage)) return 1;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_DTM_PROCLAYOUT:
    case ODT_DTM_ASYNCLAYOUT: {
        struct odt_gpLayout *gpl = msg;
        odt_u32 r;
        if (id == ODT_DTM_PROCLAYOUT) env->do_super(env, cl, obj, msg);
        if (id == ODT_DTM_PROCLAYOUT) lock(env, obj);
        r = layout(env, in, obj, gpl->gpl_GInfo);
        if (id == ODT_DTM_PROCLAYOUT) unlock(env, obj);
        return r;
    }
    case ODT_GM_RENDER: {
        struct odt_gpRender *r = msg;
        struct odt_DTSpecialInfo *si = dtc_si(obj);
        if (si && env->sem_attempt && !env->sem_attempt(env, &si->si_Lock)) return 0;
        if (r->gpr_GInfo) {
            dtc_copy(&in->gi, r->gpr_GInfo, sizeof in->gi);
            in->have_gi = 1;
            if (!in->screen) in->screen = r->gpr_GInfo->gi_Screen;
        }
        draw_frame(env, in, obj, r->gpr_GInfo, r->gpr_RPort, domain_of(env, obj), si ? si->si_TopVert : 0,
                   si ? si->si_TopHoriz : 0, 1);
        if (si && env->sem_attempt) env->sem_release(env, &si->si_Lock);
        return 1;
    }
    case ODT_DTM_WRITE: {
        struct odt_dtWrite *w = msg;
        if (w->dtw_Mode != ODT_DTWM_IFF)
            return env->do_super(env, cl, obj, msg);   /* raw: the file as it came, or the subclass's own */
        return write_anim(env, in, obj, w->dtw_FileHandle);
    }
    case ODT_DTM_DRAW: {
        struct odt_dtDraw *d = msg;
        struct odt_IBox box;
        box.Left = (odt_s16)d->dtd_Left;
        box.Top = (odt_s16)d->dtd_Top;
        box.Width = (odt_s16)d->dtd_Width;
        box.Height = (odt_s16)d->dtd_Height;
        draw_frame(env, in, obj, 0, d->dtd_RPort, box, d->dtd_TopVert, d->dtd_TopHoriz, 0);
        return 1;
    }
    case ODT_GM_HITTEST: {
        struct odt_gpHitTest *h = msg;
        odt_u32 f;
        struct odt_Gadget *g = obj;
        if (in->panel && in->pnl.h &&
            adt_panel_hit(&in->pnl, h->gpht_Mouse.x + g->LeftEdge, h->gpht_Mouse.y + g->TopEdge, in->frames, &f))
            return ODT_GMR_GADGETHIT;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_GM_GOACTIVE: {
        struct odt_gpInput *gp = msg;
        struct odt_Gadget *g = obj;
        odt_u32 f = 0;
        int hit = in->panel && in->pnl.h
                      ? adt_panel_hit(&in->pnl, gp->gpi_Mouse.x + g->LeftEdge, gp->gpi_Mouse.y + g->TopEdge,
                                      in->frames, &f)
                      : ADT_HIT_NONE;
        if (hit == ADT_HIT_NONE) return env->do_super(env, cl, obj, msg);
        if (hit == ADT_HIT_PLAY) {
            if (in->play.state == ADT_PLAYING) pause_(env, in, obj, ADT_PAUSED);
            else start(env, in, obj, in->play.frame);
            redraw(env, in, obj);
        } else if (hit == ADT_HIT_STOP) {
            pause_(env, in, obj, ADT_STOPPED);
            tell_frame(env, in, obj, gp->gpi_GInfo);
            redraw(env, in, obj);
        } else
            locate(env, in, obj, f, gp->gpi_GInfo);
        return ODT_GMR_NOREUSE;
    }
    case ODT_DTM_TRIGGER: {
        struct odt_dtTrigger *t = msg;
        switch (t->dtt_Function & ODT_STMF_METHOD_MASK) {
        case ODT_STM_PLAY:
        case ODT_STM_RESUME: return controlled(env, in, obj, ADT_PLAYING, in->play.frame);
        case ODT_STM_PAUSE: return controlled(env, in, obj, ADT_PAUSED, 0);
        case ODT_STM_STOP: return controlled(env, in, obj, ADT_STOPPED, 0);
        case ODT_STM_REWIND: return locate(env, in, obj, 0, t->dtt_GInfo);
        case ODT_STM_FASTFORWARD: return locate(env, in, obj, in->frames ? in->frames - 1 : 0, t->dtt_GInfo);
        case ODT_STM_LOCATE: return locate(env, in, obj, (odt_u32)t->dtt_Data, t->dtt_GInfo);
        default: return env->do_super(env, cl, obj, msg);
        }
    }
    case ADT_ADTM_START: return controlled(env, in, obj, ADT_PLAYING, ((struct adt_Start *)msg)->asa_Frame);
    case ADT_ADTM_PAUSE: return controlled(env, in, obj, ADT_PAUSED, 0);
    case ADT_ADTM_STOP: return controlled(env, in, obj, ADT_STOPPED, 0);
    case ADT_ADTM_LOCATE: return locate(env, in, obj, ((struct adt_Start *)msg)->asa_Frame, 0);
    case ADT_ADTM_LOADFRAME: {
        /* a subclass that loads no frames: the key frame is every frame (ABI test) */
        struct adt_Frame *f = msg;
        f->alf_BitMap = in->key_frame;
        f->alf_Duration = 1;
        return in->key_frame != 0;
    }
    case ADT_ADTM_UNLOADFRAME:
        ((struct adt_Frame *)msg)->alf_BitMap = 0;
        return 0;
    case ADT_ADTM_LOADNEWFORMATFRAME:
    case ADT_ADTM_UNLOADNEWFORMATFRAME:
        return 0;                       /* not known: the class asks with ADTM_LOADFRAME */
    default:
        return env->do_super(env, cl, obj, msg);
    }
}
