/* GFOpenDatatypes: an open text.datatype for AmigaOS 3.2, API and ABI
 * compatible.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the text_dtc autodoc, datatypes/textclass.h
 * (NDK 3.2) and OS 3.2's answers measured by tests/TextClassABITest,
 * never from Kickstart's, Hyperion's or AROS's code or bytes. The class
 * is text_core.c (plain C, no Amiga headers), tested under qemu-m68k
 * (tests/m68k/test_opendatatypes_textanim.sh); this file is the class
 * library around it, from dtc_shell.h: the RomTag, the vectors (text.lvo:
 * Open, Close, Expunge, Null, ObtainEngine at -30), MakeClass and AddClass
 * of the public class "text.datatype" under datatypesclass, the
 * dispatcher's entry, and the text family's services (fonts, drawing,
 * the clip region, DrawInfo pens, DoubleClick).
 *
 * LIBNAME is "text.datatype" for the replacement and "gfopentext.datatype"
 * for the side-by-side build the ABI test opens next to OS 3.2's.
 */
#ifndef LIBNAME
#define LIBNAME "text.datatype"
#endif
#define DTC_SUPERCLASS "datatypesclass"
#define DTC_SUPERLIB "datatypes.library"
#define DTC_DISPATCH tdt_dispatch
#define DTC_INST_SIZE tdt_inst_size
#define DTC_IDSTRING LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team\r\n"
#define DTC_EXT_INIT text_ext_init

#include "text_core.h"
struct DTCBase;
static int text_ext_init(struct DTCBase *base);
#include "dtc_shell.h"
#include <proto/diskfont.h>
#include <proto/layers.h>

static const char ver[] __attribute__((used)) =
    "\0$VER: " LIBNAME " 47.100 (13.10.2026) GFOpenDatatypes, AmiCradle Team";

/* ---- the text family's services --------------------------------------- */

static void *x_open_font(const struct odt_cenv *e, const void *ta)
{
    struct TextFont *f = NULL;
    (void)e;
    if (DiskfontBase) f = OpenDiskFont((struct TextAttr *)ta);
    if (!f) f = OpenFont((struct TextAttr *)ta);
    return f;
}
static void x_close_font(const struct odt_cenv *e, void *f) { (void)e; if (f) CloseFont((struct TextFont *)f); }
static void *x_default_font(const struct odt_cenv *e, struct odt_GadgetInfo *gi)
{
    struct GadgetInfo *g = (struct GadgetInfo *)gi;
    (void)e;
    /* the screen's font (ABI test: OS 3.2 may take the system default) */
    if (g && g->gi_DrInfo && g->gi_DrInfo->dri_Font) return g->gi_DrInfo->dri_Font;
    return GfxBase->DefaultFont;
}
static void x_font_size(const struct odt_cenv *e, void *font, odt_u16 *ys, odt_u16 *bl, odt_u16 *xs)
{
    struct TextFont *f = font;
    (void)e;
    *ys = f->tf_YSize;
    *bl = f->tf_Baseline;
    *xs = f->tf_XSize;
}
static odt_s32 x_text_length(const struct odt_cenv *e, void *font, odt_u32 style, const char *s, odt_u32 n)
{
    struct RastPort rp;
    (void)e;
    InitRastPort(&rp);
    SetFont(&rp, (struct TextFont *)font);
    SetSoftStyle(&rp, style, AskSoftStyle(&rp));
    return TextLength(&rp, (CONST_STRPTR)s, n);
}
static void x_text(const struct odt_cenv *e, void *rport, void *font, odt_u32 style, odt_s32 fg, odt_s32 x, odt_s32 y,
                   const char *s, odt_u32 n)
{
    struct RastPort *rp = rport;
    struct TextFont *old = rp->Font;
    (void)e;
    SetFont(rp, (struct TextFont *)font);
    SetSoftStyle(rp, style, AskSoftStyle(rp));
    SetAPen(rp, fg);
    SetDrMd(rp, JAM1);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (CONST_STRPTR)s, n);
    SetSoftStyle(rp, 0, AskSoftStyle(rp));
    if (old) SetFont(rp, old);
}
static void x_fill(const struct odt_cenv *e, void *rp, odt_s32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    (void)e;
    if (x1 < x0 || y1 < y0) return;
    SetAPen((struct RastPort *)rp, pen);
    SetDrMd((struct RastPort *)rp, JAM1);
    RectFill((struct RastPort *)rp, x0, y0, x1, y1);
}

struct clip { struct Region *mine, *old; };

static void *x_clip(const struct odt_cenv *e, void *rport, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1)
{
    struct RastPort *rp = rport;
    struct clip *c;
    struct Rectangle r;
    (void)e;
    if (!rp->Layer || !(c = AllocVec(sizeof *c, MEMF_ANY))) return NULL;
    if (!(c->mine = NewRegion())) { FreeVec(c); return NULL; }
    r.MinX = x0; r.MinY = y0; r.MaxX = x1; r.MaxY = y1;
    /* rp's coordinates are its layer's */
    OrRectRegion(c->mine, &r);
    c->old = InstallClipRegion(rp->Layer, c->mine);
    return c;
}
static void x_unclip(const struct odt_cenv *e, void *rport, void *p)
{
    struct RastPort *rp = rport;
    struct clip *c = p;
    (void)e;
    InstallClipRegion(rp->Layer, c->old);
    DisposeRegion(c->mine);
    FreeVec(c);
}
static odt_s32 x_pen(const struct odt_cenv *e, struct odt_GadgetInfo *gi, odt_u32 which)
{
    static const UBYTE defaults[] = { 0, 1, 1, 2, 1, 3, 1, 0, 2 };
    struct GadgetInfo *g = (struct GadgetInfo *)gi;
    (void)e;
    if (g && g->gi_DrInfo && which < g->gi_DrInfo->dri_NumPens) return g->gi_DrInfo->dri_Pens[which];
    return which < sizeof defaults ? defaults[which] : 1;
}
static int x_double_click(const struct odt_cenv *e, odt_u32 s1, odt_u32 m1, odt_u32 s2, odt_u32 m2)
{ (void)e; return DoubleClick(s1, m1, s2, m2) != 0; }

static const struct tdt_ext text_ext = {
    x_open_font, x_close_font, x_default_font, x_font_size, x_text_length, x_text, x_fill, x_clip, x_unclip, x_pen,
    x_double_click
};

static int text_ext_init(struct DTCBase *base)
{
    base->ext = (void *)&text_ext;
    base->env.class_ext = (void *)&text_ext;
    return 1;
}

static const APTR lib_vectors[] = { DTC_STD_VECTORS, (APTR)-1 };

DTC_ROMTAG(lib_vectors);
