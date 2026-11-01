/* GFOpenPicture: picture.datatype's class, lean C++ with no Amiga headers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the picture_dtc autodoc, datatypes/pictureclass.h's
 * published layouts (NDK 3.2), the IFF ILBM specification (EA, public)
 * and OS 3.2's answers measured by tests/PictureDTClassABITest; never from
 * Kickstart, Hyperion or AROS code. Everything the system does comes in
 * through struct odt_cenv and its class_ext, struct dtc_ext.
 *
 * The picture keeps its source as pixels: one byte an index (LUT8) or
 * four bytes ARGB, chosen by the first PDTM_WRITEPIXELARRAY (V43
 * subclasses), or read from PDTA_BitMap (V42 subclasses) at layout. A
 * layout makes the destination: scaled (PDTM_SCALE, nearest pixel),
 * remapped to the screen's pens (ObtainBestPen, shared, released at the
 * next layout or at dispose), or true colour on a screen deeper than 8
 * planes when PDTA_DestMode is PMODE_V43; and a mask plane when the
 * picture has a mask, a transparent colour or alpha.
 *
 * Where the autodocs leave a corner open the code says "ABI test" and the
 * ABI test's PROBE line records OS 3.2's answer.
 */
#include "picture_core.h"
#include "../../../../../common/include/odt_planar.h"

