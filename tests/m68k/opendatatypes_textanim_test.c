/* GFOpenDatatypes text, ascii, animation and anim class cores under qemu-m68k:
 * runs the script tests/m68k/opendatatypes_textanim_ref.py writes and logs
 * every answer and every call the cores make through their env, for the
 * test to compare with the log python's own models expect.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The superclass, datatypesclass, is a mock here (domain, source,
 * DTSpecialInfo, OM_NOTIFY logged): the class cores are tested, not it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text/text_core.h"
#include "ascii/ascii_core.h"
#include "animation/animation_core.h"
#include "anim/anim_core.h"

static long outstanding;                /* allocations not freed */

static void *xalloc(odt_u32 n)
{
    odt_u32 *p = calloc(1, n + 8);
    if (!p) return 0;
    p[0] = 0x4d454d21;
    p[1] = n;
    outstanding++;
    return p + 2;
}

static void xfree(void *q)
{
    odt_u32 *p = q;
    if (!q) return;
    p -= 2;
    if (p[0] != 0x4d454d21) { printf("BAD FREE\n"); exit(3); }
    p[0] = 0;
    outstanding--;
    free(p);
}

static void hex(const void *d, odt_u32 n)
{
    const odt_u8 *b = d;
    odt_u32 i;
    for (i = 0; i < n; i++) printf("%02x", b[i]);
}

static int unhex(const char *s, odt_u8 *out)
{
    int n = 0;
    while (s[0] && s[1]) {
        unsigned v;
        sscanf(s, "%2x", &v);
        out[n++] = (odt_u8)v;
        s += 2;
    }
    return n;
}

/* ---- the env ------------------------------------------------------------------- */

