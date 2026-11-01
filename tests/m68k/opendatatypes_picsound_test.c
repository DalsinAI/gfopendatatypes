/* GFOpenDatatypes' picture and sound classes under qemu-m68k: picture.datatype,
 * ilbm.datatype, sound.datatype and 8svx.datatype (the lean C++ cores in
 * libraries/opendatatypes/classes/datatypes/), hosted on a mock BOOPSI with
 * a stand-in datatypesclass (attributes only, as the classes see OS 3.2's),
 * a mock iffparse over files python3 wrote, mock graphics (planar bitmaps,
 * a screen whose pens are shared as ObtainBestPen shares them) and a mock
 * audio player that logs the plan it is given.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * It runs a script opendatatypes_picsound_ref.py writes, one command a
 * line, and prints a log; test_opendatatypes_picsound.sh compares it with
 * the log python's own models expect. The words:
 *   ilbm FILE SCREEN REMAP DESTMODE SW SH KEEP   load, read, lay out, draw, write, reload
 *   pixw FILE                                    PDTM_WRITEPIXELARRAY then READPIXELARRAY
 *   svx FILE PANEL                               load an 8SVX, play, draw, click, write, reload
 *   snd LEN STEREO CYCLES PAN TYPE               a sound made in memory, played
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libraries/opendatatypes/classes/datatypes/ilbm/ilbm_core.h"
#include "../../libraries/opendatatypes/classes/datatypes/8svx/svx_core.h"

static long live;
static long ioerr;

/* ---- memory ------------------------------------------------------------- */

static void *m_alloc(const struct odt_cenv *e, odt_u32 n) { (void)e; live++; return calloc(1, n ? n : 1); }
static void m_free(const struct odt_cenv *e, void *p) { (void)e; if (p) { live--; free(p); } }
static void m_set_io_err(const struct odt_cenv *e, odt_s32 c) { (void)e; ioerr = c; }

/* ---- BOOPSI ------------------------------------------------------------- */