namespace {

using u8 = odt_u8;
using u16 = odt_u16;
using u32 = odt_u32;
using s32 = odt_s32;
using env_t = const odt_cenv;

template <class T> constexpr T min(T a, T b) { return a < b ? a : b; }
template <class T> constexpr T max(T a, T b) { return a > b ? a : b; }

inline const dtc_ext *X(env_t *env) { return DTC_EXT(env); }
inline pic_inst *inst(odt_IClass *cl, void *o) { return static_cast<pic_inst *>(ODT_INST_DATA(cl, o)); }
inline u32 method(void *msg) { return *static_cast<u32 *>(msg); }

void zero(void *p, u32 n)
{
    u8 *d = static_cast<u8 *>(p);
    for (u32 i = 0; i < n; i++)
        d[i] = 0;
}
void copy(void *to, const void *from, u32 n)
{
    u8 *d = static_cast<u8 *>(to);
    const u8 *s = static_cast<const u8 *>(from);
    u32 i = 0;
    if (!((reinterpret_cast<u32>(d) | reinterpret_cast<u32>(s)) & 3)) {
        /* both on a long: four bytes at a time */
        using u32a = u32 __attribute__((__may_alias__));
        for (; i + 4 <= n; i += 4)
            *reinterpret_cast<u32a *>(d + i) = *reinterpret_cast<const u32a *>(s + i);
    }
    for (; i < n; i++)
        d[i] = s[i];
}

void set_err(env_t *env, s32 e)
{
    if (env->set_io_err)
        env->set_io_err(env, e);
}

/* ---- pens and the destination ------------------------------------------------ */

void release_pens(env_t *env, pic_inst *p)
{
    for (u32 i = 0; i < p->numalloc; i++)
        if (X(env)->release_pen)
            X(env)->release_pen(env, p->pen_cm, p->pens[i]);
    p->numalloc = 0;
    zero(p->allocated, sizeof p->allocated);
    p->pen_cm = nullptr;
}

void free_dest(env_t *env, pic_inst *p)
{
    release_pens(env, p);
    if (p->destbm && X(env)->free_bitmap)
        X(env)->free_bitmap(env, p->destbm);
    p->destbm = nullptr;
    env->free(env, p->mask);
    p->mask = nullptr;
    p->dw = p->dh = 0;
    p->flags &= ~(PIC_F_LAIDOUT | PIC_F_DESTARGB);
    p->laid_screen = nullptr;
}

void free_classbm(env_t *env, pic_inst *p)
{
    if (p->classbm && X(env)->free_bitmap)
        X(env)->free_bitmap(env, p->classbm);
    p->classbm = nullptr;
}

void free_all(env_t *env, pic_inst *p)
{
    free_dest(env, p);
    free_classbm(env, p);
    if ((p->flags & PIC_F_FREESRC) && p->srcbm && X(env)->free_bitmap)
        X(env)->free_bitmap(env, p->srcbm);
    p->srcbm = nullptr;
    env->free(env, p->pixels);
    env->free(env, p->alpha);
    p->pixels = p->alpha = nullptr;
    p->store = PIC_STORE_NONE;
}

/* A subclass that fills only PDTA_ColorRegisters (8-bit guns) gets CRegs from it */
void fix_cregs(pic_inst *p)
{
    for (u32 i = 0; i < p->numcolors; i++) {
        u32 *c = &p->cregs[i * 3];
        if (c[0] | c[1] | c[2])
            continue;
        c[0] = p->cmap[i].red * 0x01010101UL;
        c[1] = p->cmap[i].green * 0x01010101UL;
        c[2] = p->cmap[i].blue * 0x01010101UL;
    }
}

/* The V42 subclass's PDTA_BitMap read into LUT8 pixels */
bool ensure_pixels(env_t *env, pic_inst *p)
{
    if (p->pixels)
        return true;
    if (!p->srcbm)
        return false;
    u32 w = p->bmhd.bmh_Width ? p->bmhd.bmh_Width : p->srcbm->BytesPerRow * 8U;
    u32 h = p->bmhd.bmh_Height ? p->bmhd.bmh_Height : p->srcbm->Rows;
    h = min<u32>(h, p->srcbm->Rows);
    w = min<u32>(w, p->srcbm->BytesPerRow * 8U);
    if (!w || !h)
        return false;
    if (w > 0xffffUL || h > 0xffffUL) {
        set_err(env, DTC_ERROR_NO_FREE_STORE); /* w * h would not fit */
        return false;
    }
    p->pixels = static_cast<u8 *>(env->alloc(env, w * h));
    if (!p->pixels) {
        set_err(env, DTC_ERROR_NO_FREE_STORE);
        return false;
    }
    pic_planar_to_chunky(p->srcbm, w, h, p->pixels);
    p->store = PIC_STORE_LUT8;
    p->pw = w;
    p->ph = h;
    return true;
}

/* A source pixel as ARGB */
void argb_of(const pic_inst *p, u32 i, u8 out[4])
{
    if (p->store == PIC_STORE_ARGB) {
        copy(out, p->pixels + i * 4, 4);
        return;
    }
    u32 c = p->pixels[i];
    out[1] = static_cast<u8>(p->cregs[c * 3] >> 24);
    out[2] = static_cast<u8>(p->cregs[c * 3 + 1] >> 24);
    out[3] = static_cast<u8>(p->cregs[c * 3 + 2] >> 24);
    /* ABI test: the alpha of an index pixel. Ours: the mask's, 0 for the
     * transparent colour, else opaque. */
    if (p->alpha)
        out[0] = p->alpha[i];
    else if (p->bmhd.bmh_Masking == PIC_mskHasTransparentColor && c == p->bmhd.bmh_Transparent)
        out[0] = 0;
    else
        out[0] = 0xff;
}

bool opaque(const pic_inst *p, u32 i)
{
    if (p->alpha)
        return p->alpha[i] != 0;
    if (p->store == PIC_STORE_ARGB)
        return p->bmhd.bmh_Masking != PIC_mskHasAlpha || p->pixels[i * 4] >= 0x80; /* ABI test: the cut */
    if (p->bmhd.bmh_Masking == PIC_mskHasTransparentColor)
        return p->pixels[i] != p->bmhd.bmh_Transparent;
    return true;
}

u32 dist(s32 r, s32 g, s32 b, const u32 *c)
{
    s32 dr = r - static_cast<s32>(c[0] >> 24), dg = g - static_cast<s32>(c[1] >> 24),
        db = b - static_cast<s32>(c[2] >> 24);
    return static_cast<u32>(dr * dr + dg * dg + db * db);
}

/* The palette entry nearest an 8-bit colour, among n (the first on a tie) */
u32 nearest(const u32 *regs, u32 n, u32 r, u32 g, u32 b)
{
    u32 best = 0, bd = 0xffffffffUL;
    for (u32 i = 0; i < n; i++) {
        u32 d = dist(static_cast<s32>(r), static_cast<s32>(g), static_cast<s32>(b), regs + i * 3);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

s32 obtain(env_t *env, pic_inst *p, void *cm, u32 r, u32 g, u32 b)
{
    s32 pen = X(env)->obtain_pen ? X(env)->obtain_pen(env, cm, r, g, b, p->precision) : -1;
    if (pen < 0)
        return 0;                       /* ABI test: a colour no pen could take: pen 0 */
    if (p->numalloc < 256) {
        p->pens[p->numalloc++] = static_cast<u8>(pen);
        p->allocated[pen & 0xff] = 1;
    }
    return pen;
}

/* The cube a true colour picture is remapped through on a screen of depth planes */
u32 cube_levels(u32 depth)
{
    return depth >= 8 ? 6 : depth == 7 ? 5 : depth == 6 ? 4 : depth == 5 ? 3 : 2;
}
inline u32 level_of(u32 v, u32 L) { return (v * (L - 1) + 127) / 255; }
inline u32 level_value(u32 l, u32 L) { return l * 255 / (L - 1); }

/* The source columns of a row scaled from sw to dw pixels, one after another:
 * xx * sw / dw for xx = 0, 1, ..., without a division for each pixel. */
struct col_step {
    u32 q, r, d, acc, v;
    col_step(u32 sw, u32 dw) : q(sw / dw), r(sw % dw), d(dw), acc(0), v(0) {}
    u32 next()
    {
        u32 o = v;
        v += q;
        acc += r;
        if (acc >= d) {
            acc -= d;
            v++;
        }
        return o;
    }
};

bool layout(env_t *env, odt_IClass *cl, void *obj, pic_inst *p, odt_GadgetInfo *gi)
{
    void *screen = p->screen ? p->screen : gi ? gi->gi_Screen : nullptr;
    if ((p->flags & PIC_F_LAIDOUT) && p->destbm && screen == p->laid_screen)
        return true;
    free_dest(env, p);
    if (!ensure_pixels(env, p))
        return false;
    fix_cregs(p);
    const dtc_ext *x = X(env);
    u32 sw = p->pw, sh = p->ph;
    u32 dw = p->scale_w ? p->scale_w : sw, dh = p->scale_h ? p->scale_h : sh;
    u32 depth = screen && x->screen_depth ? x->screen_depth(env, screen) : 0;
    void *cm = screen && x->screen_colormap ? x->screen_colormap(env, screen) : nullptr;
    bool remap = (p->flags & PIC_F_REMAP) && screen;
    bool truecolour = remap ? depth > 8 && p->destmode == PIC_PMODE_V43 : p->store == PIC_STORE_ARGB;
    /* a true colour destination is the screen's kind of bitmap (RTG): its friend */
    odt_BitMap *friend_bm = ((p->flags & PIC_F_FRIEND) || (remap && truecolour)) && screen && x->screen_bitmap
                                ? x->screen_bitmap(env, screen) : nullptr;
    if (remap && depth > 8 && !truecolour)
        depth = 8;                      /* PMODE_V42 on a deep screen: 8 planes of pens */

    /* sizes of a BitMap (16 bits each), so dw * dh fits; 4 bytes a pixel
     * of a true colour picture checked before they are multiplied */
    u32 n = dw <= 0xffffUL && dh <= 0xffffUL ? dw * dh : 0;
    bool fits = n && (!truecolour || n <= 0x3fffffffUL);
    /* ARGB pixels shown at their own size are written as they are: no copy */
    bool direct = truecolour && p->store == PIC_STORE_ARGB && dw == sw && dh == sh;
    u8 *buf = fits && !direct ? static_cast<u8 *>(env->alloc(env, truecolour ? n * 4 : n)) : nullptr;
    if (!fits || (!buf && !direct) || !x->alloc_bitmap) {
        env->free(env, buf);
        set_err(env, DTC_ERROR_NO_FREE_STORE);
        return false;
    }
    u8 *cube = nullptr;
    u32 L = 0;
    if (direct) {
        /* p->pixels go to write_argb below */
    } else if (truecolour) {
        u8 *d = buf;
        for (u32 y = 0; y < dh; y++) {
            u32 row = (y * sh / dh) * sw;
            col_step c(sw, dw);
            for (u32 xx = 0; xx < dw; xx++, d += 4)
                argb_of(p, row + c.next(), d);
        }
    } else if (p->store == PIC_STORE_LUT8) {
        u32 nc = p->numcolors ? p->numcolors : 256;
        if (remap) {
            if (p->numsparse) {
                /* only the sparse table's colours get pens; the others
                 * take the pen of the nearest of them */
                u32 *sparse_regs = static_cast<u32 *>(env->alloc(env, 256 * 3 * 4));
                if (!sparse_regs) {
                    env->free(env, buf);
                    set_err(env, DTC_ERROR_NO_FREE_STORE);
                    return false;
                }
                for (u32 i = 0; i < p->numsparse; i++) {
                    u32 c = p->sparse[i];
                    sparse_regs[i * 3] = p->cregs[c * 3];
                    sparse_regs[i * 3 + 1] = p->cregs[c * 3 + 1];
                    sparse_regs[i * 3 + 2] = p->cregs[c * 3 + 2];
                    p->colortable[c] = static_cast<u8>(
                        obtain(env, p, cm, p->cregs[c * 3], p->cregs[c * 3 + 1], p->cregs[c * 3 + 2]));
                }
                for (u32 c = 0; c < nc; c++) {
                    u32 k = nearest(sparse_regs, p->numsparse, p->cregs[c * 3] >> 24, p->cregs[c * 3 + 1] >> 24,
                                    p->cregs[c * 3 + 2] >> 24);
                    p->colortable[c] = p->colortable[p->sparse[k]];
                }
                env->free(env, sparse_regs);
            } else {
                for (u32 c = 0; c < nc; c++)
                    p->colortable[c] = static_cast<u8>(
                        obtain(env, p, cm, p->cregs[c * 3], p->cregs[c * 3 + 1], p->cregs[c * 3 + 2]));
            }
            for (u32 c = 0; c < nc; c++) {
                p->colortable2[c] = p->colortable[c];
                if (x->get_rgb32)
                    x->get_rgb32(env, cm, p->colortable[c], &p->gregs[c * 3]);
            }
        } else {
            for (u32 c = 0; c < 256; c++)
                p->colortable[c] = p->colortable2[c] = static_cast<u8>(c);
            depth = p->bmhd.bmh_Depth >= 1 && p->bmhd.bmh_Depth <= 8 ? p->bmhd.bmh_Depth : 8;
        }
        u8 *d = buf;
        for (u32 y = 0; y < dh; y++) {
            const u8 *row = p->pixels + (y * sh / dh) * sw;
            col_step c(sw, dw);
            for (u32 xx = 0; xx < dw; xx++)
                *d++ = p->colortable[row[c.next()]];
        }
    } else {
        /* true colour onto pens: through a cube of pens. ABI test: OS 3.2
         * may dither (PDTA_DitherQuality); ours takes the nearest. */
        L = cube_levels(depth);
        cube = static_cast<u8 *>(env->alloc(env, L * L * L));
        if (!cube) {
            env->free(env, buf);
            set_err(env, DTC_ERROR_NO_FREE_STORE);
            return false;
        }
        for (u32 r = 0; r < L; r++)
            for (u32 g = 0; g < L; g++)
                for (u32 b = 0; b < L; b++)
                    cube[(r * L + g) * L + b] = static_cast<u8>(obtain(env, p, cm, level_value(r, L) * 0x01010101UL,
                                                                       level_value(g, L) * 0x01010101UL,
                                                                       level_value(b, L) * 0x01010101UL));
        u8 lv[256];                     /* each 8-bit level's place in the cube */
        for (u32 v = 0; v < 256; v++)
            lv[v] = static_cast<u8>(level_of(v, L));
        u8 *d = buf;
        for (u32 y = 0; y < dh; y++) {
            const u8 *row = p->pixels + (y * sh / dh) * sw * 4;
            col_step c(sw, dw);
            for (u32 xx = 0; xx < dw; xx++) {
                const u8 *s = row + c.next() * 4;
                *d++ = cube[(lv[s[1]] * L + lv[s[2]]) * L + lv[s[3]]];
            }
        }
        env->free(env, cube);
    }
    if (truecolour && !remap)
        depth = 24;
    p->destbm = x->alloc_bitmap(env, dw, dh, depth, friend_bm);
    if (!p->destbm) {
        env->free(env, buf);
        release_pens(env, p);
        set_err(env, DTC_ERROR_NO_FREE_STORE);
        return false;
    }
    if (truecolour) {
        if (x->write_argb)
            x->write_argb(env, p->destbm, direct ? p->pixels : buf, dw, dh, dw * 4);
        p->flags |= PIC_F_DESTARGB;
    } else if (x->write_lut8) {
        x->write_lut8(env, p->destbm, buf, dw, dh, dw);
    }
    env->free(env, buf);
    p->pen_cm = cm;
    p->dw = dw;
    p->dh = dh;

    u8 m = p->bmhd.bmh_Masking;
    if (p->alpha || m == PIC_mskHasMask || m == PIC_mskHasTransparentColor || m == PIC_mskHasAlpha) {
        /* a plane of the destination for BltMaskBitMapRastPort: its rows
         * as long as the bitmap's (AllocBitMap may round them up); of a
         * friend (RTG) bitmap RASSIZE (ABI test) */
        u32 rb = !friend_bm && p->destbm->Depth <= 8 && p->destbm->BytesPerRow >= pic_rowbytes(dw)
                     ? p->destbm->BytesPerRow : pic_rowbytes(dw);
        p->mask = static_cast<u8 *>(x->alloc_chip ? x->alloc_chip(env, rb * dh) : env->alloc(env, rb * dh));
        if (!p->mask) {
            /* no mask: the transparent pixels would be drawn, so no layout */
            free_dest(env, p);
            set_err(env, DTC_ERROR_NO_FREE_STORE);
            return false;
        }
        for (u32 y = 0; y < dh; y++) {
            u32 row = (y * sh / dh) * sw;
            col_step c(sw, dw);
            for (u32 xx = 0; xx < dw; xx++)
                if (opaque(p, row + c.next()))
                    p->mask[y * rb + (xx >> 3)] |= static_cast<u8>(0x80 >> (xx & 7));
        }
    }
    p->flags |= PIC_F_LAIDOUT;
    p->laid_screen = screen;
    /* PDTA_FreeSourceBitMap: freed once the layout has the pixels from it */
    if ((p->flags & PIC_F_FREESRC) && p->srcbm && p->pixels && x->free_bitmap) {
        x->free_bitmap(env, p->srcbm);
        p->srcbm = nullptr;
    }

    odt_TagItem t[] = { { ODT_DTA_TotalHoriz, dw }, { ODT_DTA_TotalVert, dh }, { ODT_DTA_HorizUnit, 1 },
                        { ODT_DTA_VertUnit, 1 }, { ODT_TAG_DONE, 0 } };
    dtc_set_super(env, cl, obj, t);
    return true;
}

void invalidate(env_t *env, pic_inst *p)
{
    (void)env;
    p->flags &= ~PIC_F_LAIDOUT;
}

/* ---- attributes ------------------------------------------------------------- */

u32 set_attrs(env_t *env, pic_inst *p, odt_TagItem *tags)
{
    u32 n = 0;
    odt_TagItem *t;
    while ((t = dtc_next_tag(&tags)) != nullptr) {
        u32 d = t->ti_Data;
        switch (t->ti_Tag) {
        case PIC_PDTA_ModeID: p->modeid = d; break;
        case PIC_PDTA_NumColors: p->numcolors = static_cast<u16>(min<u32>(d, 256)); invalidate(env, p); break;
        case PIC_PDTA_Remap:
            p->flags = static_cast<u16>(d ? p->flags | PIC_F_REMAP : p->flags & ~PIC_F_REMAP);
            invalidate(env, p);
            break;
        case PIC_PDTA_Screen: p->screen = reinterpret_cast<void *>(d); invalidate(env, p); break;
        case PIC_PDTA_FreeSourceBitMap:
            p->flags = static_cast<u16>(d ? p->flags | PIC_F_FREESRC : p->flags & ~PIC_F_FREESRC);
            break;
        case PIC_PDTA_UseFriendBitMap:
            p->flags = static_cast<u16>(d ? p->flags | PIC_F_FRIEND : p->flags & ~PIC_F_FRIEND);
            invalidate(env, p);
            break;
        case PIC_PDTA_Grab:
            if (d)
                p->grab = *reinterpret_cast<odt_Point *>(d);
            break;
        case PIC_PDTA_BitMap:
            /* a V42 subclass's picture: it replaces any pixels */
            if (p->srcbm != reinterpret_cast<odt_BitMap *>(d) && (p->flags & PIC_F_FREESRC) && p->srcbm &&
                X(env)->free_bitmap)
                X(env)->free_bitmap(env, p->srcbm);
            p->srcbm = reinterpret_cast<odt_BitMap *>(d);
            env->free(env, p->pixels);
            p->pixels = nullptr;
            p->store = PIC_STORE_NONE;
            free_classbm(env, p);
            invalidate(env, p);
            break;
        case PIC_PDTA_NumSparse: p->numsparse = static_cast<u16>(min<u32>(d, 256)); invalidate(env, p); break;
        case PIC_PDTA_SparseTable:
            if (d)
                copy(p->sparse, reinterpret_cast<u8 *>(d), p->numsparse);
            invalidate(env, p);
            break;
        case PIC_PDTA_SourceMode: p->sourcemode = d; break;
        case PIC_PDTA_DestMode: p->destmode = d; invalidate(env, p); break;
        case PIC_OBP_Precision: p->precision = static_cast<s32>(d); invalidate(env, p); break;
        case PIC_PDTA_MaskPlane: {
            /* ABI test: OS 3.2 documents MaskPlane as read only; ours
             * takes a subclass's mask (a plane of the picture's width)
             * into the source's alpha. */
            u32 w = p->bmhd.bmh_Width, h = p->bmhd.bmh_Height, rb = pic_rowbytes(w);
            env->free(env, p->alpha);
            p->alpha = nullptr;
            if (d && w && h && (p->alpha = static_cast<u8 *>(env->alloc(env, w * h))) != nullptr) {
                const u8 *m = reinterpret_cast<const u8 *>(d);
                for (u32 y = 0; y < h; y++)
                    for (u32 x = 0; x < w; x++)
                        p->alpha[y * w + x] = (m[y * rb + (x >> 3)] & (0x80 >> (x & 7))) ? 0xff : 0;
            }
            invalidate(env, p);
            break;
        }
        default: continue;
        }
        n++;
    }
    return n;
}

bool get_attr(env_t *env, pic_inst *p, u32 id, u32 *st)
{
    switch (id) {
    case PIC_PDTA_ModeID: *st = p->modeid; return true;
    case PIC_PDTA_BitMapHeader: *st = reinterpret_cast<u32>(&p->bmhd); return true;
    case PIC_PDTA_ColorRegisters: *st = reinterpret_cast<u32>(p->cmap); return true;
    case PIC_PDTA_CRegs: *st = reinterpret_cast<u32>(p->cregs); return true;
    case PIC_PDTA_GRegs: *st = reinterpret_cast<u32>(p->gregs); return true;
    case PIC_PDTA_ColorTable: *st = reinterpret_cast<u32>(p->colortable); return true;
    case PIC_PDTA_ColorTable2: *st = reinterpret_cast<u32>(p->colortable2); return true;
    case PIC_PDTA_Allocated: *st = reinterpret_cast<u32>(p->allocated); return true;
    case PIC_PDTA_NumColors: *st = p->numcolors; return true;
    case PIC_PDTA_NumAlloc: *st = p->numalloc; return true;
    case PIC_PDTA_Remap: *st = (p->flags & PIC_F_REMAP) ? 1 : 0; return true;
    case PIC_PDTA_Screen: *st = reinterpret_cast<u32>(p->screen); return true;
    case PIC_PDTA_FreeSourceBitMap: *st = (p->flags & PIC_F_FREESRC) ? 1 : 0; return true;
    case PIC_PDTA_UseFriendBitMap: *st = (p->flags & PIC_F_FRIEND) ? 1 : 0; return true;
    case PIC_PDTA_Grab: *st = reinterpret_cast<u32>(&p->grab); return true;
    case PIC_PDTA_DestBitMap: *st = reinterpret_cast<u32>(p->destbm); return true;
    case PIC_PDTA_NumSparse: *st = p->numsparse; return true;
    case PIC_PDTA_SparseTable: *st = reinterpret_cast<u32>(p->sparse); return true;
    case PIC_PDTA_SourceMode: *st = p->sourcemode; return true;
    case PIC_PDTA_DestMode: *st = p->destmode; return true;
    case PIC_PDTA_MaskPlane: *st = reinterpret_cast<u32>(p->mask); return true;
    case PIC_PDTA_WhichPicture: *st = 0; return true;
    case PIC_PDTA_GetNumPictures: *st = 1; return true;
    case PIC_PDTA_BitMap:
    case PIC_PDTA_ClassBitMap:
        /* a V42 subclass's own bitmap, or one made from the pixels
         * (ABI test: ours is made at the first ask, of bmh_Depth planes) */
        if (p->srcbm) {
            *st = reinterpret_cast<u32>(p->srcbm);
            return true;
        }
        if (!p->classbm && p->store == PIC_STORE_LUT8 && X(env)->alloc_bitmap) {
            u32 d = p->bmhd.bmh_Depth >= 1 && p->bmhd.bmh_Depth <= 8 ? p->bmhd.bmh_Depth : 8;
            p->classbm = X(env)->alloc_bitmap(env, p->pw, p->ph, d, nullptr);
            if (p->classbm && X(env)->write_lut8)
                X(env)->write_lut8(env, p->classbm, p->pixels, p->pw, p->ph, p->pw);
        }
        *st = reinterpret_cast<u32>(p->classbm);
        return true;
    }
    return false;
}

/* ---- pixel arrays ----------------------------------------------------------- */

u32 write_pixels(env_t *env, pic_inst *p, pic_pdtBlitPixelArray *m)
{
    u32 fmt = m->pbpa_PixelFormat;
    if (fmt > PIC_PBPAFMT_GREY8 || !m->pbpa_PixelData)
        return 0;
    if (!p->pixels) {
        u32 w = p->bmhd.bmh_Width, h = p->bmhd.bmh_Height;
        if (!w || !h)
            return 0;                   /* the BitMapHeader comes first */
        p->store = (fmt == PIC_PBPAFMT_LUT8 || fmt == PIC_PBPAFMT_GREY8) && p->bmhd.bmh_Depth <= 8
                       ? PIC_STORE_LUT8 : PIC_STORE_ARGB;
        u32 bpp = p->store == PIC_STORE_ARGB ? 4 : 1;
        /* w and h are the BitMapHeader's 16 bits: only the 4 can overflow */
        p->pixels = w * h <= 0xffffffffUL / bpp ? static_cast<u8 *>(env->alloc(env, w * h * bpp)) : nullptr;
        if (!p->pixels) {
            p->store = PIC_STORE_NONE;
            set_err(env, DTC_ERROR_NO_FREE_STORE);
            return 0;
        }
        p->pw = w;
        p->ph = h;
    }
    if (p->srcbm)
        return 0;
    fix_cregs(p);
    /* ABI test: a rectangle partly outside: ours clips it */
    u32 l = m->pbpa_Left, t = m->pbpa_Top;
    if (l >= p->pw || t >= p->ph)
        return 0;
    u32 w = min<u32>(m->pbpa_Width, p->pw - l), h = min<u32>(m->pbpa_Height, p->ph - t);
    const u8 *src = static_cast<const u8 *>(m->pbpa_PixelData);
    u32 bpp = fmt == PIC_PBPAFMT_RGB ? 3 : fmt <= PIC_PBPAFMT_ARGB ? 4 : 1;
    bool same = (p->store == PIC_STORE_ARGB && fmt == PIC_PBPAFMT_ARGB) ||
                (p->store == PIC_STORE_LUT8 && (fmt == PIC_PBPAFMT_LUT8 || fmt == PIC_PBPAFMT_GREY8));
    for (u32 y = 0; y < h; y++) {
        const u8 *s = src + y * m->pbpa_PixelArrayMod;
        if (same) {
            /* the store's own format: the row as it is */
            copy(p->pixels + ((t + y) * p->pw + l) * bpp, s, w * bpp);
            continue;
        }
        for (u32 x = 0; x < w; x++, s += bpp) {
            u32 i = (t + y) * p->pw + l + x;
            u8 a = 0xff, r, g, b;
            switch (fmt) {
            case PIC_PBPAFMT_RGB: r = s[0]; g = s[1]; b = s[2]; break;
            case PIC_PBPAFMT_RGBA: r = s[0]; g = s[1]; b = s[2]; a = s[3]; break;
            case PIC_PBPAFMT_ARGB: a = s[0]; r = s[1]; g = s[2]; b = s[3]; break;
            case PIC_PBPAFMT_GREY8: r = g = b = s[0]; break;
            default: {
                u32 c = s[0];
                r = static_cast<u8>(p->cregs[c * 3] >> 24);
                g = static_cast<u8>(p->cregs[c * 3 + 1] >> 24);
                b = static_cast<u8>(p->cregs[c * 3 + 2] >> 24);
                break;
            }
            }
            if (p->store == PIC_STORE_ARGB) {
                u8 *d = p->pixels + i * 4;
                d[0] = a; d[1] = r; d[2] = g; d[3] = b;
            } else if (fmt == PIC_PBPAFMT_LUT8 || fmt == PIC_PBPAFMT_GREY8) {
                p->pixels[i] = s[0];    /* ABI test: GREY8 into an index picture is the index */
            } else {
                p->pixels[i] = static_cast<u8>(nearest(p->cregs, p->numcolors ? p->numcolors : 1, r, g, b));
            }
        }
    }
    free_classbm(env, p);
    invalidate(env, p);
    return 1;
}

u32 read_pixels(env_t *env, pic_inst *p, pic_pdtBlitPixelArray *m)
{
    u32 fmt = m->pbpa_PixelFormat;
    if (fmt > PIC_PBPAFMT_GREY8 || !m->pbpa_PixelData || !ensure_pixels(env, p))
        return 0;
    if (fmt == PIC_PBPAFMT_LUT8 && p->store != PIC_STORE_LUT8)
        return 0;                       /* ABI test: no indexes of a true colour picture */
    fix_cregs(p);
    u32 l = m->pbpa_Left, t = m->pbpa_Top;
    if (l >= p->pw || t >= p->ph)
        return 0;
    u32 w = min<u32>(m->pbpa_Width, p->pw - l), h = min<u32>(m->pbpa_Height, p->ph - t);
    u8 *dst = static_cast<u8 *>(m->pbpa_PixelData);
    u32 bpp = fmt == PIC_PBPAFMT_RGB ? 3 : fmt <= PIC_PBPAFMT_ARGB ? 4 : 1;
    for (u32 y = 0; y < h; y++) {
        u8 *d = dst + y * m->pbpa_PixelArrayMod;
        for (u32 x = 0; x < w; x++, d += bpp) {
            u32 i = (t + y) * p->pw + l + x;
            if (fmt == PIC_PBPAFMT_LUT8) {
                d[0] = p->pixels[i];
                continue;
            }
            u8 c[4];
            argb_of(p, i, c);
            switch (fmt) {
            case PIC_PBPAFMT_RGB: d[0] = c[1]; d[1] = c[2]; d[2] = c[3]; break;
            case PIC_PBPAFMT_RGBA: d[0] = c[1]; d[1] = c[2]; d[2] = c[3]; d[3] = c[0]; break;
            case PIC_PBPAFMT_ARGB: copy(d, c, 4); break;
            default:                    /* ABI test: OS 3.2's grey weights; ours 77/151/28 of 256 */
                d[0] = static_cast<u8>((c[1] * 77U + c[2] * 151U + c[3] * 28U) >> 8);
                break;
            }
        }
    }
    return 1;
}

/* ---- drawing ---------------------------------------------------------------- */

void gadget_box(void *obj, odt_GadgetInfo *gi, odt_IBox *b)
{
    const odt_Gadget *g = static_cast<const odt_Gadget *>(obj);
    s32 dl = gi ? gi->gi_Domain.Left : 0, dt = gi ? gi->gi_Domain.Top : 0;
    s32 dw = gi ? gi->gi_Domain.Width : 0, dh = gi ? gi->gi_Domain.Height : 0;
    s32 l = g->LeftEdge + dl, t = g->TopEdge + dt, w = g->Width, h = g->Height;
    if (g->Flags & ODT_GFLG_RELRIGHT) l += dw - 1;
    if (g->Flags & ODT_GFLG_RELBOTTOM) t += dh - 1;
    if (g->Flags & ODT_GFLG_RELWIDTH) w += dw;
    if (g->Flags & ODT_GFLG_RELHEIGHT) h += dh;
    b->Left = static_cast<odt_s16>(l);
    b->Top = static_cast<odt_s16>(t);
    b->Width = static_cast<odt_s16>(max<s32>(w, 0));
    b->Height = static_cast<odt_s16>(max<s32>(h, 0));
}

void draw(env_t *env, pic_inst *p, void *rp, s32 x, s32 y, s32 w, s32 h, s32 th, s32 tv)
{
    if (!p->destbm || !rp || !X(env)->blit)
        return;
    th = max<s32>(th, 0);
    tv = max<s32>(tv, 0);
    w = min<s32>(w, static_cast<s32>(p->dw) - th);
    h = min<s32>(h, static_cast<s32>(p->dh) - tv);
    if (w > 0 && h > 0)
        X(env)->blit(env, rp, p->destbm, p->mask, th, tv, x, y, w, h);
}

u32 render(env_t *env, odt_IClass *cl, void *obj, pic_inst *p, odt_gpRender *m)
{
    if (!(p->flags & PIC_F_LAIDOUT) || !p->destbm)
        return 0;
    odt_IBox b;
    gadget_box(obj, m->gpr_GInfo, &b);
    u32 th = 0, tv = 0;
    dtc_get_super(env, cl, obj, ODT_DTA_TopHoriz, &th);
    dtc_get_super(env, cl, obj, ODT_DTA_TopVert, &tv);
    draw(env, p, m->gpr_RPort, b.Left, b.Top, b.Width, b.Height, static_cast<s32>(th), static_cast<s32>(tv));
    return 1;
}

u32 frame_box(pic_inst *p, odt_dtFrameBox *m)
{
    odt_FrameInfo *f = m->dtf_FrameInfo;
    if (!f || m->dtf_SizeFrameInfo < sizeof(odt_FrameInfo))
        return 0;
    zero(f, sizeof *f);
    /* ABI test: the DIPF_ OS 3.2 gives; ours from the mode's HAM, EHB and LACE bits */
    u32 mode = p->modeid == PIC_INVALID_ID ? 0 : p->modeid;
    if (mode & PIC_MODE_LACE) f->fri_PropertyFlags |= 0x00000001UL;  /* DIPF_IS_LACE */
    if (mode & PIC_MODE_HAM) f->fri_PropertyFlags |= 0x00000008UL;   /* DIPF_IS_HAM */
    if (mode & PIC_MODE_EHB) f->fri_PropertyFlags |= 0x00001000UL;   /* DIPF_IS_EXTRAHALFBRITE */
    f->fri_Resolution.x = p->bmhd.bmh_XAspect;
    f->fri_Resolution.y = p->bmhd.bmh_YAspect;
    f->fri_RedBits = f->fri_GreenBits = f->fri_BlueBits = 8;
    f->fri_Dimensions.Width = p->bmhd.bmh_Width;
    f->fri_Dimensions.Height = p->bmhd.bmh_Height;
    f->fri_Dimensions.Depth = p->bmhd.bmh_Depth;
    f->fri_Flags = ODT_FIF_REMAPPABLE | ODT_FIF_SCALABLE;
    return 1;
}

} // namespace

/* ---- pure helpers --------------------------------------------------------------- */

extern "C" void pic_planar_to_chunky(const odt_BitMap *bm, odt_u32 w, odt_u32 h, odt_u8 *out)
{
    u32 depth = min<u32>(bm->Depth, 8);
    const u8 *rows[8];
    for (u32 y = 0; y < h; y++) {
        for (u32 d = 0; d < depth; d++)
            rows[d] = bm->Planes[d] ? bm->Planes[d] + y * bm->BytesPerRow : nullptr;
        odt_planar_row(rows, depth, w, out + y * w, 1);  /* 8 pixels at a time */
    }
}

extern "C" void pic_chunky_to_planar(const odt_u8 *in, odt_u32 w, odt_u32 h, odt_u32 depth, odt_u8 *const *planes,
                                     odt_u32 bpr)
{
    for (u32 d = 0; d < depth; d++) {
        if (!planes[d])
            continue;
        for (u32 y = 0; y < h; y++) {
            u8 *row = planes[d] + y * bpr;
            for (u32 x = 0; x < bpr; x++)
                row[x] = 0;
            for (u32 x = 0; x < w; x++)
                if (in[y * w + x] & (1U << d))
                    row[x >> 3] |= static_cast<u8>(0x80 >> (x & 7));
        }
    }
}

extern "C" odt_u32 pic_byterun1_pack(const odt_u8 *in, odt_u32 n, odt_u8 *out)
{
    u32 o = 0, i = 0;
    while (i < n) {
        u32 r = 1;
        while (i + r < n && r < 128 && in[i + r] == in[i])
            r++;
        if (r >= 3) {
            out[o++] = static_cast<u8>(257 - r);
            out[o++] = in[i];
            i += r;
            continue;
        }
        u32 s = i, l = 0;
        while (i < n && l < 128) {
            if (l && i + 2 < n && in[i] == in[i + 1] && in[i] == in[i + 2])
                break;
            i++;
            l++;
        }
        out[o++] = static_cast<u8>(l - 1);
        for (u32 k = 0; k < l; k++)
            out[o++] = in[s + k];
    }
    return o;
}

/* ILBM of the picture: BMHD, CMAP and CAMG for an index picture of up to 8
 * planes, a deep ILBM of 24 planes (no CMAP) for a true colour one; the
 * BODY packed with ByteRun1, a plane's row at a time. */
extern "C" int pic_write_ilbm(const odt_cenv *env, pic_inst *p, odt_u32 fh)
{
    if (!ensure_pixels(env, p) || !env->write) {
        set_err(env, DTC_ERROR_REQUIRED_ARG_MISSING);
        return 0;
    }
    fix_cregs(p);
    u32 w = p->pw, h = p->ph;
    bool lut = p->store == PIC_STORE_LUT8;
    u32 ncol = 0, depth;
    if (lut) {
        depth = p->bmhd.bmh_Depth;
        if (depth < 1 || depth > 8) {
            depth = 1;
            while (depth < 8 && (1U << depth) < p->numcolors)
                depth++;
        }
        ncol = p->numcolors ? p->numcolors : 1U << depth;
    } else {
        depth = 24;
    }
    u32 rb = pic_rowbytes(w);
    u32 bodymax = h * depth * (rb + rb / 128 + 2);
    u32 head = 12 + 28 + (lut ? 8 + ((ncol * 3 + 1) & ~1U) : 0) + 12 + 8;
    u8 *buf = static_cast<u8 *>(env->alloc(env, head + bodymax + rb + 2));
    if (!buf) {
        set_err(env, DTC_ERROR_NO_FREE_STORE);
        return 0;
    }
    u8 *row = buf + head + bodymax;
    u32 o = 12;
    dtc_put32(buf, ODT_ID('F', 'O', 'R', 'M'));
    dtc_put32(buf + 8, ODT_ID('I', 'L', 'B', 'M'));
    dtc_put32(buf + o, ODT_ID('B', 'M', 'H', 'D'));
    dtc_put32(buf + o + 4, 20);
    u8 *b = buf + o + 8;
    dtc_put16(b, w);
    dtc_put16(b + 2, h);
    dtc_put16(b + 4, static_cast<u16>(p->bmhd.bmh_Left));
    dtc_put16(b + 6, static_cast<u16>(p->bmhd.bmh_Top));
    b[8] = static_cast<u8>(depth);
    b[9] = lut && p->bmhd.bmh_Masking == PIC_mskHasTransparentColor ? PIC_mskHasTransparentColor : PIC_mskNone;
    b[10] = PIC_cmpByteRun1;
    b[11] = PIC_BMHDF_CMAPOK;           /* the CMAP has 8-bit guns: no 4-bit guess on reading */
    dtc_put16(b + 12, b[9] ? p->bmhd.bmh_Transparent : 0);
    b[14] = p->bmhd.bmh_XAspect ? p->bmhd.bmh_XAspect : 1; /* ABI test: OS 3.2's aspect when there is none */
    b[15] = p->bmhd.bmh_YAspect ? p->bmhd.bmh_YAspect : 1;
    dtc_put16(b + 16, p->bmhd.bmh_PageWidth > 0 ? static_cast<u32>(p->bmhd.bmh_PageWidth) : w);
    dtc_put16(b + 18, p->bmhd.bmh_PageHeight > 0 ? static_cast<u32>(p->bmhd.bmh_PageHeight) : h);
    o += 28;
    if (lut) {
        dtc_put32(buf + o, ODT_ID('C', 'M', 'A', 'P'));
        dtc_put32(buf + o + 4, ncol * 3);
        for (u32 c = 0; c < ncol; c++)
            for (u32 k = 0; k < 3; k++)
                buf[o + 8 + c * 3 + k] = static_cast<u8>(p->cregs[c * 3 + k] >> 24);
        o += 8 + ncol * 3;
        if (o & 1)
            buf[o++] = 0;
        if (p->modeid != PIC_INVALID_ID) {
            dtc_put32(buf + o, ODT_ID('C', 'A', 'M', 'G'));
            dtc_put32(buf + o + 4, 4);
            dtc_put32(buf + o + 8, p->modeid & ~PIC_MODE_HAM); /* index pixels are never HAM */
            o += 12;
        }
    }
    u32 body = o;
    dtc_put32(buf + o, ODT_ID('B', 'O', 'D', 'Y'));
    o += 8;
    for (u32 y = 0; y < h; y++)
        for (u32 d = 0; d < depth; d++) {
            for (u32 x = 0; x < rb; x++)
                row[x] = 0;
            for (u32 x = 0; x < w; x++) {
                u32 v;
                if (lut)
                    v = p->pixels[y * w + x] >> d;
                else
                    v = p->pixels[(y * w + x) * 4 + 1 + d / 8] >> (d & 7);
                if (v & 1)
                    row[x >> 3] |= static_cast<u8>(0x80 >> (x & 7));
            }
            o += pic_byterun1_pack(row, rb, buf + o);
        }
    dtc_put32(buf + body + 4, o - body - 8);
    if (o & 1)
        buf[o++] = 0;
    dtc_put32(buf + 4, o - 8);
    s32 n = env->write(env, fh, buf, static_cast<s32>(o));
    env->free(env, buf);
    if (n != static_cast<s32>(o)) {
        set_err(env, ODT_DTERROR_COULDNT_SAVE);
        return 0;
    }
    return 1;
}

extern "C" odt_u32 pic_inst_size(void) { return sizeof(pic_inst); }

extern "C" odt_u32 pic_dispatch(const odt_cenv *env, odt_IClass *cl, void *obj, void *msg)
{
    switch (method(msg)) {
    case ODT_OM_NEW: {
        void *o = reinterpret_cast<void *>(env->do_super(env, cl, obj, msg));
        if (!o)
            return 0;
        pic_inst *p = inst(cl, o);
        p->flags = PIC_F_REMAP | PIC_F_FRIEND; /* PDTA_UseFriendBitMap defaults to TRUE (V43 autodoc) */
        p->modeid = PIC_INVALID_ID;     /* ABI test: OS 3.2's ModeID before a subclass sets one */
        p->precision = PIC_PRECISION_IMAGE;
        p->destmode = PIC_PMODE_V42;    /* ABI test: OS 3.2's default PDTA_DestMode */
        p->sourcemode = PIC_PMODE_V42;
        set_attrs(env, p, static_cast<odt_opSet *>(msg)->ops_AttrList);
        return reinterpret_cast<u32>(o);
    }
    case ODT_OM_DISPOSE:
        free_all(env, inst(cl, obj));
        return env->do_super(env, cl, obj, msg);
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        u32 r = env->do_super(env, cl, obj, msg);
        return r + set_attrs(env, inst(cl, obj), static_cast<odt_opSet *>(msg)->ops_AttrList);
    }
    case ODT_OM_GET: {
        odt_opGet *g = static_cast<odt_opGet *>(msg);
        if (get_attr(env, inst(cl, obj), g->opg_AttrID, g->opg_Storage))
            return 1;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_DTM_PROCLAYOUT:
    case ODT_DTM_ASYNCLAYOUT: {
        /* datatypesclass's part first (the domain, the scroll numbers),
         * then the picture made for the screen */
        env->do_super(env, cl, obj, msg);
        odt_gpLayout *l = static_cast<odt_gpLayout *>(msg);
        return layout(env, cl, obj, inst(cl, obj), l->gpl_GInfo) ? 1 : 0;
    }
    case ODT_GM_RENDER:
        return render(env, cl, obj, inst(cl, obj), static_cast<odt_gpRender *>(msg));
    case ODT_DTM_OBTAINDRAWINFO: {
        /* si_Lock held until DTM_RELEASEDRAWINFO: no layout (which takes it)
         * replaces the destination while the program draws */
        pic_inst *p = inst(cl, obj);
        odt_DTSpecialInfo *si = static_cast<odt_DTSpecialInfo *>(static_cast<odt_Gadget *>(obj)->SpecialInfo);
        bool lock = si && env->sem_obtain && env->sem_release;
        if (lock)
            env->sem_obtain(env, &si->si_Lock);
        set_attrs(env, p, static_cast<odt_opSet *>(msg)->ops_AttrList);
        if (!layout(env, cl, obj, p, nullptr)) {
            if (lock)
                env->sem_release(env, &si->si_Lock);
            return 0;
        }
        if (lock)
            p->draw_holds++;
        return reinterpret_cast<u32>(obj);
    }
    case ODT_DTM_DRAW: {
        odt_dtDraw *d = static_cast<odt_dtDraw *>(msg);
        pic_inst *p = inst(cl, obj);
        if (!p->destbm)
            return 0;
        draw(env, p, d->dtd_RPort, d->dtd_Left, d->dtd_Top, d->dtd_Width, d->dtd_Height, d->dtd_TopHoriz,
             d->dtd_TopVert);
        return 1;
    }
    case ODT_DTM_RELEASEDRAWINFO: {
        pic_inst *p = inst(cl, obj);
        odt_DTSpecialInfo *si = static_cast<odt_DTSpecialInfo *>(static_cast<odt_Gadget *>(obj)->SpecialInfo);
        /* only a handle ObtainDTDrawInfo gave, and only as often */
        if (si && p->draw_holds && static_cast<odt_dtReleaseDrawInfo *>(msg)->dtr_Handle == obj) {
            p->draw_holds--;
            env->sem_release(env, &si->si_Lock);
        }
        return 0;
    }
    case ODT_DTM_FRAMEBOX:
        return frame_box(inst(cl, obj), static_cast<odt_dtFrameBox *>(msg));
    case ODT_DTM_WRITE: {
        odt_dtWrite *w = static_cast<odt_dtWrite *>(msg);
        if (w->dtw_Mode != ODT_DTWM_IFF) {
            set_err(env, DTC_ERROR_NOT_IMPLEMENTED); /* ABI test: picture.datatype's own DTWM_RAW */
            return 0;
        }
        return pic_write_ilbm(env, inst(cl, obj), w->dtw_FileHandle) ? 1 : 0;
    }
    case PIC_PDTM_WRITEPIXELARRAY:
        return write_pixels(env, inst(cl, obj), static_cast<pic_pdtBlitPixelArray *>(msg));
    case PIC_PDTM_READPIXELARRAY:
        return read_pixels(env, inst(cl, obj), static_cast<pic_pdtBlitPixelArray *>(msg));
    case PIC_PDTM_SCALE: {
        pic_inst *p = inst(cl, obj);
        pic_pdtScale *s = static_cast<pic_pdtScale *>(msg);
        u32 w = s->ps_NewWidth, h = s->ps_NewHeight, sw = p->bmhd.bmh_Width, sh = p->bmhd.bmh_Height;
        /* a BitMap's sizes are 16 bits: larger would overflow the pixels' sizes */
        if (!w || !h || !sw || !sh || w > 0xffffUL || h > 0xffffUL)
            return 0;
        if (s->ps_Flags & PIC_PScale_KeepAspect) {
            if (w * sh > h * sw)
                w = max<u32>(h * sw / sh, 1);
            else
                h = max<u32>(w * sh / sw, 1);
        }
        p->scale_w = w;
        p->scale_h = h;
        p->scale_flags = s->ps_Flags;
        invalidate(env, p);
        return 1;
    }
    }
    return env->do_super(env, cl, obj, msg);
}