static odt_u32 m_do_method(const struct odt_cenv *env, void *obj, void *msg);
static odt_u32 m_do_super(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
static odt_u32 m_coerce(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
static void *e_alloc(const struct odt_cenv *env, odt_u32 n) { (void)env; return xalloc(n); }
static void e_free(const struct odt_cenv *env, void *p) { (void)env; xfree(p); }
static odt_s32 ioerr;
static odt_s32 e_io_err(const struct odt_cenv *env) { (void)env; return ioerr; }
static void e_set_io_err(const struct odt_cenv *env, odt_s32 c) { (void)env; ioerr = c; }
static int sem_depth;
static void e_sem_init(const struct odt_cenv *env, struct odt_SignalSemaphore *s) { (void)env; (void)s; }
static void e_sem_obtain(const struct odt_cenv *env, struct odt_SignalSemaphore *s) { (void)env; (void)s; sem_depth++; }
static int e_sem_attempt(const struct odt_cenv *env, struct odt_SignalSemaphore *s)
{ (void)env; (void)s; sem_depth++; return 1; }
static void e_sem_release(const struct odt_cenv *env, struct odt_SignalSemaphore *s)
{ (void)env; (void)s; sem_depth--; if (sem_depth < 0) printf("SEM UNDERFLOW\n"); }

/* files: one "file" in memory, read in uneven pieces */
static odt_u8 file_data[65536];
static odt_u32 file_len, file_pos;
static odt_s32 e_read(const struct odt_cenv *env, odt_u32 fh, void *buf, odt_s32 n)
{
    odt_u32 k;
    (void)env;
    if (fh != 0x1234) return -1;
    if (n > 777) n = 777;
    k = file_len - file_pos < (odt_u32)n ? file_len - file_pos : (odt_u32)n;
    memcpy(buf, file_data + file_pos, k);
    file_pos += k;
    return (odt_s32)k;
}
static odt_s32 e_seek(const struct odt_cenv *env, odt_u32 fh, odt_s32 off, odt_s32 mode)
{
    odt_s32 old = (odt_s32)file_pos;
    (void)env;
    if (fh != 0x1234) return -1;
    if (mode == -1) file_pos = (odt_u32)off;
    return old;
}
static odt_s32 e_write(const struct odt_cenv *env, odt_u32 fh, const void *buf, odt_s32 n)
{
    (void)env;
    printf("write %lu ", (unsigned long)fh);
    hex(buf, (odt_u32)n);
    printf("\n");
    return n;
}

/* iffparse: a reader over memory that stops at the stop chunks, and a
 * writer that logs what it pushes */
struct miff { const odt_u8 *d; odt_u32 len, at, stop_type, stop_id; odt_u32 cur_id, cur_size, cur_at, read; int write; };
static void *e_iff_open_mem_(const odt_u8 *d, odt_u32 len)
{
    struct miff *f = calloc(1, sizeof *f);
    f->d = d;
    f->len = len;
    return f;
}
static void *e_iff_open_clip(const struct odt_cenv *env, odt_u32 unit, int write)
{
    struct miff *f = calloc(1, sizeof *f);
    (void)env;
    f->write = write;
    printf("clip open %lu %d\n", (unsigned long)unit, write);
    return f;
}
static void e_iff_close(const struct odt_cenv *env, void *iff) { (void)env; if (((struct miff *)iff)->write) printf("clip close\n"); free(iff); }
static odt_s32 e_iff_stop(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id)
{
    struct miff *f = iff;
    (void)env;
    f->stop_type = type;
    f->stop_id = id;
    return 0;
}
static odt_u32 be32(const odt_u8 *p) { return ((odt_u32)p[0] << 24) | ((odt_u32)p[1] << 16) | ((odt_u32)p[2] << 8) | p[3]; }
/* Walks FORMs (and CAT/LIST) to the next stop chunk */
static odt_s32 e_iff_parse(const struct odt_cenv *env, void *iff, odt_s32 mode)
{
    struct miff *f = iff;
    (void)env; (void)mode;
    while (f->at + 8 <= f->len) {
        odt_u32 id = be32(f->d + f->at), size = be32(f->d + f->at + 4);
        if (id == ODT_ID('F', 'O', 'R', 'M') || id == ODT_ID('C', 'A', 'T', ' ') || id == ODT_ID('L', 'I', 'S', 'T')) {
            f->at += 12;
            continue;
        }
        if (f->at + 8 + size > f->len) return -1;
        f->cur_id = id;
        f->cur_size = size;
        f->cur_at = f->at + 8;
        f->read = 0;
        f->at += 8 + size + (size & 1);
        if (id == f->stop_id) return 0;
    }
    return -1;
}
static odt_u32 e_iff_current(const struct odt_cenv *env, void *iff, odt_u32 *type, odt_s32 *size)
{
    struct miff *f = iff;
    (void)env;
    *type = f->stop_type;
    *size = (odt_s32)f->cur_size;
    return f->cur_id;
}
static odt_s32 e_iff_read_chunk(const struct odt_cenv *env, void *iff, void *buf, odt_s32 n)
{
    struct miff *f = iff;
    odt_u32 k = f->cur_size - f->read;
    (void)env;
    if ((odt_u32)n < k) k = (odt_u32)n;
    memcpy(buf, f->d + f->cur_at + f->read, k);
    f->read += k;
    return (odt_s32)k;
}
static void id4(odt_u32 id) { printf("%c%c%c%c", (int)(id >> 24), (int)(id >> 16) & 255, (int)(id >> 8) & 255, (int)id & 255); }
static odt_s32 e_iff_push(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id, odt_s32 size)
{
    (void)env; (void)iff;
    printf("clip push ");
    id4(type);
    printf(" ");
    id4(id);
    printf(" %ld\n", (long)size);
    return 0;
}
static odt_s32 e_iff_write_chunk(const struct odt_cenv *env, void *iff, const void *buf, odt_s32 n)
{
    (void)env; (void)iff;
    printf("clip write ");
    hex(buf, (odt_u32)n);
    printf("\n");
    return n;
}
static odt_s32 e_iff_pop(const struct odt_cenv *env, void *iff) { (void)env; (void)iff; printf("clip pop\n"); return 0; }

static void *e_obtain_rp(const struct odt_cenv *env, struct odt_GadgetInfo *gi) { (void)env; (void)gi; return (void *)0x5000; }
static void e_release_rp(const struct odt_cenv *env, void *rp) { (void)env; (void)rp; }

/* ---- the text family's services ---------------------------------------------------- */

#define FONT ((void *)0x7000)
static odt_s32 cw(odt_u8 c, odt_u32 style) { return 4 + c % 5 + ((style & TDT_FSF_BOLD) ? 1 : 0); }
static odt_s32 mock_len(const struct tdt_measure *m, odt_u32 style, const char *s, odt_u32 n)
{
    odt_s32 w = 0;
    odt_u32 i;
    (void)m;
    for (i = 0; i < n; i++) w += cw((odt_u8)s[i], style);
    return w;
}
static void *t_open_font(const struct odt_cenv *env, const void *ta) { (void)env; printf("openfont %s\n", *(const char *const *)ta); return FONT; }
static void t_close_font(const struct odt_cenv *env, void *f) { (void)env; (void)f; printf("closefont\n"); }
static void *t_default_font(const struct odt_cenv *env, struct odt_GadgetInfo *gi) { (void)env; (void)gi; return FONT; }
static void t_font_size(const struct odt_cenv *env, void *f, odt_u16 *ys, odt_u16 *bl, odt_u16 *xs)
{ (void)env; (void)f; *ys = 9; *bl = 7; *xs = 6; }
static odt_s32 t_text_length(const struct odt_cenv *env, void *f, odt_u32 style, const char *s, odt_u32 n)
{ (void)env; (void)f; return mock_len(0, style, s, n); }
static void t_text(const struct odt_cenv *env, void *rp, void *font, odt_u32 style, odt_s32 fg, odt_s32 x, odt_s32 y,
                   const char *s, odt_u32 n)
{
    (void)env; (void)rp; (void)font;
    printf("text %lu %ld %ld %ld ", (unsigned long)style, (long)fg, (long)x, (long)y);
    hex(s, n);
    printf("\n");
}
static void t_fill(const struct odt_cenv *env, void *rp, odt_s32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{ (void)env; (void)rp; printf("fill %ld %ld %ld %ld %ld\n", (long)pen, (long)x0, (long)y0, (long)x1, (long)y1); }
static void *t_clip(const struct odt_cenv *env, void *rp, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{ (void)env; (void)rp; printf("clip %ld %ld %ld %ld\n", (long)x0, (long)y0, (long)x1, (long)y1); return (void *)1; }
static void t_unclip(const struct odt_cenv *env, void *rp, void *c) { (void)env; (void)rp; (void)c; printf("unclip\n"); }
static odt_s32 t_pen(const struct odt_cenv *env, struct odt_GadgetInfo *gi, odt_u32 which) { (void)env; (void)gi; return 10 + which; }
static int t_double(const struct odt_cenv *env, odt_u32 s1, odt_u32 m1, odt_u32 s2, odt_u32 m2)
{ (void)env; return (s2 - s1) * 1000000 + m2 - m1 < 500000; }

static const struct tdt_ext text_ext = {
    t_open_font, t_close_font, t_default_font, t_font_size, t_text_length, t_text, t_fill, t_clip, t_unclip, t_pen, t_double
};

/* ---- the animation family's services --------------------------------------------- */

static struct adt_BitMap *a_alloc_bitmap(const struct odt_cenv *env, odt_u32 w, odt_u32 h, odt_u32 depth, void *fr)
{
    struct adt_BitMap *bm = xalloc(sizeof *bm);
    odt_u32 p, bpr = ((w + 15) >> 4) << 1;
    (void)env; (void)fr;
    bm->BytesPerRow = (odt_u16)bpr;
    bm->Rows = (odt_u16)h;
    bm->Depth = (odt_u8)depth;
    for (p = 0; p < depth && p < 8; p++) bm->Planes[p] = xalloc(bpr * h);
    printf("allocbm %lu %lu %lu\n", (unsigned long)w, (unsigned long)h, (unsigned long)depth);
    return bm;
}
static void a_free_bitmap(const struct odt_cenv *env, struct adt_BitMap *bm)
{
    odt_u32 p;
    (void)env;
    for (p = 0; p < 8; p++) xfree(bm->Planes[p]);
    xfree(bm);
    printf("freebm\n");
}
static odt_s32 a_obtain_pen(const struct odt_cenv *env, void *scr, odt_u32 r, odt_u32 g, odt_u32 b)
{
    (void)env; (void)scr;
    return (odt_s32)(((r >> 24) * 3 + (g >> 24) * 5 + (b >> 24) * 7) % 16);
}
static void a_release_pen(const struct odt_cenv *env, void *scr, odt_u32 pen) { (void)env; (void)scr; printf("releasepen %lu\n", (unsigned long)pen); }
static odt_u32 a_screen_depth(const struct odt_cenv *env, void *scr) { (void)env; (void)scr; return 4; }
static void a_blit(const struct odt_cenv *env, struct adt_BitMap *bm, odt_s32 sx, odt_s32 sy, void *rp, odt_s32 dx,
                   odt_s32 dy, odt_s32 w, odt_s32 h)
{
    odt_u32 sum = 0, x, y, p;
    (void)env; (void)rp;
    /* a checksum of the pixels blitted */
    for (y = 0; y < (odt_u32)h; y++)
        for (x = 0; x < (odt_u32)w; x++) {
            odt_u32 px = 0, xx = x + (odt_u32)sx, yy = y + (odt_u32)sy;
            for (p = 0; p < bm->Depth && p < 8; p++)
                if (bm->Planes[p][yy * bm->BytesPerRow + (xx >> 3)] & (0x80 >> (xx & 7))) px |= 1u << p;
            sum = sum * 31 + px;
        }
    printf("blit d%u %ld %ld %ld %ld %ld %ld %08lx\n", bm->Depth, (long)sx, (long)sy, (long)dx, (long)dy, (long)w,
           (long)h, (unsigned long)sum);
}
static void a_bevel(const struct odt_cenv *env, void *rp, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h, int rec)
{ (void)env; (void)rp; printf("bevel %ld %ld %ld %ld %d\n", (long)x, (long)y, (long)w, (long)h, rec); }
static void a_glyph(const struct odt_cenv *env, void *rp, odt_u32 k, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h, odt_s32 pen)
{ (void)env; (void)rp; printf("glyph %lu %ld %ld %ld %ld %ld\n", (unsigned long)k, (long)x, (long)y, (long)w, (long)h, (long)pen); }
static odt_u32 a_font_ysize(const struct odt_cenv *env, struct odt_GadgetInfo *gi) { (void)env; (void)gi; return 8; }
static const odt_s8 *sample_base;
static int a_sound_play(const struct odt_cenv *env, const odt_s8 *d, odt_u32 len, odt_u32 period, odt_u32 vol, odt_u32 cyc)
{
    (void)env;
    printf("sound %ld %lu %lu %lu %lu\n", sample_base ? (long)(d - sample_base) : -1L, (unsigned long)len,
           (unsigned long)period, (unsigned long)vol, (unsigned long)cyc);
    return 1;
}
static void a_sound_stop(const struct odt_cenv *env) { (void)env; printf("soundstop\n"); }
static odt_u32 a_clock(const struct odt_cenv *env) { (void)env; return 3546895UL; }
static void *a_player_start(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, odt_u32 fps)
{ (void)env; (void)cl; (void)obj; printf("player %lu\n", (unsigned long)fps); return (void *)0x9000; }
static void a_player_stop(const struct odt_cenv *env, void *p) { (void)env; (void)p; printf("playerstop\n"); }
static void a_wait_blit(const struct odt_cenv *env) { (void)env; printf("waitblit\n"); }

static const struct adt_ext anim_ext = {
    a_alloc_bitmap, a_free_bitmap, a_obtain_pen, a_release_pen, a_screen_depth, a_blit, t_fill, a_bevel, a_glyph,
    t_pen, a_font_ysize, a_sound_play, a_sound_stop, a_clock, a_player_start, a_player_stop, a_wait_blit
};

static struct odt_cenv env_text, env_anim;

static void env_init(struct odt_cenv *e, const void *ext)
{
    memset(e, 0, sizeof *e);
    e->do_method = m_do_method;
    e->do_super = m_do_super;
    e->coerce = m_coerce;
    e->alloc = e_alloc;
    e->free = e_free;
    e->sem_init = e_sem_init;
    e->sem_obtain = e_sem_obtain;
    e->sem_obtain_shared = e_sem_obtain;
    e->sem_attempt = e_sem_attempt;
    e->sem_release = e_sem_release;
    e->io_err = e_io_err;
    e->set_io_err = e_set_io_err;
    e->read = e_read;
    e->write = e_write;
    e->seek = e_seek;
    e->iff_open_clip = e_iff_open_clip;
    e->iff_close = e_iff_close;
    e->iff_stop = e_iff_stop;
    e->iff_parse = e_iff_parse;
    e->iff_current = e_iff_current;
    e->iff_read_chunk = e_iff_read_chunk;
    e->iff_push = e_iff_push;
    e->iff_write_chunk = e_iff_write_chunk;
    e->iff_pop = e_iff_pop;
    e->obtain_rp = e_obtain_rp;
    e->release_rp = e_release_rp;
    e->class_ext = (void *)ext;
}

/* ---- a mock BOOPSI and datatypesclass ---------------------------------------------- */

struct mclass { struct odt_IClass ic; odt_dispatch_fn fn; };

/* the mock datatypesclass's instance, after the ExtGadget */
struct mdt {
    struct odt_DTSpecialInfo si;
    struct odt_IBox domain;
    odt_u32 source_type, handle, datatype, src_addr, src_size, panel, text_attr;
};
#define GADGET_SIZE ((odt_u16)sizeof(struct odt_ExtGadget))
#define MDT(o) ((struct mdt *)((odt_u8 *)(o) + GADGET_SIZE))

static struct mclass c_dt, c_text, c_ascii, c_anim, c_animation;

static odt_u32 m_do_method(const struct odt_cenv *env, void *obj, void *msg)
{
    struct mclass *c = (struct mclass *)ODT_OCLASS(obj);
    return c->fn(env, &c->ic, obj, msg);
}
static odt_u32 m_do_super(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    struct mclass *c = (struct mclass *)cl->cl_Super;
    return c->fn(env, &c->ic, obj, msg);
}
static odt_u32 m_coerce(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    return ((struct mclass *)cl)->fn(env, cl, obj, msg);
}

static int is_ptr_tag(odt_u32 t) { return t == TDT_TDTA_WordSelect; }

static odt_u32 dt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg;
    (void)cl;
    (void)env;
    switch (id) {
    case ODT_OM_NEW: {
        struct odt_opSet *ops = msg;
        struct odt_IClass *true_cl = obj;
        odt_u32 size = true_cl->cl_InstOffset + true_cl->cl_InstSize;
        struct odt_Object *h = xalloc(sizeof *h + size);
        void *o = h + 1;
        struct odt_Gadget *g = o;
        struct mdt *m = MDT(o);
        const struct odt_TagItem *t, *list = ops->ops_AttrList;
        h->o_Class = true_cl;
        g->SpecialInfo = &m->si;
        m->source_type = ODT_DTST_RAM;
        m->panel = 1;
        while ((t = dtc_next_tag(&list)) != 0) {
            switch (t->ti_Tag) {
            case ODT_GA_Left: m->domain.Left = (odt_s16)t->ti_Data; g->LeftEdge = m->domain.Left; break;
            case ODT_GA_Top: m->domain.Top = (odt_s16)t->ti_Data; g->TopEdge = m->domain.Top; break;
            case ODT_GA_Width: m->domain.Width = (odt_s16)t->ti_Data; g->Width = m->domain.Width; break;
            case ODT_GA_Height: m->domain.Height = (odt_s16)t->ti_Data; g->Height = m->domain.Height; break;
            case ODT_DTA_SourceType: m->source_type = t->ti_Data; break;
            case ODT_DTA_Handle: m->handle = t->ti_Data; break;
            case ODT_DTA_DataType: m->datatype = t->ti_Data; break;
            case ODT_DTA_SourceAddress: m->src_addr = t->ti_Data; break;
            case ODT_DTA_SourceSize: m->src_size = t->ti_Data; break;
            case ODT_DTA_ControlPanel: m->panel = t->ti_Data; break;
            case ODT_DTA_TextAttr: m->text_attr = t->ti_Data; break;
            default: break;
            }
        }
        return (odt_u32)o;
    }
    case ODT_OM_DISPOSE:
        xfree((struct odt_Object *)obj - 1);
        return 0;
    case ODT_OM_GET: {
        struct odt_opGet *g = msg;
        struct mdt *m = MDT(obj);
        switch (g->opg_AttrID) {
        case ODT_DTA_Domain: *g->opg_Storage = (odt_u32)&m->domain; return 1;
        case ODT_DTA_SourceType: *g->opg_Storage = m->source_type; return 1;
        case ODT_DTA_Handle: *g->opg_Storage = m->handle; return 1;
        case ODT_DTA_DataType: *g->opg_Storage = m->datatype; return 1;
        case ODT_DTA_SourceAddress: *g->opg_Storage = m->src_addr; return 1;
        case ODT_DTA_SourceSize: *g->opg_Storage = m->src_size; return 1;
        case ODT_DTA_ControlPanel: *g->opg_Storage = m->panel; return 1;
        case ODT_DTA_TextAttr: *g->opg_Storage = m->text_attr; return 1;
        case ODT_DTA_TopVert: *g->opg_Storage = (odt_u32)m->si.si_TopVert; return 1;
        default: return 0;
        }
    }
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        struct odt_opSet *ops = msg;
        struct mdt *m = MDT(obj);
        const struct odt_TagItem *t, *list = ops->ops_AttrList;
        while ((t = dtc_next_tag(&list)) != 0) {
            if (t->ti_Tag == ODT_DTA_TopVert) m->si.si_TopVert = (odt_s32)t->ti_Data;
            if (t->ti_Tag == ODT_DTA_TopHoriz) m->si.si_TopHoriz = (odt_s32)t->ti_Data;
        }
        return 0;
    }
    case ODT_OM_NOTIFY: {
        struct odt_opUpdate *u = msg;
        const struct odt_TagItem *t, *list = u->opu_AttrList;
        printf("notify");
        while ((t = dtc_next_tag(&list)) != 0) {
            if (is_ptr_tag(t->ti_Tag)) printf(" %lx='%s'", (unsigned long)(t->ti_Tag - ODT_DTA_Dummy), (char *)t->ti_Data);
            else printf(" %lx=%lu", (unsigned long)(t->ti_Tag - ODT_DTA_Dummy), (unsigned long)t->ti_Data);
        }
        printf("\n");
        return 0;
    }
    default:
        return 0;
    }
}

static void mk(struct mclass *c, struct mclass *super, odt_dispatch_fn fn, odt_u32 size)
{
    memset(c, 0, sizeof *c);
    c->fn = fn;
    c->ic.cl_Super = super ? &super->ic : 0;
    c->ic.cl_InstOffset = super ? (odt_u16)(super->ic.cl_InstOffset + super->ic.cl_InstSize) : 0;
    c->ic.cl_InstSize = (odt_u16)size;
}

static void *mnew(const struct odt_cenv *env, struct mclass *c, struct odt_TagItem *tags)
{
    struct odt_opSet s;
    s.MethodID = ODT_OM_NEW;
    s.ops_AttrList = tags;
    s.ops_GInfo = 0;
    return (void *)c->fn(env, &c->ic, &c->ic, &s);
}

static void mdispose(const struct odt_cenv *env, void *o)
{
    odt_u32 m = ODT_OM_DISPOSE;
    m_do_method(env, o, &m);
}

static odt_u32 mget(const struct odt_cenv *env, void *o, odt_u32 tag)
{
    odt_u32 v = 0xdeadbeef;
    dtc_get(env, o, tag, &v);
    return v;
}

static odt_u32 mset(const struct odt_cenv *env, void *o, struct odt_TagItem *tags)
{
    struct odt_opSet s;
    s.MethodID = ODT_OM_SET;
    s.ops_AttrList = tags;
    s.ops_GInfo = 0;
    return m_do_method(env, o, &s);
}

/* ---- script ------------------------------------------------------------------------ */

static struct odt_GadgetInfo ginfo;
static char line[1 << 17];
static odt_u8 bin[1 << 16];
static struct tdt_layout lay;
static struct tdt_measure meas;
static char textbuf[1 << 16];

static void dump_layout(const struct tdt_layout *l)
{
    const struct odt_MinNode *n;
    printf("layout rows %lu width %lu segs %lu\n", (unsigned long)l->rows, (unsigned long)l->width, (unsigned long)l->segs);
    for (n = l->lines.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        const struct tdt_seg *g = (const struct tdt_seg *)n;
        printf("seg %lu %u %u %u %u %u %d %d %lu %u %u ", (unsigned long)g->row, g->l.ln_XOffset, g->l.ln_YOffset,
               g->l.ln_Width, g->l.ln_Height, g->l.ln_Flags, g->l.ln_FgPen, g->l.ln_BgPen, (unsigned long)g->l.ln_Style,
               g->tab, g->brk);
        hex(g->l.ln_Text, g->l.ln_TextLen);
        printf("\n");
    }
}

static void dump_frame(const struct anm_dec *d)
{
    const struct anm_info *in = d->info;
    const odt_u8 *pl = anm_planes(d);
    odt_u32 x, y, p, rows = in->bmhd.h;
    for (y = 0; y < rows; y++) {
        printf("row ");
        for (x = 0; x < in->bmhd.w; x++) {
            odt_u32 v = 0;
            for (p = 0; p < in->depth; p++)
                if (pl[(p * rows + y) * in->bpr + (x >> 3)] & (0x80 >> (x & 7))) v |= 1u << p;
            printf("%02lx", (unsigned long)v);
        }
        printf("\n");
    }
}

static odt_u8 *read_file(const char *path, odt_u32 *len)
{
    FILE *f = fopen(path, "rb");
    odt_u8 *d;
    long n;
    if (!f) { printf("no file %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc((size_t)n + 1);
    if (fread(d, 1, (size_t)n, f) != (size_t)n) exit(2);
    fclose(f);
    *len = (odt_u32)n;
    return d;
}

static void *a_env_alloc(void *ctx, odt_u32 n) { (void)ctx; return xalloc(n); }
static void a_env_free(void *ctx, void *p) { (void)ctx; xfree(p); }

static void run_anim(char *args)
{
    long base = outstanding;
    struct anm_env ae = { 0, a_env_alloc, a_env_free };
    struct anm_info info;
    struct anm_dec dec;
    char *path = strtok(args, " "), *tok;
    odt_u32 len, i;
    odt_u8 *data = read_file(path, &len), rgb[768];
    int r = anm_scan(&ae, data, len, &info);
    if (r) { printf("scan err %d\n", r); free(data); return; }
    printf("anim %u %u %u %lu fps %lu camg %08lx dpan %lu bpr %u\n", info.bmhd.w, info.bmhd.h, info.depth,
           (unsigned long)info.nframes, (unsigned long)info.fps, (unsigned long)anm_mode_id(&info),
           (unsigned long)info.dpan_frames, info.bpr);
    for (i = 0; i < info.nframes; i++) {
        odt_u32 n = anm_colors(&info, data, i, rgb);
        printf("frame %lu dur %lu colors %lu ", (unsigned long)i, (unsigned long)anm_duration(&info, i), (unsigned long)n);
        hex(rgb, n * 3);
        printf("\n");
    }
    if (anm_dec_init(&ae, &dec, &info, data, len)) { printf("dec init fail\n"); return; }
    while ((tok = strtok(0, " ")) != 0) {
        odt_u32 f = (odt_u32)atol(tok);
        r = anm_goto(&dec, f);
        printf("goto %lu r %d cur %ld\n", (unsigned long)f, r, (long)dec.cur);
        if (dec.cur >= 0) dump_frame(&dec);
    }
    anm_dec_free(&ae, &dec);
    anm_free_info(&ae, &info);
    free(data);
    printf("allocs %ld\n", outstanding - base);
}

/* ANIMBACK path: to the last frame, then back a frame at a time to the
 * first; each frame (and anm_goto's answer) as a new decoder going
 * straight to it gives, and no more than nframes + nframes^2 / 4 frames
 * decoded on the way back */
static void run_animback(char *args)
{
    long base = outstanding;
    struct anm_env ae = { 0, a_env_alloc, a_env_free };
    struct anm_info info;
    struct anm_dec a, b;
    odt_u32 len, f, bad = 0, n;
    odt_u8 *data = read_file(strtok(args, " "), &len);
    if (anm_scan(&ae, data, len, &info)) { printf("scan err\n"); free(data); return; }
    n = info.nframes;
    if (anm_dec_init(&ae, &a, &info, data, len)) { printf("dec init fail\n"); return; }
    anm_goto(&a, n - 1);
    a.decoded = 0;
    for (f = n - 1; f-- > 0;) {
        int ra = anm_goto(&a, f), rb;
        if (anm_dec_init(&ae, &b, &info, data, len)) { printf("dec init fail\n"); return; }
        rb = anm_goto(&b, f);
        if (ra != rb || a.cur != b.cur || memcmp(anm_planes(&a), anm_planes(&b), a.size)) {
            printf("animback frame %lu: r %d cur %ld, straight r %d cur %ld\n", (unsigned long)f, ra, (long)a.cur,
                   rb, (long)b.cur);
            bad++;
        }
        anm_dec_free(&ae, &b);
    }
    if (a.decoded > n + n * n / 4) {
        printf("animback %lu frames decoded going back over %lu\n", (unsigned long)a.decoded, (unsigned long)n);
        bad++;
    }
    anm_dec_free(&ae, &a);
    anm_free_info(&ae, &info);
    free(data);
    if (outstanding != base) printf("animback allocs %ld\n", outstanding - base);
    else if (!bad) printf("animback %lu ok\n", (unsigned long)n);
}

/* text pure: TLAY wrap hex | TSEL x0 y0 x1 y1 | TCOPY all | TWORD x y | TRENDER l t w h row px */
static void run_text(const char *cmd, char *args)
{
    if (!strcmp(cmd, "TLAY")) {
        char *w = strtok(args, " "), *h = strtok(0, " ");
        int n = h ? unhex(h, (odt_u8 *)textbuf) : 0;
        tdt_free_layout(&env_text, &lay);
        meas.env = &env_text;
        meas.font = FONT;
        meas.ysize = 9;
        meas.baseline = 7;
        meas.xsize = 6;
        meas.len = mock_len;
        printf("tlay r %ld\n", (long)tdt_layout(&env_text, &meas, textbuf, (odt_u32)n, (odt_u32)atol(w), &lay));
        dump_layout(&lay);
    } else if (!strcmp(cmd, "TSEL")) {
        long a, b, c, d;
        sscanf(args, "%ld %ld %ld %ld", &a, &b, &c, &d);
        tdt_clear_selected(&lay);
        printf("tsel %lu\n", (unsigned long)tdt_select(&env_text, &meas, &lay, a, b, c, d));
        dump_layout(&lay);
    } else if (!strcmp(cmd, "TCOPY")) {
        static char out[1 << 16];
        static odt_u8 form[(1 << 16) + 32];
        odt_u32 n = tdt_copy_text(&lay, atoi(args), out, sizeof out), k;
        printf("tcopy ");
        hex(out, n);
        printf("\n");
        k = tdt_ftxt(out, n, form);
        printf("ftxt ");
        hex(form, k);
        printf("\n");
    } else if (!strcmp(cmd, "TWORD")) {
        long x, y;
        char w[16];
        odt_u32 n;
        sscanf(args, "%ld %ld", &x, &y);
        n = tdt_word_at(&meas, &lay, 0, x, y, w, sizeof w);
        printf("tword %lu ", (unsigned long)n);
        hex(w, n);
        printf("\n");
    } else if (!strcmp(cmd, "TRENDER")) {
        long l, t, w, h, row, px;
        struct tdt_pens pens = { 1, 0, 3, 2 };
        sscanf(args, "%ld %ld %ld %ld %ld %ld", &l, &t, &w, &h, &row, &px);
        tdt_render(&env_text, &text_ext, &meas, &lay, (void *)0x5000, &pens, l, t, w, h, (odt_u32)row, px);
        printf("trender done\n");
    }
}

/* adt_remap as it was, a pixel and a bit at a time: AREMAPCMP checks the
 * one that replaced it makes every byte of the destination the same */
static void old_remap(const struct adt_BitMap *src, odt_u32 w, odt_u32 h, const odt_u8 *table, struct adt_BitMap *dst)
{
    odt_u32 x, y, p, sd = src->Depth > 8 ? 8 : src->Depth, dd = dst->Depth > 8 ? 8 : dst->Depth;
    if (w > (odt_u32)src->BytesPerRow * 8) w = (odt_u32)src->BytesPerRow * 8;
    if (w > (odt_u32)dst->BytesPerRow * 8) w = (odt_u32)dst->BytesPerRow * 8;
    if (h > src->Rows) h = src->Rows;
    if (h > dst->Rows) h = dst->Rows;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            odt_u32 byte = x >> 3, bit = 0x80 >> (x & 7), pix = 0, out;
            for (p = 0; p < sd; p++)
                if (src->Planes[p] && (src->Planes[p][y * src->BytesPerRow + byte] & bit)) pix |= 1u << p;
            out = table ? table[pix] : pix;
            for (p = 0; p < dd; p++) {
                odt_u8 *b;
                if (!dst->Planes[p]) continue;
                b = &dst->Planes[p][y * dst->BytesPerRow + byte];
                if (out & (1u << p)) *b |= (odt_u8)bit;
                else *b &= (odt_u8)~bit;
            }
        }
    }
}

static unsigned long cmp_seed;
static odt_u32 cmp_rnd(void)
{
    cmp_seed = cmp_seed * 1103515245UL + 12345UL;
    return (odt_u32)(cmp_seed >> 16) & 0x7fff;
}

/* animation pure */
static void run_apure(const char *cmd, char *args)
{
    if (!strcmp(cmd, "AREMAPCMP")) {
        /* seed cases: bitmaps of random sizes, depths, modulos and missing
         * planes, destinations full of other bits, a table or none */
        unsigned long seed, cases, k, differ = 0;
        sscanf(args, "%lu %lu", &seed, &cases);
        cmp_seed = seed;
        for (k = 0; k < cases; k++) {
            odt_u32 w = 1 + cmp_rnd() % (k & 1 ? 700 : 70), h = 1 + cmp_rnd() % 6, sd = cmp_rnd() % 10, dd = 1 + cmp_rnd() % 9;
            odt_u32 sbpr = (((w + 15) >> 4) << 1) + 2 * (cmp_rnd() % 3), dbpr = (((w + 15) >> 4) << 1) + 2 * (cmp_rnd() % 2);
            odt_u32 cw = w + (cmp_rnd() % 4 ? 0 : cmp_rnd() % 40), ch = h + cmp_rnd() % 2, p, i;
            odt_u8 table[256], *sp[8], *a[8], *b[8];
            struct adt_BitMap s, d1, d2;
            int use_table = cmp_rnd() % 5 != 0, same = 1;
            for (i = 0; i < 256; i++) table[i] = (odt_u8)cmp_rnd();
            memset(&s, 0, sizeof s);
            memset(&d1, 0, sizeof d1);
            s.BytesPerRow = (odt_u16)sbpr; s.Rows = (odt_u16)h; s.Depth = (odt_u8)sd;
            d1.BytesPerRow = (odt_u16)dbpr; d1.Rows = (odt_u16)h; d1.Depth = (odt_u8)dd;
            d2 = d1;
            for (p = 0; p < 8; p++) {
                sp[p] = malloc(sbpr * h);
                a[p] = malloc(dbpr * h);
                b[p] = malloc(dbpr * h);
                for (i = 0; i < sbpr * h; i++) sp[p][i] = (odt_u8)cmp_rnd();
                for (i = 0; i < dbpr * h; i++) a[p][i] = b[p][i] = (odt_u8)cmp_rnd();
                s.Planes[p] = cmp_rnd() % 6 ? sp[p] : 0;
                d1.Planes[p] = d2.Planes[p] = 0;
                if (cmp_rnd() % 6) { d1.Planes[p] = a[p]; d2.Planes[p] = b[p]; }
            }
            old_remap(&s, cw, ch, use_table ? table : 0, &d1);
            adt_remap(&s, cw, ch, use_table ? table : 0, &d2);
            for (p = 0; p < 8; p++) {
                if (memcmp(a[p], b[p], dbpr * h)) same = 0;
                free(sp[p]); free(a[p]); free(b[p]);
            }
            if (!same && differ++ < 5)
                printf("aremapcmp case %lu differs: %lux%lu depth %lu to %lu\n", k, (unsigned long)cw, (unsigned long)ch,
                       (unsigned long)sd, (unsigned long)dd);
        }
        printf("aremapcmp %lu cases, %lu differ from the old remap\n", cases, differ);
        return;
    }
    if (!strcmp(cmd, "APLAY")) {
        /* frames inc repeat start ticks dur... */
        struct adt_play p;
        long frames, inc, rep, st, ticks, i, durs[64], nd = 0;
        char *tok = strtok(args, " ");
        frames = atol(tok); inc = atol(strtok(0, " ")); rep = atol(strtok(0, " "));
        st = atol(strtok(0, " ")); ticks = atol(strtok(0, " "));
        while ((tok = strtok(0, " ")) != 0 && nd < 64) durs[nd++] = atol(tok);
        adt_play_init(&p, (odt_u32)frames);
        p.increment = (odt_u32)inc;
        p.repeat = (odt_u8)rep;
        adt_play_start(&p, (odt_u32)st, (odt_u32)durs[st % nd], (odt_u32)st);
        printf("aplay");
        for (i = 0; i < ticks; i++) {
            if (adt_play_tick(&p)) adt_play_shown(&p, (odt_u32)durs[p.frame % nd]);
            printf(" %lu%c", (unsigned long)p.frame, p.state == ADT_PLAYING ? 'p' : 's');
        }
        printf(" t%lu\n", (unsigned long)p.ticks);
    } else if (!strcmp(cmd, "ASND")) {
        unsigned long t, f, per, sps, clk, len;
        sscanf(args, "%lu %lu %lu %lu %lu %lu", &t, &f, &per, &sps, &clk, &len);
        printf("asnd %lu\n", (unsigned long)adt_sound_offset(t, f, per, sps, clk, len));
    } else if (!strcmp(cmd, "AREMAP")) {
        /* w h sdepth ddepth table-hex planes-hex */
        unsigned long w, h, sd, dd, p, y, x;
        char th[1024], ph[1 << 15];
        odt_u8 table[256], planes[1 << 14];
        struct adt_BitMap s, d;
        odt_u32 bpr;
        sscanf(args, "%lu %lu %lu %lu %1023s %32767s", &w, &h, &sd, &dd, th, ph);
        unhex(th, table);
        unhex(ph, planes);
        bpr = ((w + 15) >> 4) << 1;
        memset(&s, 0, sizeof s);
        memset(&d, 0, sizeof d);
        s.BytesPerRow = (odt_u16)(bpr * sd);    /* interleaved, to try the modulo */
        s.Rows = (odt_u16)h;
        s.Depth = (odt_u8)sd;
        for (p = 0; p < sd; p++) s.Planes[p] = planes + p * bpr;
        d.BytesPerRow = (odt_u16)bpr;
        d.Rows = (odt_u16)h;
        d.Depth = (odt_u8)dd;
        for (p = 0; p < dd; p++) { d.Planes[p] = calloc(1, bpr * h); memset(d.Planes[p], 0xa5, bpr * h); }
        adt_remap(&s, w, h, table, &d);
        printf("aremap");
        for (y = 0; y < h; y++) {
            printf(" ");
            for (x = 0; x < w; x++) {
                unsigned v = 0;
                for (p = 0; p < dd; p++)
                    if (d.Planes[p][y * bpr + (x >> 3)] & (0x80 >> (x & 7))) v |= 1u << p;
                printf("%02x", v);
            }
        }
        printf("\n");
        for (p = 0; p < dd; p++) free(d.Planes[p]);
    } else if (!strcmp(cmd, "APANEL")) {
        long l, t, w, h, ys, fr, n, i;
        struct adt_panel p;
        char *tok = strtok(args, " ");
        l = atol(tok); t = atol(strtok(0, " ")); w = atol(strtok(0, " ")); h = atol(strtok(0, " "));
        ys = atol(strtok(0, " ")); fr = atol(strtok(0, " "));
        adt_panel_layout(&p, (odt_s16)l, (odt_s16)t, (odt_s16)w, (odt_s16)h, (odt_u32)ys);
        printf("apanel %d %d %d %d %d %d %d %d %d %d\n", p.x, p.y, p.w, p.h, p.play_x, p.stop_x, p.bw, p.sx, p.sw, p.kw);
        n = atol(strtok(0, " "));
        for (i = 0; i < n; i++) {
            long x = atol(strtok(0, " ")), y = atol(strtok(0, " "));
            odt_u32 f = 999;
            int hit = adt_panel_hit(&p, x, y, (odt_u32)fr, &f);
            printf("hit %ld %ld %d %lu\n", x, y, hit, hit == ADT_HIT_SLIDER ? (unsigned long)f : 0UL);
        }
        for (i = 0; i < fr && i < 4; i++) printf("knob %ld %ld\n", i, (long)adt_knob_x(&p, (odt_u32)i, (odt_u32)fr));
        adt_panel_draw(&env_anim, &anim_ext, (void *)0x5000, &p, ADT_PLAYING, 1, (odt_u32)fr, 0, 3, 1);
        adt_panel_draw(&env_anim, &anim_ext, (void *)0x5000, &p, ADT_STOPPED, 0, (odt_u32)fr, 0, 3, 1);
    }
}

/* ---- class scenarios ---------------------------------------------------------- */

static void layout_obj(const struct odt_cenv *env, void *o, odt_u32 method)
{
    struct odt_gpLayout g;
    g.MethodID = method;
    g.gpl_GInfo = &ginfo;
    g.gpl_Initial = 1;
    printf("layout -> %lu\n", (unsigned long)m_do_method(env, o, &g));
}

static void render_obj(const struct odt_cenv *env, void *o)
{
    struct odt_gpRender r;
    r.MethodID = ODT_GM_RENDER;
    r.gpr_GInfo = &ginfo;
    r.gpr_RPort = (void *)0x5000;
    r.gpr_Redraw = ODT_GREDRAW_REDRAW;
    printf("render -> %lu\n", (unsigned long)m_do_method(env, o, &r));
}

/* TCLASS wrap hex-text: a text.datatype object through its life */
static void run_tclass(char *args)
{
    long base = outstanding;
    char *w = strtok(args, " "), *h = strtok(0, " ");
    int n = unhex(h, (odt_u8 *)textbuf);
    struct odt_TagItem tags[8];
    void *o;
    odt_u32 v;
    struct odt_dtSelect sel;
    struct odt_dtWrite wr;
    odt_u32 copy = ODT_DTM_COPY;
    tags[0].ti_Tag = ODT_GA_Left;        tags[0].ti_Data = 5;
    tags[1].ti_Tag = ODT_GA_Top;         tags[1].ti_Data = 7;
    tags[2].ti_Tag = ODT_GA_Width;       tags[2].ti_Data = 100;
    tags[3].ti_Tag = ODT_GA_Height;      tags[3].ti_Data = 40;
    tags[4].ti_Tag = TDT_TDTA_Buffer;    tags[4].ti_Data = (odt_u32)textbuf;
    tags[5].ti_Tag = TDT_TDTA_BufferLen; tags[5].ti_Data = (odt_u32)n;
    tags[6].ti_Tag = TDT_TDTA_WordWrap;  tags[6].ti_Data = (odt_u32)atol(w);
    tags[7].ti_Tag = ODT_TAG_DONE;
    o = mnew(&env_text, &c_text, tags);
    printf("tclass new %d\n", o != 0);
    textbuf[0] ^= 1;                    /* ours is a copy */
    v = mget(&env_text, o, TDT_TDTA_BufferLen);
    printf("buflen %lu wrap %lu\n", (unsigned long)v, (unsigned long)mget(&env_text, o, TDT_TDTA_WordWrap));
    layout_obj(&env_text, o, ODT_DTM_PROCLAYOUT);
    {
        struct odt_DTSpecialInfo *si = dtc_si(o);
        printf("si %ld %ld %ld %ld %ld %ld\n", (long)si->si_VisVert, (long)si->si_TotVert, (long)si->si_VertUnit,
               (long)si->si_VisHoriz, (long)si->si_TotHoriz, (long)si->si_HorizUnit);
        si->si_TopVert = 1;
    }
    render_obj(&env_text, o);
    sel.MethodID = ODT_DTM_SELECT;
    sel.dts_GInfo = &ginfo;
    sel.dts_Select.MinX = 7; sel.dts_Select.MinY = 2; sel.dts_Select.MaxX = 30; sel.dts_Select.MaxY = 12;
    printf("select -> %lu\n", (unsigned long)m_do_method(&env_text, o, &sel));
    printf("copy -> %lu\n", (unsigned long)m_do_method(&env_text, o, &copy));
    wr.MethodID = ODT_DTM_WRITE;
    wr.dtw_GInfo = 0;
    wr.dtw_FileHandle = 99;
    wr.dtw_Mode = ODT_DTWM_RAW;
    wr.dtw_AttrList = 0;
    printf("write raw -> %lu\n", (unsigned long)m_do_method(&env_text, o, &wr));
    wr.dtw_Mode = ODT_DTWM_IFF;
    printf("write iff -> %lu\n", (unsigned long)m_do_method(&env_text, o, &wr));
    {
        /* a double click on the first row's second segment */
        struct odt_InputEvent ie;
        struct odt_gpInput gp;
        memset(&ie, 0, sizeof ie);
        ie.ie_Class = ODT_IECLASS_RAWMOUSE;
        ie.ie_Code = ODT_SELECTDOWN;
        ie.ie_Secs = 10;
        ie.ie_Micro = 0;
        gp.MethodID = ODT_GM_GOACTIVE;
        gp.gpi_GInfo = &ginfo;
        gp.gpi_IEvent = &ie;
        gp.gpi_Termination = 0;
        gp.gpi_Mouse.x = 3;
        gp.gpi_Mouse.y = 0;
        gp.gpi_TabletData = 0;
        dtc_si(o)->si_TopVert = 0;
        m_do_method(&env_text, o, &gp);
        ie.ie_Micro = 200000;
        m_do_method(&env_text, o, &gp);
        printf("wordselect '%s'\n", (char *)mget(&env_text, o, TDT_TDTA_WordSelect));
    }
    {
        struct odt_TagItem t2[2];
        t2[0].ti_Tag = TDT_TDTA_Buffer; t2[0].ti_Data = (odt_u32)"x y";
        t2[1].ti_Tag = ODT_TAG_DONE;
        odt_u32 r = mset(&env_text, o, t2);
        printf("set -> %lu buflen %lu\n", (unsigned long)r, (unsigned long)mget(&env_text, o, TDT_TDTA_BufferLen));
        layout_obj(&env_text, o, ODT_DTM_ASYNCLAYOUT);
    }
    mdispose(&env_text, o);
    printf("allocs %ld sem %d\n", outstanding - base, sem_depth);
}

/* ACLASS kind hex: ascii.datatype reading FILE (1), MEMORY (2), CLIPBOARD (3), RAM (4) */
static void run_aclass(char *args)
{
    long base = outstanding;
    int kind = atoi(strtok(args, " "));
    char *h = strtok(0, " ");
    int n = h ? unhex(h, file_data) : 0;
    struct odt_TagItem tags[6];
    void *o, *iff = 0;
    tags[0].ti_Tag = ODT_GA_Width;  tags[0].ti_Data = 60;
    tags[1].ti_Tag = ODT_GA_Height; tags[1].ti_Data = 30;
    tags[2].ti_Tag = ODT_TAG_DONE;  tags[3].ti_Tag = ODT_TAG_DONE; tags[4].ti_Tag = ODT_TAG_DONE;
    file_len = (odt_u32)n;
    file_pos = 5;
    if (kind == 1) {
        tags[2].ti_Tag = ODT_DTA_SourceType; tags[2].ti_Data = ODT_DTST_FILE;
        tags[3].ti_Tag = ODT_DTA_Handle;     tags[3].ti_Data = 0x1234;
    } else if (kind == 2) {
        tags[2].ti_Tag = ODT_DTA_SourceType;    tags[2].ti_Data = ODT_DTST_MEMORY;
        tags[3].ti_Tag = ODT_DTA_SourceAddress; tags[3].ti_Data = (odt_u32)file_data;
        tags[4].ti_Tag = ODT_DTA_SourceSize;    tags[4].ti_Data = (odt_u32)n;
        tags[5].ti_Tag = ODT_TAG_DONE;
    } else if (kind == 3) {
        iff = e_iff_open_mem_(file_data, (odt_u32)n);
        tags[2].ti_Tag = ODT_DTA_SourceType; tags[2].ti_Data = ODT_DTST_CLIPBOARD;
        tags[3].ti_Tag = ODT_DTA_Handle;     tags[3].ti_Data = (odt_u32)iff;
    } else {
        tags[2].ti_Tag = ODT_DTA_SourceType; tags[2].ti_Data = ODT_DTST_RAM;
    }
    ioerr = 0;
    o = mnew(&env_text, &c_ascii, tags);
    printf("aclass new %d ioerr %ld\n", o != 0, (long)ioerr);
    if (o) {
        odt_u32 len = mget(&env_text, o, TDT_TDTA_BufferLen);
        printf("buf %lu ", (unsigned long)len);
        hex((const void *)mget(&env_text, o, TDT_TDTA_Buffer), len);
        printf("\n");
        layout_obj(&env_text, o, ODT_DTM_PROCLAYOUT);
        mdispose(&env_text, o);
    }
    free(iff);
    printf("allocs %ld sem %d\n", outstanding - base, sem_depth);
}

/* NCLASS path ops...: anim.datatype under animation.datatype, from memory */
static void run_nclass(char *args)
{
    long base = outstanding;
    char *path = strtok(args, " "), *tok;
    odt_u32 len;
    odt_u8 *data = read_file(path, &len);
    struct odt_TagItem tags[8];
    void *o;
    static odt_s8 snd[4000];
    tags[0].ti_Tag = ODT_GA_Left;           tags[0].ti_Data = 2;
    tags[1].ti_Tag = ODT_GA_Top;            tags[1].ti_Data = 3;
    tags[2].ti_Tag = ODT_GA_Width;          tags[2].ti_Data = 120;
    tags[3].ti_Tag = ODT_GA_Height;         tags[3].ti_Data = 50;
    tags[4].ti_Tag = ODT_DTA_SourceType;    tags[4].ti_Data = ODT_DTST_MEMORY;
    tags[5].ti_Tag = ODT_DTA_SourceAddress; tags[5].ti_Data = (odt_u32)data;
    tags[6].ti_Tag = ODT_DTA_SourceSize;    tags[6].ti_Data = len;
    tags[7].ti_Tag = ODT_TAG_DONE;
    ioerr = 0;
    ginfo.gi_Screen = (void *)0x6000;
    sample_base = snd;
    o = mnew(&env_anim, &c_anim, tags);
    printf("nclass new %d ioerr %ld\n", o != 0, (long)ioerr);
    if (!o) { free(data); printf("allocs %ld sem %d\n", outstanding - base, sem_depth); return; }
    printf("attrs %lu %lu %lu %lu %lu %08lx %lu\n", (unsigned long)mget(&env_anim, o, ADT_ADTA_Width),
           (unsigned long)mget(&env_anim, o, ADT_ADTA_Height), (unsigned long)mget(&env_anim, o, ADT_ADTA_Depth),
           (unsigned long)mget(&env_anim, o, ADT_ADTA_Frames), (unsigned long)mget(&env_anim, o, ADT_ADTA_FramesPerSecond),
           (unsigned long)mget(&env_anim, o, ADT_ADTA_ModeID), (unsigned long)mget(&env_anim, o, ADT_ADTA_NumColors));
    {
        const struct adt_ColorRegister *cr = (const void *)mget(&env_anim, o, ADT_ADTA_ColorRegisters);
        const odt_u32 *cregs = (const void *)mget(&env_anim, o, ADT_ADTA_CRegs);
        printf("cr ");
        hex(cr, mget(&env_anim, o, ADT_ADTA_NumColors) * 3);
        printf(" %08lx\n", cregs ? (unsigned long)cregs[1] : 0UL);
    }
    while ((tok = strtok(0, " ")) != 0) {
        if (!strcmp(tok, "layout")) layout_obj(&env_anim, o, ODT_DTM_PROCLAYOUT);
        else if (!strcmp(tok, "render")) render_obj(&env_anim, o);
        else if (!strcmp(tok, "tick")) { printf("tick\n"); adt_tick(&env_anim, &c_animation.ic, o); }
        else if (!strncmp(tok, "start", 5) || !strncmp(tok, "locate", 6)) {
            struct adt_Start s;
            s.MethodID = tok[0] == 's' ? ADT_ADTM_START : ADT_ADTM_LOCATE;
            s.asa_Frame = (odt_u32)atol(tok + (tok[0] == 's' ? 5 : 6));
            printf("%s -> %lu\n", tok, (unsigned long)m_do_method(&env_anim, o, &s));
        } else if (!strcmp(tok, "pause") || !strcmp(tok, "stop")) {
            odt_u32 m = tok[0] == 'p' ? ADT_ADTM_PAUSE : ADT_ADTM_STOP;
            printf("%s -> %lu\n", tok, (unsigned long)m_do_method(&env_anim, o, &m));
        } else if (!strncmp(tok, "preload", 7) || !strncmp(tok, "repeat", 6) || !strcmp(tok, "sample") ||
                   !strcmp(tok, "noremap") || !strcmp(tok, "nopanel")) {
            struct odt_TagItem t[5];
            t[1].ti_Tag = ODT_TAG_DONE;
            if (tok[0] == 'p' && tok[1] == 'r') { t[0].ti_Tag = ADT_ADTA_PreloadFrameCount; t[0].ti_Data = (odt_u32)atol(tok + 7); }
            else if (tok[0] == 'r') { t[0].ti_Tag = ODT_DTA_Repeat; t[0].ti_Data = (odt_u32)atol(tok + 6); }
            else if (tok[0] == 'n' && tok[2] == 'r') { t[0].ti_Tag = ADT_ADTA_Remap; t[0].ti_Data = 0; }
            else if (tok[0] == 'n') { t[0].ti_Tag = ODT_DTA_ControlPanel; t[0].ti_Data = 0; }
            else {
                t[0].ti_Tag = ADT_ADTA_Sample;       t[0].ti_Data = (odt_u32)snd;
                t[1].ti_Tag = ADT_ADTA_SampleLength; t[1].ti_Data = sizeof snd;
                t[2].ti_Tag = ADT_ADTA_Period;       t[2].ti_Data = 1000;
                t[3].ti_Tag = ODT_TAG_DONE;
            }
            printf("%s -> %lu\n", tok, (unsigned long)mset(&env_anim, o, t));
        } else if (!strncmp(tok, "click", 5)) {
            /* clickX,Y: GM_HITTEST then GM_GOACTIVE at gadget-relative X,Y */
            struct odt_gpHitTest ht;
            struct odt_gpInput gp;
            struct odt_InputEvent ie;
            long x, y;
            sscanf(tok + 5, "%ld,%ld", &x, &y);
            ht.MethodID = ODT_GM_HITTEST;
            ht.gpht_GInfo = &ginfo;
            ht.gpht_Mouse.x = (odt_s16)x;
            ht.gpht_Mouse.y = (odt_s16)y;
            printf("hittest %ld,%ld -> %lu\n", x, y, (unsigned long)m_do_method(&env_anim, o, &ht));
            memset(&ie, 0, sizeof ie);
            ie.ie_Class = ODT_IECLASS_RAWMOUSE;
            ie.ie_Code = ODT_SELECTDOWN;
            gp.MethodID = ODT_GM_GOACTIVE;
            gp.gpi_GInfo = &ginfo;
            gp.gpi_IEvent = &ie;
            gp.gpi_Termination = 0;
            gp.gpi_Mouse.x = (odt_s16)x;
            gp.gpi_Mouse.y = (odt_s16)y;
            gp.gpi_TabletData = 0;
            printf("goactive -> %lu\n", (unsigned long)m_do_method(&env_anim, o, &gp));
        } else if (!strcmp(tok, "frame")) {
            printf("frame %lu\n", (unsigned long)mget(&env_anim, o, ADT_ADTA_Frame));
        }
    }
    mdispose(&env_anim, o);
    free(data);
    printf("allocs %ld sem %d\n", outstanding - base, sem_depth);
}

/* animation.datatype logs the frame loads it asks for: a spy between it and anim */
static odt_u32 spy_anim(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg, r;
    r = anm_dispatch(env, cl, obj, msg);
    if (id == ADT_ADTM_LOADFRAME || id == ADT_ADTM_LOADNEWFORMATFRAME)
        printf("load %lu %lu dur %lu -> %lu\n", (unsigned long)(id & 0xff), (unsigned long)((struct adt_Frame *)msg)->alf_Frame,
               (unsigned long)((struct adt_Frame *)msg)->alf_Duration, (unsigned long)r);
    else if (id == ADT_ADTM_UNLOADFRAME || id == ADT_ADTM_UNLOADNEWFORMATFRAME)
        printf("unload %lu %lu\n", (unsigned long)(id & 0xff), (unsigned long)((struct adt_Frame *)msg)->alf_Frame);
    return r;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r");
    setvbuf(stdout, 0, _IOFBF, 1 << 16);
    if (argc < 2 || !f) return 2;
    env_init(&env_text, &text_ext);
    env_init(&env_anim, &anim_ext);
    dtc_list_init(&lay.lines);
    mk(&c_dt, 0, dt_dispatch, GADGET_SIZE + sizeof(struct mdt));
    mk(&c_text, &c_dt, tdt_dispatch, (odt_u16)tdt_inst_size());
    mk(&c_ascii, &c_text, asc_dispatch, (odt_u16)asc_inst_size());
    mk(&c_animation, &c_dt, adt_dispatch, (odt_u16)adt_inst_size());
    mk(&c_anim, &c_animation, spy_anim, (odt_u16)anm_inst_size());
    while (fgets(line, sizeof line, f)) {
        char *cmd, *args;
        line[strcspn(line, "\n")] = 0;
        cmd = line;
        args = strchr(line, ' ');
        if (args) *args++ = 0;
        else args = line + strlen(line);
        printf("> %s\n", cmd);
        if (!strcmp(cmd, "ANIM")) run_anim(args);
        else if (!strcmp(cmd, "ANIMBACK")) run_animback(args);
        else if (cmd[0] == 'T' && strcmp(cmd, "TCLASS")) run_text(cmd, args);
        else if (!strcmp(cmd, "TCLASS")) run_tclass(args);
        else if (!strcmp(cmd, "ACLASS")) run_aclass(args);
        else if (!strcmp(cmd, "NCLASS")) run_nclass(args);
        else if (cmd[0] == 'A') run_apure(cmd, args);
    }
    tdt_free_layout(&env_text, &lay);
    printf("end allocs %ld\n", outstanding);
    fclose(f);
    (void)bin;
    return 0;
}