static odt_u32 call_class(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    const odt_dispatch_fn *fn = cl->cl_Dispatcher.h_Data;
    return (*fn)(env, cl, obj, msg);
}
static odt_u32 m_do_method(const struct odt_cenv *env, void *obj, void *msg)
{ return obj ? call_class(env, ODT_OCLASS(obj), obj, msg) : 0; }
static odt_u32 m_do_super(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{ return call_class(env, cl->cl_Super, obj, msg); }
static odt_u32 m_coerce(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{ return call_class(env, cl, obj, msg); }

/* The stand-in datatypesclass: a gadget, and what it is told */
struct fdt {
    odt_u32 srctype;
    void *handle;
    odt_u32 top_h, top_v, tot_h, tot_v, unit_h, unit_v, nom_h, nom_v;
    odt_u32 panel, immediate, repeat;
    char *name, *author, *anno, *copy;
};
#define FDT_SIZE ((odt_u16)(sizeof(struct odt_Gadget) + sizeof(struct fdt)))
static struct fdt *FDT(void *o) { return (struct fdt *)((odt_u8 *)o + sizeof(struct odt_Gadget)); }

static char *dupstr(const char *s)
{
    char *d;
    if (!s) return NULL;
    d = malloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

static odt_u32 fdt_set(void *o, struct odt_TagItem *tags)
{
    struct fdt *f = FDT(o);
    struct odt_Gadget *g = o;
    struct odt_TagItem *t;
    odt_u32 n = 0;
    while ((t = dtc_next_tag(&tags)) != NULL) {
        odt_u32 d = t->ti_Data;
        switch (t->ti_Tag) {
        case ODT_DTA_SourceType: f->srctype = d; break;
        case ODT_DTA_Handle: f->handle = (void *)d; break;
        case ODT_DTA_TopHoriz: f->top_h = d; break;
        case ODT_DTA_TopVert: f->top_v = d; break;
        case ODT_DTA_TotalHoriz: f->tot_h = d; break;
        case ODT_DTA_TotalVert: f->tot_v = d; break;
        case ODT_DTA_HorizUnit: f->unit_h = d; break;
        case ODT_DTA_VertUnit: f->unit_v = d; break;
        case ODT_DTA_NominalHoriz: f->nom_h = d; break;
        case ODT_DTA_NominalVert: f->nom_v = d; break;
        case ODT_DTA_ControlPanel: f->panel = d; break;
        case ODT_DTA_Immediate: f->immediate = d; break;
        case ODT_DTA_Repeat: f->repeat = d; break;
        case ODT_DTA_ObjName: free(f->name); f->name = dupstr((char *)d); break;
        case ODT_DTA_ObjAuthor: free(f->author); f->author = dupstr((char *)d); break;
        case ODT_DTA_ObjAnnotation: free(f->anno); f->anno = dupstr((char *)d); break;
        case ODT_DTA_ObjCopyright: free(f->copy); f->copy = dupstr((char *)d); break;
        case ODT_GA_Left: g->LeftEdge = (odt_s16)d; break;
        case ODT_GA_Top: g->TopEdge = (odt_s16)d; break;
        case ODT_GA_Width: g->Width = (odt_s16)d; break;
        case ODT_GA_Height: g->Height = (odt_s16)d; break;
        default: continue;
        }
        n++;
    }
    return n;
}

static odt_u32 fdt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 m = *(odt_u32 *)msg;
    (void)env;
    switch (m) {
    case ODT_OM_NEW: {
        struct odt_IClass *true_cl = obj;
        odt_u32 size = true_cl->cl_InstOffset + true_cl->cl_InstSize;
        struct odt_Object *h = calloc(1, sizeof *h + size);
        void *o = h + 1;
        (void)cl;
        h->o_Class = true_cl;
        fdt_set(o, ((struct odt_opSet *)msg)->ops_AttrList);
        return (odt_u32)o;
    }
    case ODT_OM_DISPOSE: {
        struct fdt *f = FDT(obj);
        free(f->name); free(f->author); free(f->anno); free(f->copy);
        free((struct odt_Object *)obj - 1);
        return 0;
    }
    case ODT_OM_SET:
    case ODT_OM_UPDATE:
        return fdt_set(obj, ((struct odt_opSet *)msg)->ops_AttrList);
    case ODT_OM_GET: {
        struct odt_opGet *g = msg;
        struct fdt *f = FDT(obj);
        odt_u32 *s = g->opg_Storage;
        switch (g->opg_AttrID) {
        case ODT_DTA_SourceType: *s = f->srctype; return 1;
        case ODT_DTA_Handle: *s = (odt_u32)f->handle; return 1;
        case ODT_DTA_TopHoriz: *s = f->top_h; return 1;
        case ODT_DTA_TopVert: *s = f->top_v; return 1;
        case ODT_DTA_TotalHoriz: *s = f->tot_h; return 1;
        case ODT_DTA_TotalVert: *s = f->tot_v; return 1;
        case ODT_DTA_ControlPanel: *s = f->panel; return 1;
        case ODT_DTA_Immediate: *s = f->immediate; return 1;
        case ODT_DTA_Repeat: *s = f->repeat; return 1;
        case ODT_DTA_ObjName: *s = (odt_u32)f->name; return 1;
        }
        return 0;
    }
    case ODT_DTM_PROCLAYOUT:
    case ODT_DTM_ASYNCLAYOUT:
    case ODT_GM_LAYOUT:
        return 1;
    }
    return 0;
}

static struct odt_IClass cl_dt, cl_pic, cl_ilbm, cl_snd, cl_svx;

static odt_dispatch_fn fns[8];
static int nfns;
static void make_class(struct odt_IClass *cl, struct odt_IClass *super, odt_dispatch_fn fn, odt_u32 size)
{
    fns[nfns] = fn;
    cl->cl_Dispatcher.h_Data = &fns[nfns++];
    cl->cl_Super = super;
    cl->cl_InstOffset = super ? (odt_u16)(super->cl_InstOffset + super->cl_InstSize) : 0;
    cl->cl_InstSize = (odt_u16)size;
}

/* ---- iffparse over memory ------------------------------------------------ */

struct miff {
    const odt_u8 *data;
    odt_u32 len;
    odt_u32 props[32][2], nprops;
    odt_u32 stops[8][2], nstops;
    struct { odt_u32 type, id; const odt_u8 *at; odt_s32 size; } found[32];
    odt_u32 nfound;
    odt_u32 cur_id, cur_type, pos;
    const odt_u8 *cur;
    odt_s32 cur_size;
};

static odt_u32 be32(const odt_u8 *p) { return ((odt_u32)p[0] << 24) | ((odt_u32)p[1] << 16) | ((odt_u32)p[2] << 8) | p[3]; }

static odt_s32 i_prop(const struct odt_cenv *e, void *h, odt_u32 type, odt_u32 id)
{ struct miff *f = h; (void)e; f->props[f->nprops][0] = type; f->props[f->nprops++][1] = id; return 0; }
static odt_s32 i_stop(const struct odt_cenv *e, void *h, odt_u32 type, odt_u32 id)
{ struct miff *f = h; (void)e; f->stops[f->nstops][0] = type; f->stops[f->nstops++][1] = id; return 0; }

static odt_s32 i_parse(const struct odt_cenv *e, void *h, odt_s32 mode)
{
    struct miff *f = h;
    odt_u32 o = 12, type, end, i;
    (void)e; (void)mode;
    if (f->len < 12 || be32(f->data) != ODT_ID('F', 'O', 'R', 'M')) return -10;   /* IFFERR_NOTIFF */
    type = be32(f->data + 8);
    end = 8 + be32(f->data + 4);
    if (end > f->len) end = f->len;
    while (o + 8 <= end) {
        odt_u32 id = be32(f->data + o), size = be32(f->data + o + 4);
        odt_u32 avail = end - o - 8;
        if (size > avail) size = avail;
        for (i = 0; i < f->nstops; i++)
            if (f->stops[i][0] == type && f->stops[i][1] == id) {
                f->cur_id = id; f->cur_type = type; f->cur = f->data + o + 8; f->cur_size = (odt_s32)size; f->pos = 0;
                return 0;
            }
        for (i = 0; i < f->nprops; i++)
            if (f->props[i][0] == type && f->props[i][1] == id) {
                f->found[f->nfound].type = type; f->found[f->nfound].id = id;
                f->found[f->nfound].at = f->data + o + 8; f->found[f->nfound++].size = (odt_s32)size;
            }
        o += 8 + size + (size & 1);
    }
    return -1;                                                  /* IFFERR_EOF */
}

static const void *i_find(const struct odt_cenv *e, void *h, odt_u32 type, odt_u32 id, odt_s32 *size)
{
    struct miff *f = h;
    odt_u32 i;
    (void)e;
    for (i = f->nfound; i-- > 0;)
        if (f->found[i].type == type && f->found[i].id == id) {
            *size = f->found[i].size;
            return f->found[i].at;
        }
    return NULL;
}

static odt_u32 i_current(const struct odt_cenv *e, void *h, odt_u32 *type, odt_s32 *size)
{
    struct miff *f = h;
    (void)e;
    if (!f->cur) return 0;
    *type = f->cur_type;
    *size = f->cur_size;
    return f->cur_id;
}

static odt_s32 i_read(const struct odt_cenv *e, void *h, void *buf, odt_s32 n)
{
    struct miff *f = h;
    odt_s32 k = f->cur_size - (odt_s32)f->pos;
    (void)e;
    if (n < k) k = n;
    memcpy(buf, f->cur + f->pos, (size_t)k);
    f->pos += (odt_u32)k;
    return k;
}

/* ---- DOS writes to memory ---------------------------------------------- */

static odt_u8 *wbuf;
static odt_u32 wlen, wcap;
static odt_s32 m_write(const struct odt_cenv *e, odt_u32 fh, const void *buf, odt_s32 n)
{
    (void)e; (void)fh;
    if (wlen + (odt_u32)n > wcap) { wcap = (wlen + (odt_u32)n) * 2; wbuf = realloc(wbuf, wcap); }
    memcpy(wbuf + wlen, buf, (size_t)n);
    wlen += (odt_u32)n;
    return n;
}

/* ---- graphics ------------------------------------------------------------ */

struct fscreen { int depth; int npens; odt_u8 rgb[256][3]; int share[256]; int fixed[256]; struct odt_BitMap bm; };
static struct fscreen scr;

static void *x_alloc_chip(const struct odt_cenv *e, odt_u32 n) { return m_alloc(e, n); }

static struct odt_BitMap *x_alloc_bitmap(const struct odt_cenv *e, odt_u32 w, odt_u32 h, odt_u32 d, struct odt_BitMap *fr)
{
    struct odt_BitMap *bm = calloc(1, sizeof *bm);
    odt_u32 i;
    (void)e; (void)fr;
    live++;
    bm->Rows = (odt_u16)h;
    bm->Depth = (odt_u8)d;
    if (d > 8) {
        bm->BytesPerRow = (odt_u16)(w * 4);
        bm->Planes[0] = calloc(1, w * h * 4);
    } else {
        bm->BytesPerRow = (odt_u16)(((w + 15) >> 4) << 1);
        for (i = 0; i < d; i++) bm->Planes[i] = calloc(1, (size_t)bm->BytesPerRow * h);
    }
    return bm;
}
static void x_free_bitmap(const struct odt_cenv *e, struct odt_BitMap *bm)
{
    int i;
    (void)e;
    if (!bm) return;
    live--;
    for (i = 0; i < 8; i++) free(bm->Planes[i]);
    free(bm);
}
static void x_write_lut8(const struct odt_cenv *e, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                         odt_u32 mod)
{
    odt_u32 x, y, d;
    (void)e;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            for (d = 0; d < bm->Depth; d++)
                if (px[y * mod + x] & (1u << d))
                    bm->Planes[d][y * bm->BytesPerRow + x / 8] |= (odt_u8)(0x80 >> (x % 8));
}
static void x_write_argb(const struct odt_cenv *e, struct odt_BitMap *bm, const odt_u8 *px, odt_u32 w, odt_u32 h,
                         odt_u32 mod)
{
    odt_u32 y;
    (void)e;
    for (y = 0; y < h; y++) memcpy(bm->Planes[0] + y * w * 4, px + y * mod, w * 4);
}
static odt_u32 x_screen_depth(const struct odt_cenv *e, void *s) { (void)e; return (odt_u32)((struct fscreen *)s)->depth; }
static void *x_screen_cm(const struct odt_cenv *e, void *s) { (void)e; return s; }
static struct odt_BitMap *x_screen_bm(const struct odt_cenv *e, void *s) { (void)e; return &((struct fscreen *)s)->bm; }

static int in_use(int p) { return scr.fixed[p] || scr.share[p] > 0; }
static odt_s32 x_obtain(const struct odt_cenv *e, void *cm, odt_u32 r, odt_u32 g, odt_u32 b, odt_s32 prec)
{
    int p, best = -1;
    long bd = 0x7fffffff;
    odt_u8 c[3] = { (odt_u8)(r >> 24), (odt_u8)(g >> 24), (odt_u8)(b >> 24) };
    (void)e; (void)cm; (void)prec;
    if (!scr.npens) return -1;
    for (p = 0; p < scr.npens; p++)
        if (in_use(p) && !memcmp(scr.rgb[p], c, 3)) { scr.share[p]++; return p; }
    for (p = 0; p < scr.npens; p++)
        if (!in_use(p)) { memcpy(scr.rgb[p], c, 3); scr.share[p] = 1; return p; }
    for (p = 0; p < scr.npens; p++) {
        long dr = scr.rgb[p][0] - c[0], dg = scr.rgb[p][1] - c[1], db = scr.rgb[p][2] - c[2];
        long d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = p; }
    }
    scr.share[best]++;
    return best;
}
static void x_release(const struct odt_cenv *e, void *cm, odt_u32 pen) { (void)e; (void)cm; if (!scr.fixed[pen]) scr.share[pen]--; }
static void x_get_rgb(const struct odt_cenv *e, void *cm, odt_u32 pen, odt_u32 rgb[3])
{
    int k;
    (void)e; (void)cm;
    for (k = 0; k < 3; k++) rgb[k] = scr.rgb[pen][k] * 0x01010101UL;
}
static void x_blit(const struct odt_cenv *e, void *rp, struct odt_BitMap *bm, const odt_u8 *mask, odt_s32 sx,
                   odt_s32 sy, odt_s32 x, odt_s32 y, odt_s32 w, odt_s32 h)
{
    (void)e; (void)rp; (void)bm;
    printf("blit %ld %ld %ld %ld %ld %ld %d\n", (long)sx, (long)sy, (long)x, (long)y, (long)w, (long)h, mask != NULL);
}
static void x_fill(const struct odt_cenv *e, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{ (void)e; (void)rp; printf("fill %lu %ld %ld %ld %ld\n", (unsigned long)pen, (long)x0, (long)y0, (long)x1, (long)y1); }
static void x_line(const struct odt_cenv *e, void *rp, odt_u32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{ (void)e; (void)rp; printf("line %lu %ld %ld %ld %ld\n", (unsigned long)pen, (long)x0, (long)y0, (long)x1, (long)y1); }
static int rp_dummy;
static void *m_obtain_rp(const struct odt_cenv *e, struct odt_GadgetInfo *gi) { (void)e; (void)gi; printf("obtainrp\n"); return &rp_dummy; }
static void m_release_rp(const struct odt_cenv *e, void *rp) { (void)e; (void)rp; printf("releaserp\n"); }

/* ---- audio ------------------------------------------------------------- */

static int x_play(const struct odt_cenv *e, void **player, const struct dtc_audio_plan *pl)
{
    unsigned i, k;
    (void)e;
    *player = (void *)pl;
    printf("play alloc=");
    for (i = 0; i < pl->nalloc; i++) printf(i ? ",%u" : "%u", pl->alloc[i]);
    printf(" repeat=%u sig=%08lx\n", pl->repeat, (unsigned long)pl->signal_mask);
    for (i = 0; i < pl->nseg; i++) {
        const struct dtc_audio_seg *s = &pl->seg[i];
        unsigned long sum = 0;
        for (k = 0; k < s->length; k++) sum += (unsigned long)(odt_u8)s->data[k] * (k % 7 + 1);
        printf("seg r=%u v=%u p=%u c=%u len=%lu sum=%08lx\n", s->roles, s->volume, s->period, s->cycles,
               (unsigned long)s->length, sum & 0xffffffffUL);
    }
    return 1;
}
static void x_stop(const struct odt_cenv *e, void **player) { (void)e; printf("stop%s\n", *player ? "" : " none"); *player = NULL; }

static const struct dtc_ext ext = {
    x_alloc_chip, x_alloc_bitmap, x_free_bitmap, x_write_lut8, x_write_argb, x_screen_depth, x_screen_cm,
    x_screen_bm, x_obtain, x_release, x_get_rgb, x_blit, x_fill, x_line, x_play, x_stop, 3546895UL,
};

static struct odt_cenv env;

static void env_init(void)
{
    env.do_method = m_do_method;
    env.do_super = m_do_super;
    env.coerce = m_coerce;
    env.alloc = m_alloc;
    env.free = m_free;
    env.set_io_err = m_set_io_err;
    env.write = m_write;
    env.iff_prop = i_prop;
    env.iff_stop = i_stop;
    env.iff_parse = i_parse;
    env.iff_find_prop = i_find;
    env.iff_current = i_current;
    env.iff_read_chunk = i_read;
    env.obtain_rp = m_obtain_rp;
    env.release_rp = m_release_rp;
    env.class_ext = (void *)&ext;
}

/* ---- helpers ------------------------------------------------------------ */

static odt_u8 *read_file(const char *path, odt_u32 *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    odt_u8 *b;
    if (!f) { printf("no file %s\n", path); exit(20); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(20);
    fclose(f);
    *len = (odt_u32)n;
    return b;
}

static void hexrow(const char *tag, const odt_u8 *p, odt_u32 n)
{
    odt_u32 i;
    printf("%s ", tag);
    for (i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

static odt_u32 get(void *o, odt_u32 attr)
{
    odt_u32 v = 0;
    dtc_get(&env, o, attr, &v);
    return v;
}

static void *new_obj(struct odt_IClass *cl, odt_u32 srctype, struct miff *h, odt_u32 panel)
{
    struct odt_TagItem t[8];
    struct odt_opSet m;
    t[0].ti_Tag = ODT_DTA_SourceType; t[0].ti_Data = srctype;
    t[1].ti_Tag = ODT_DTA_Handle; t[1].ti_Data = (odt_u32)h;
    t[2].ti_Tag = ODT_GA_Left; t[2].ti_Data = 10;
    t[3].ti_Tag = ODT_GA_Top; t[3].ti_Data = 5;
    t[4].ti_Tag = ODT_GA_Width; t[4].ti_Data = cl == &cl_ilbm ? 16 : 40;
    t[5].ti_Tag = ODT_GA_Height; t[5].ti_Data = cl == &cl_ilbm ? 12 : 30;
    t[6].ti_Tag = ODT_DTA_ControlPanel; t[6].ti_Data = panel;
    t[7].ti_Tag = ODT_TAG_DONE; t[7].ti_Data = 0;
    m.MethodID = ODT_OM_NEW;
    m.ops_AttrList = t;
    m.ops_GInfo = NULL;
    ioerr = 0;
    return (void *)call_class(&env, cl, cl, &m);
}

static void dispose(void *o)
{
    odt_u32 m = ODT_OM_DISPOSE;
    m_do_method(&env, o, &m);
}

static int read_px(void *o, odt_u32 fmt, odt_u8 *buf, odt_u32 w, odt_u32 h, odt_u32 bpp)
{
    struct pic_pdtBlitPixelArray m = { PIC_PDTM_READPIXELARRAY, buf, fmt, w * bpp, 0, 0, w, h };
    return (int)m_do_method(&env, o, &m);
}

static void print_px(const char *tag, void *o, odt_u32 fmt, odt_u32 w, odt_u32 h)
{
    odt_u32 bpp = fmt == PIC_PBPAFMT_RGB ? 3 : fmt <= PIC_PBPAFMT_ARGB ? 4 : 1, y;
    odt_u8 *buf = calloc(1, w * h * bpp + 1);
    if (!read_px(o, fmt, buf, w, h, bpp)) printf("%s no\n", tag);
    else for (y = 0; y < h; y++) hexrow(tag, buf + y * w * bpp, w * bpp);
    free(buf);
}

static void screen_init(int depth)
{
    static const odt_u8 fixed[4][3] = { { 0, 0, 0 }, { 255, 255, 255 }, { 0, 0x55, 0xaa }, { 0xff, 0x88, 0 } };
    int p;
    memset(&scr, 0, sizeof scr);
    scr.depth = depth;
    scr.npens = depth <= 8 ? 1 << depth : 0;
    for (p = 0; p < 4 && p < scr.npens; p++) { memcpy(scr.rgb[p], fixed[p], 3); scr.fixed[p] = 1; }
}

static int pens_left(void)
{
    int p, n = 0;
    for (p = 0; p < scr.npens; p++) if (!scr.fixed[p]) n += scr.share[p];
    return n;
}

/* ---- the commands ---------------------------------------------------------- */

static void cmd_ilbm(char **a)
{
    odt_u32 len, y;
    odt_u8 *data = read_file(a[0], &len);
    int sdepth = atoi(a[1]), remap = atoi(a[2]), destmode = atoi(a[3]);
    odt_u32 sw = (odt_u32)atoi(a[4]), sh = (odt_u32)atoi(a[5]), keep = (odt_u32)atoi(a[6]);
    struct miff h;
    void *o;
    long base = live;
    memset(&h, 0, sizeof h);
    h.data = data;
    h.len = len;
    screen_init(sdepth);
    o = new_obj(&cl_ilbm, ODT_DTST_FILE, &h, 0);
    if (!o) {
        printf("fail %ld\n", ioerr);
    } else {
        struct pic_BitMapHeader *b = (void *)get(o, PIC_PDTA_BitMapHeader);
        odt_u32 *cregs = (void *)get(o, PIC_PDTA_CRegs), n = get(o, PIC_PDTA_NumColors), i, w = b->bmh_Width,
                ht = b->bmh_Height;
        struct odt_Point *gp = (void *)get(o, PIC_PDTA_Grab);
        char *name = (char *)get(o, ODT_DTA_ObjName);
        struct odt_TagItem t[4];
        struct odt_opSet set;
        struct odt_gpLayout lay;
        struct odt_GadgetInfo gi;
        struct odt_gpRender rm;
        printf("bmhd %u %u %u %u %u %u %u %u %d %d\n", b->bmh_Width, b->bmh_Height, b->bmh_Depth, b->bmh_Masking,
               b->bmh_Compression, b->bmh_Transparent, b->bmh_XAspect, b->bmh_YAspect, b->bmh_PageWidth,
               b->bmh_PageHeight);
        printf("mode %08lx ncol %lu\ncmap", (unsigned long)get(o, PIC_PDTA_ModeID), (unsigned long)n);
        for (i = 0; i < n; i++)
            printf(" %02lx%02lx%02lx", (unsigned long)(cregs[i * 3] >> 24), (unsigned long)(cregs[i * 3 + 1] >> 24),
                   (unsigned long)(cregs[i * 3 + 2] >> 24));
        printf("\n");
        if (name) printf("name %s\n", name);
        printf("grab %d %d\n", gp->x, gp->y);
        print_px("argb", o, PIC_PBPAFMT_ARGB, w, ht);
        print_px("lut", o, PIC_PBPAFMT_LUT8, w, ht);
        print_px("grey", o, PIC_PBPAFMT_GREY8, w, ht);

        t[0].ti_Tag = PIC_PDTA_Remap; t[0].ti_Data = (odt_u32)remap;
        t[1].ti_Tag = PIC_PDTA_DestMode; t[1].ti_Data = (odt_u32)destmode;
        t[2].ti_Tag = ODT_TAG_DONE;
        set.MethodID = ODT_OM_SET; set.ops_AttrList = t; set.ops_GInfo = NULL;
        m_do_method(&env, o, &set);
        if (sw) {
            struct pic_pdtScale sc = { PIC_PDTM_SCALE, sw, sh, keep ? PIC_PScale_KeepAspect : 0 };
            printf("scale %lu\n", (unsigned long)m_do_method(&env, o, &sc));
        }
        memset(&gi, 0, sizeof gi);
        gi.gi_Screen = sdepth ? &scr : NULL;
        gi.gi_Domain.Width = 640;
        gi.gi_Domain.Height = 480;
        lay.MethodID = ODT_DTM_PROCLAYOUT; lay.gpl_GInfo = &gi; lay.gpl_Initial = 1;
        {
            odt_u32 r = m_do_method(&env, o, &lay);
            struct odt_BitMap *bm = (void *)get(o, PIC_PDTA_DestBitMap);
            odt_u8 *mask = (void *)get(o, PIC_PDTA_MaskPlane);
            struct fdt *f = FDT(o);
            printf("layout %lu %lu %lu depth=%u alloc=%lu\n", (unsigned long)r, (unsigned long)f->tot_h,
                   (unsigned long)f->tot_v, bm ? bm->Depth : 0, (unsigned long)get(o, PIC_PDTA_NumAlloc));
            if (bm) {
                odt_u32 dw = f->tot_h, dh = f->tot_v, x, d;
                odt_u8 *row = malloc(dw * 4 + 1);
                for (y = 0; y < dh; y++) {
                    if (bm->Depth > 8) {
                        hexrow("dest", bm->Planes[0] + y * dw * 4, dw * 4);
                        continue;
                    }
                    for (x = 0; x < dw; x++) {
                        odt_u8 v = 0;
                        for (d = 0; d < bm->Depth; d++)
                            if (bm->Planes[d][y * bm->BytesPerRow + x / 8] & (0x80 >> (x % 8))) v |= (odt_u8)(1 << d);
                        row[x] = v;
                    }
                    hexrow("dest", row, dw);
                }
                free(row);
                if (mask) for (y = 0; y < dh; y++) hexrow("mask", mask + y * (((dw + 15) >> 4) << 1), ((dw + 15) >> 4) << 1);
                else printf("mask none\n");
            }
        }
        {
            struct fdt *f = FDT(o);
            f->top_h = 1;
            f->top_v = 1;
        }
        rm.MethodID = ODT_GM_RENDER; rm.gpr_GInfo = &gi; rm.gpr_RPort = &rp_dummy; rm.gpr_Redraw = ODT_GREDRAW_REDRAW;
        m_do_method(&env, o, &rm);
        {
            struct odt_dtWrite wm = { ODT_DTM_WRITE, NULL, 1, ODT_DTWM_RAW, NULL };
            struct miff h2;
            void *o2;
            wlen = 0;
            if (!m_do_method(&env, o, &wm)) {
                printf("write fail %ld\n", ioerr);
            } else {
                memset(&h2, 0, sizeof h2);
                h2.data = wbuf;
                h2.len = wlen;
                o2 = new_obj(&cl_ilbm, ODT_DTST_FILE, &h2, 0);
                if (!o2) printf("rt fail %ld\n", ioerr);
                else {
                    print_px("rt", o2, PIC_PBPAFMT_RGB, w, ht);
                    dispose(o2);
                }
            }
        }
        dispose(o);
    }
    printf("pens left %d\nlive %ld\n", pens_left(), live - base);
    free(data);
}

static void cmd_pixw(char **a)
{
    odt_u32 len, i;
    odt_u8 *d = read_file(a[0], &len);
    odt_u32 w = (odt_u32)(d[0] << 8 | d[1]), h = (odt_u32)(d[2] << 8 | d[3]), depth = d[4], ncol = (odt_u32)(d[5] << 8 | d[6]), fmt = d[7];
    odt_u32 bpp = fmt == PIC_PBPAFMT_RGB ? 3 : fmt <= PIC_PBPAFMT_ARGB ? 4 : 1;
    long base = live;
    void *o = new_obj(&cl_pic, ODT_DTST_RAM, NULL, 0);
    struct pic_BitMapHeader *b = (void *)get(o, PIC_PDTA_BitMapHeader);
    odt_u32 *cregs = (void *)get(o, PIC_PDTA_CRegs);
    struct odt_TagItem t[2];
    struct odt_opSet set;
    struct pic_pdtBlitPixelArray m;
    b->bmh_Width = (odt_u16)w;
    b->bmh_Height = (odt_u16)h;
    b->bmh_Depth = (odt_u8)depth;
    for (i = 0; i < ncol * 3; i++) cregs[i] = d[8 + i] * 0x01010101UL;
    t[0].ti_Tag = PIC_PDTA_NumColors; t[0].ti_Data = ncol;
    t[1].ti_Tag = ODT_TAG_DONE;
    set.MethodID = ODT_OM_SET; set.ops_AttrList = t; set.ops_GInfo = NULL;
    m_do_method(&env, o, &set);
    m.MethodID = PIC_PDTM_WRITEPIXELARRAY;
    m.pbpa_PixelData = d + 8 + ncol * 3;
    m.pbpa_PixelFormat = fmt;
    m.pbpa_PixelArrayMod = w * bpp;
    m.pbpa_Left = m.pbpa_Top = 0;
    m.pbpa_Width = w;
    m.pbpa_Height = h;
    printf("write %lu\n", (unsigned long)m_do_method(&env, o, &m));
    for (i = 0; i <= PIC_PBPAFMT_GREY8; i++) {
        char tag[8];
        sprintf(tag, "rd%lu", (unsigned long)i);
        print_px(tag, o, i, w, h);
    }
    /* a rectangle partly outside, clipped */
    {
        static odt_u8 one[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        struct pic_pdtBlitPixelArray m2 = { PIC_PDTM_WRITEPIXELARRAY, one, PIC_PBPAFMT_GREY8, 4, w - 2, h - 2, 4, 4 };
        printf("clip %lu\n", (unsigned long)m_do_method(&env, o, &m2));
        print_px("rd2", o, PIC_PBPAFMT_RGB, w, h);
    }
    dispose(o);
    printf("live %ld\n", live - base);
    free(d);
}

static odt_u32 trig(void *o, odt_u32 fn)
{
    struct odt_dtTrigger t = { ODT_DTM_TRIGGER, NULL, fn, NULL };
    return m_do_method(&env, o, &t);
}

static void print_sound(void *o)
{
    odt_u32 len = get(o, SND_SDTA_SampleLength);
    odt_u8 *l = (void *)get(o, SND_SDTA_LeftSample), *r = (void *)get(o, SND_SDTA_RightSample);
    printf("snd len=%lu stereo=%d period=%lu sps=%lu vol=%lu cycles=%lu pan=%lu\n", (unsigned long)len,
           r != NULL && r != l, (unsigned long)get(o, SND_SDTA_Period), (unsigned long)get(o, SND_SDTA_SamplesPerSec),
           (unsigned long)get(o, SND_SDTA_Volume), (unsigned long)get(o, SND_SDTA_Cycles),
           (unsigned long)get(o, SND_SDTA_Pan));
    if (l) hexrow("L", l, len);
    if (r && r != l) hexrow("R", r, len);
}

static odt_u16 dri_pens[9] = { 1, 2, 1, 2, 1, 3, 1, 0, 2 };
static struct odt_DrawInfo dri = { 2, 9, dri_pens };

static void cmd_svx(char **a)
{
    odt_u32 len;
    odt_u8 *data = read_file(a[0], &len);
    int panel = atoi(a[1]);
    struct miff h;
    void *o;
    long base = live;
    memset(&h, 0, sizeof h);
    h.data = data;
    h.len = len;
    o = new_obj(&cl_svx, ODT_DTST_FILE, &h, (odt_u32)panel);
    if (!o) {
        printf("fail %ld\n", ioerr);
    } else {
        struct snd_VoiceHeader *v = (void *)get(o, SND_SDTA_VoiceHeader);
        struct snd_timeval *tv = (void *)get(o, SND_SDTA_ReplayPeriod);
        char *name = (char *)get(o, ODT_DTA_ObjName);
        struct odt_GadgetInfo gi;
        struct odt_gpRender rm;
        struct odt_InputEvent ie;
        odt_s32 term = 0;
        struct odt_gpInput in;
        printf("vh %lu %lu %lu %u %u %u %lu\n", (unsigned long)v->vh_OneShotHiSamples,
               (unsigned long)v->vh_RepeatHiSamples, (unsigned long)v->vh_SamplesPerHiCycle, v->vh_SamplesPerSec,
               v->vh_Octaves, v->vh_Compression, (unsigned long)v->vh_Volume);
        print_sound(o);
        printf("replay %lu %lu\n", (unsigned long)tv->tv_secs, (unsigned long)tv->tv_micro);
        if (name) printf("name %s\n", name);
        printf("trig %lu\n", (unsigned long)trig(o, ODT_STM_PLAY));
        memset(&gi, 0, sizeof gi);
        gi.gi_Domain.Width = 640;
        gi.gi_Domain.Height = 480;
        gi.gi_DrInfo = &dri;
        rm.MethodID = ODT_GM_RENDER; rm.gpr_GInfo = &gi; rm.gpr_RPort = &rp_dummy; rm.gpr_Redraw = ODT_GREDRAW_REDRAW;
        m_do_method(&env, o, &rm);
        memset(&ie, 0, sizeof ie);
        ie.ie_Class = ODT_IECLASS_RAWMOUSE;
        ie.ie_Code = ODT_SELECTDOWN;
        in.MethodID = ODT_GM_GOACTIVE; in.gpi_GInfo = &gi; in.gpi_IEvent = &ie; in.gpi_Termination = &term;
        in.gpi_Mouse.x = 30; in.gpi_Mouse.y = 28; in.gpi_TabletData = NULL;
        printf("goactive %lu\n", (unsigned long)m_do_method(&env, o, &in));
        printf("trig %lu\n", (unsigned long)trig(o, ODT_STM_STOP));
        {
            struct odt_dtWrite wm = { ODT_DTM_WRITE, NULL, 1, ODT_DTWM_RAW, NULL };
            struct miff h2;
            void *o2;
            wlen = 0;
            if (!m_do_method(&env, o, &wm)) {
                printf("write fail %ld\n", ioerr);
            } else {
                memset(&h2, 0, sizeof h2);
                h2.data = wbuf;
                h2.len = wlen;
                o2 = new_obj(&cl_svx, ODT_DTST_FILE, &h2, 0);
                if (!o2) printf("rt fail %ld\n", ioerr);
                else {
                    printf("rt\n");
                    print_sound(o2);
                    dispose(o2);
                }
            }
        }
        dispose(o);
    }
    printf("live %ld\n", live - base);
    free(data);
}

static void cmd_snd(char **a)
{
    odt_u32 len = (odt_u32)atol(a[0]), stereo = (odt_u32)atoi(a[1]), cycles = (odt_u32)atoi(a[2]);
    long pan = atol(a[3]);
    odt_u32 type = (odt_u32)atoi(a[4]), i, ch;
    long base = live;
    void *o = new_obj(&cl_snd, ODT_DTST_RAM, NULL, 0);
    odt_u32 wide = type == SND_SDTST_M16S || type == SND_SDTST_S16S;
    odt_u32 inter = type == SND_SDTST_S8S || type == SND_SDTST_S16S;
    odt_u32 nbuf = inter ? 1 : stereo ? 2 : 1, per = inter ? 2 : 1;
    odt_u8 *buf[2] = { NULL, NULL };
    struct odt_TagItem t[12];
    struct odt_opSet set;
    struct snd_timeval *tv;
    int k = 0;
    for (ch = 0; ch < nbuf; ch++) {
        odt_u8 *b = buf[ch] = m_alloc(&env, len * per * (wide ? 2 : 1));
        for (i = 0; i < len * per; i++) {
            odt_u32 c = inter ? i % 2 : ch, s = inter ? i / 2 : i;
            odt_u8 hi = (odt_u8)((s * 37 + c * 11) & 0xff);
            if (wide) { b[i * 2] = hi; b[i * 2 + 1] = (odt_u8)(s & 0xff); }
            else b[i] = hi;
        }
    }
    t[k].ti_Tag = SND_SDTA_SampleType; t[k++].ti_Data = type;
    t[k].ti_Tag = nbuf == 2 ? SND_SDTA_LeftSample : SND_SDTA_Sample; t[k++].ti_Data = (odt_u32)buf[0];
    if (nbuf == 2) { t[k].ti_Tag = SND_SDTA_RightSample; t[k++].ti_Data = (odt_u32)buf[1]; }
    t[k].ti_Tag = SND_SDTA_SampleLength; t[k++].ti_Data = len;
    t[k].ti_Tag = SND_SDTA_SamplesPerSec; t[k++].ti_Data = 22050;
    t[k].ti_Tag = SND_SDTA_Volume; t[k++].ti_Data = 50;
    t[k].ti_Tag = SND_SDTA_Cycles; t[k++].ti_Data = cycles;
    t[k].ti_Tag = SND_SDTA_SignalBitNumber; t[k++].ti_Data = 12;
    if (pan >= 0) { t[k].ti_Tag = SND_SDTA_Pan; t[k++].ti_Data = (odt_u32)pan; }
    t[k].ti_Tag = ODT_TAG_DONE; t[k].ti_Data = 0;
    set.MethodID = ODT_OM_SET; set.ops_AttrList = t; set.ops_GInfo = NULL;
    m_do_method(&env, o, &set);
    tv = (void *)get(o, SND_SDTA_ReplayPeriod);
    printf("period %lu replay %lu %lu\n", (unsigned long)get(o, SND_SDTA_Period), (unsigned long)tv->tv_secs,
           (unsigned long)tv->tv_micro);
    printf("trig %lu\n", (unsigned long)trig(o, ODT_STM_PLAY));
    dispose(o);
    printf("live %ld\n", live - base);
}

/* SDTA_VoiceHeader set (its rate and volume taken), then SDTA_SampleLength 0
 * while the sound plays (which stops it) */
static void cmd_sndvh(char **a)
{
    odt_u32 len = (odt_u32)atol(a[0]), i;
    long base = live;
    void *o = new_obj(&cl_snd, ODT_DTST_RAM, NULL, 0);
    odt_u8 *b = m_alloc(&env, len);
    struct snd_VoiceHeader vh, *v;
    struct odt_TagItem t[6];
    struct odt_opSet set;
    for (i = 0; i < len; i++) b[i] = (odt_u8)((i * 37) & 0xff);
    memset(&vh, 0, sizeof vh);
    vh.vh_OneShotHiSamples = len;
    vh.vh_SamplesPerSec = 11025;
    vh.vh_Volume = 0x8000;
    t[0].ti_Tag = SND_SDTA_Sample; t[0].ti_Data = (odt_u32)b;
    t[1].ti_Tag = SND_SDTA_SampleLength; t[1].ti_Data = len;
    t[2].ti_Tag = SND_SDTA_VoiceHeader; t[2].ti_Data = (odt_u32)&vh;
    t[3].ti_Tag = SND_SDTA_Cycles; t[3].ti_Data = 1;
    t[4].ti_Tag = SND_SDTA_SignalBitNumber; t[4].ti_Data = 12;
    t[5].ti_Tag = ODT_TAG_DONE; t[5].ti_Data = 0;
    set.MethodID = ODT_OM_SET; set.ops_AttrList = t; set.ops_GInfo = NULL;
    m_do_method(&env, o, &set);
    v = (void *)get(o, SND_SDTA_VoiceHeader);
    printf("vh %lu %u %lu\n", (unsigned long)v->vh_OneShotHiSamples, v->vh_SamplesPerSec,
           (unsigned long)v->vh_Volume);
    printf("sps %lu period %lu vol %lu\n", (unsigned long)get(o, SND_SDTA_SamplesPerSec),
           (unsigned long)get(o, SND_SDTA_Period), (unsigned long)get(o, SND_SDTA_Volume));
    printf("trig %lu\n", (unsigned long)trig(o, ODT_STM_PLAY));
    t[0].ti_Tag = SND_SDTA_SampleLength; t[0].ti_Data = 0;
    t[1].ti_Tag = ODT_TAG_DONE;
    m_do_method(&env, o, &set);
    printf("cleared\n");
    dispose(o);
    printf("live %ld\n", live - base);
}

int main(int argc, char **argv)
{
    char line[1024];
    FILE *f;
    if (argc < 2 || !(f = fopen(argv[1], "r"))) return 20;
    env_init();
    make_class(&cl_dt, NULL, fdt_dispatch, FDT_SIZE);
    make_class(&cl_pic, &cl_dt, pic_dispatch, pic_inst_size());
    make_class(&cl_ilbm, &cl_pic, ilbm_dispatch, ilbm_inst_size());
    make_class(&cl_snd, &cl_dt, snd_dispatch, snd_inst_size());
    make_class(&cl_svx, &cl_snd, svx_dispatch, svx_inst_size());
    while (fgets(line, sizeof line, f)) {
        char *a[16];
        int n = 0;
        char *p = strtok(line, " \n");
        while (p && n < 16) { a[n++] = p; p = strtok(NULL, " \n"); }
        if (!n) continue;
        printf("==");
        for (int i = 0; i < n; i++) printf(" %s", a[i]);
        printf("\n");
        if (!strcmp(a[0], "ilbm")) cmd_ilbm(a + 1);
        else if (!strcmp(a[0], "pixw")) cmd_pixw(a + 1);
        else if (!strcmp(a[0], "svx")) cmd_svx(a + 1);
        else if (!strcmp(a[0], "snd")) cmd_snd(a + 1);
        else if (!strcmp(a[0], "sndvh")) cmd_sndvh(a + 1);
        fflush(stdout);
    }
    printf("end\n");
    return 0;
}
