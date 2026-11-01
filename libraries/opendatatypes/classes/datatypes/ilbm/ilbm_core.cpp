/* GFOpenILBM: ilbm.datatype's class, lean C++ with no Amiga headers.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room, from the IFF ILBM specification and its updates (EA and
 * Commodore, public), the picture_dtc and datatypes autodocs and the NDK
 * 3.2 headers' layouts; never from Kickstart, Hyperion or AROS code, nor
 * any third-party datatype's.
 *
 * At OM_NEW, after picture.datatype and datatypesclass have made the
 * object, the class parses the IFFHandle datatypesclass opened (DTA_Handle)
 * with iffparse through the env: BMHD, CMAP, CAMG, GRAB and the text
 * chunks as properties, stopping at the BODY. It fills the picture's
 * BitMapHeader and colours and hands the pixels over a row at a time with
 * PDTM_WRITEPIXELARRAY (V43: LUT8 for index pictures, ARGB for HAM and
 * deep ILBMs), and a BODY mask plane with PDTA_MaskPlane. DTM_WRITE is
 * picture.datatype's ILBM writer for both DTWM_IFF and DTWM_RAW.
 *
 * "ABI test" marks a corner the specification leaves open; the ABI test's
 * PROBE line records what OS 3.2's ilbm.datatype does there.
 */
#include "ilbm_core.h"
#include "../../../../../common/include/odt_planar.h"

namespace {

using u8 = odt_u8;
using u16 = odt_u16;
using u32 = odt_u32;
using s32 = odt_s32;
using env_t = const odt_cenv;

template <class T> constexpr T min(T a, T b) { return a < b ? a : b; }

inline u32 method(void *msg) { return *static_cast<u32 *>(msg); }

void set_err(env_t *env, s32 e)
{
    if (env->set_io_err)
        env->set_io_err(env, e);
}

const u32 props[] = { ILBM_ID_BMHD, ILBM_ID_CMAP, ILBM_ID_CAMG, ILBM_ID_GRAB,
                      ILBM_ID_NAME, ILBM_ID_AUTH, ILBM_ID_ANNO, ILBM_ID_COPY };

/* A text chunk as an attribute: datatypesclass keeps its own copy */
void set_text(env_t *env, odt_IClass *cl, void *obj, void *iff, u32 type, u32 id, u32 attr)
{
    s32 n = 0;
    const char *s = static_cast<const char *>(env->iff_find_prop(env, iff, type, id, &n));
    if (!s || n <= 0)
        return;
    char *c = static_cast<char *>(env->alloc(env, static_cast<u32>(n) + 1));
    if (!c)
        return;
    for (s32 i = 0; i < n; i++)
        c[i] = s[i];
    odt_TagItem t[] = { { attr, reinterpret_cast<u32>(c) }, { ODT_TAG_DONE, 0 } };
    dtc_set_super(env, cl, obj, t);
    env->free(env, c);
}

/* The ILBM's ModeID: the CAMG as the ILBM updates say to read it, or one
 * guessed from the size when there is none */
u32 mode_of(const u8 *camg, s32 camg_len, u32 w, u32 h, u32 depth, u32 ncmap)
{
    u32 m;
    if (camg && camg_len >= 4) {
        m = dtc_be32(camg);
        if (!(m & 0xffff0000UL))
            m &= ~(ILBM_GENLOCK_VIDEO | ILBM_GENLOCK_AUDIO | ILBM_VP_HIDE | ILBM_SPRITES);
    } else {
        /* ABI test: OS 3.2's mode for an ILBM without CAMG */
        m = (w >= 640 ? PIC_MODE_HIRES : 0) | (h >= 400 ? PIC_MODE_LACE : 0);
        if (depth == 6 && ncmap == 32)
            m |= PIC_MODE_EHB;
    }
    if ((m & PIC_MODE_HAM) && depth != 6 && depth != 8)
        m &= ~PIC_MODE_HAM;             /* ABI test: HAM of another depth is taken as indexes */
    if ((m & PIC_MODE_EHB) && depth != 6)
        m &= ~PIC_MODE_EHB;
    return m;
}

u32 load(env_t *env, odt_IClass *cl, void *obj)
{
    u32 st = 0, handle = 0;
    dtc_get_super(env, cl, obj, ODT_DTA_SourceType, &st);
    dtc_get_super(env, cl, obj, ODT_DTA_Handle, &handle);
    if (st == ODT_DTST_RAM)
        return 0;                       /* a new, empty picture */
    void *iff = reinterpret_cast<void *>(handle);
    if (!iff || !env->iff_parse || !env->iff_prop || !env->iff_stop || !env->iff_find_prop || !env->iff_current ||
        !env->iff_read_chunk)
        return DTC_ERROR_REQUIRED_ARG_MISSING;
    for (u32 t = 0; t < 2; t++) {
        u32 form = t ? ILBM_ID_PBM : ILBM_ID_ILBM;
        for (u32 id : props)
            env->iff_prop(env, iff, form, id);
        env->iff_stop(env, iff, form, ILBM_ID_BODY);
    }
    s32 r = env->iff_parse(env, iff, ILBM_IFFPARSE_SCAN);
    if (r)
        return r == ILBM_IFFERR_EOF ? ODT_DTERROR_NOT_ENOUGH_DATA : ODT_DTERROR_INVALID_DATA;
    u32 type = 0;
    s32 bodylen = 0;
    if (env->iff_current(env, iff, &type, &bodylen) != ILBM_ID_BODY)
        return ODT_DTERROR_NOT_ENOUGH_DATA;
    bool pbm = type == ILBM_ID_PBM;

    s32 n = 0;
    const u8 *bh = static_cast<const u8 *>(env->iff_find_prop(env, iff, type, ILBM_ID_BMHD, &n));
    if (!bh || n < 20)
        return ODT_DTERROR_INVALID_DATA; /* ABI test: OS 3.2's error for a missing BMHD */
    u32 w = dtc_be16(bh), h = dtc_be16(bh + 2), depth = bh[8], masking = bh[9], cmp = bh[10];
    if (cmp > PIC_cmpByteRun1)
        return ODT_DTERROR_UNKNOWN_COMPRESSION;
    if (!w || !h || !depth || (pbm ? depth != 8 : depth > 8 && depth != 24 && depth != 32))
        return ODT_DTERROR_INVALID_DATA; /* ABI test: the deep ILBMs OS 3.2 takes */

    s32 cmaplen = 0, camglen = 0, grablen = 0;
    const u8 *cmap = static_cast<const u8 *>(env->iff_find_prop(env, iff, type, ILBM_ID_CMAP, &cmaplen));
    const u8 *camg = static_cast<const u8 *>(env->iff_find_prop(env, iff, type, ILBM_ID_CAMG, &camglen));
    const u8 *grab = static_cast<const u8 *>(env->iff_find_prop(env, iff, type, ILBM_ID_GRAB, &grablen));
    u32 ncmap = cmap && cmaplen > 0 ? min<u32>(static_cast<u32>(cmaplen) / 3, 256) : 0;
    u32 mode = mode_of(camg, camglen, w, h, depth, ncmap);
    bool ham = (mode & PIC_MODE_HAM) != 0, ehb = (mode & PIC_MODE_EHB) != 0;
    bool deep = depth > 8;

    /* the picture's header and colours */
    u32 bmp = 0, cregp = 0, cmapp = 0;
    dtc_get_super(env, cl, obj, PIC_PDTA_BitMapHeader, &bmp);
    dtc_get_super(env, cl, obj, PIC_PDTA_CRegs, &cregp);
    dtc_get_super(env, cl, obj, PIC_PDTA_ColorRegisters, &cmapp);
    if (!bmp || !cregp || !cmapp)
        return DTC_ERROR_OBJECT_WRONG_TYPE;
    pic_BitMapHeader *bmhd = reinterpret_cast<pic_BitMapHeader *>(bmp);
    bmhd->bmh_Width = static_cast<u16>(w);
    bmhd->bmh_Height = static_cast<u16>(h);
    bmhd->bmh_Left = static_cast<odt_s16>(dtc_be16(bh + 4));
    bmhd->bmh_Top = static_cast<odt_s16>(dtc_be16(bh + 6));
    bmhd->bmh_Depth = static_cast<u8>(depth);
    bmhd->bmh_Masking = static_cast<u8>(depth == 32 ? PIC_mskHasAlpha : masking); /* ABI test: 32 planes */
    bmhd->bmh_Compression = static_cast<u8>(cmp);
    bmhd->bmh_Pad = bh[11];
    bmhd->bmh_Transparent = dtc_be16(bh + 12);
    bmhd->bmh_XAspect = bh[14];
    bmhd->bmh_YAspect = bh[15];
    bmhd->bmh_PageWidth = static_cast<odt_s16>(dtc_be16(bh + 16));
    bmhd->bmh_PageHeight = static_cast<odt_s16>(dtc_be16(bh + 18));

    /* the colours: a CMAP of 4-bit guns (low nibbles all 0, BMHDF_CMAPOK
     * not set) is scaled to 8 bits, as the ILBM update says; EHB's upper
     * half is the lower at half brightness; ABI test: a CMAP too short
     * leaves black, and no CMAP at all gives a grey ramp */
    u32 ncol = deep ? 0 : ham ? (depth == 6 ? 16 : 64) : ehb ? 64 : 1U << depth;
    u32 *cregs = reinterpret_cast<u32 *>(cregp);
    pic_ColorRegister *cr = reinterpret_cast<pic_ColorRegister *>(cmapp);
    bool scale4 = !(bh[11] & PIC_BMHDF_CMAPOK) && ncmap > 0;
    for (u32 i = 0; i < ncmap * 3 && scale4; i++)
        if (cmap[i] & 0x0f)
            scale4 = false;
    u8 pal[256 * 3];
    for (u32 i = 0; i < 256 * 3; i++)
        pal[i] = 0;
    if (!ncmap && !deep && ncol > 1) {
        for (u32 i = 0; i < ncol; i++)
            pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = static_cast<u8>(i * 255 / (ncol - 1));
    }
    for (u32 i = 0; i < ncmap * 3; i++)
        pal[i] = static_cast<u8>(scale4 ? cmap[i] | cmap[i] >> 4 : cmap[i]);
    if (ehb)
        for (u32 i = 32 * 3; i < 64 * 3; i++)
            pal[i] = static_cast<u8>(pal[i - 32 * 3] >> 1);
    for (u32 i = 0; i < ncol; i++) {
        cr[i].red = pal[i * 3];
        cr[i].green = pal[i * 3 + 1];
        cr[i].blue = pal[i * 3 + 2];
        for (u32 k = 0; k < 3; k++)
            cregs[i * 3 + k] = pal[i * 3 + k] * 0x01010101UL;
    }
    {
        odt_TagItem t[] = { { PIC_PDTA_NumColors, ncol }, { PIC_PDTA_ModeID, mode },
                            { PIC_PDTA_SourceMode, PIC_PMODE_V43 }, { ODT_DTA_NominalHoriz, w },
                            { ODT_DTA_NominalVert, h }, { ODT_TAG_DONE, 0 } };
        dtc_set_super(env, cl, obj, t);
    }
    if (grab && grablen >= 4) {
        odt_Point pt = { static_cast<odt_s16>(dtc_be16(grab)), static_cast<odt_s16>(dtc_be16(grab + 2)) };
        odt_TagItem t[] = { { PIC_PDTA_Grab, reinterpret_cast<u32>(&pt) }, { ODT_TAG_DONE, 0 } };
        dtc_set_super(env, cl, obj, t);
    }
    set_text(env, cl, obj, iff, type, ILBM_ID_NAME, ODT_DTA_ObjName);
    set_text(env, cl, obj, iff, type, ILBM_ID_AUTH, ODT_DTA_ObjAuthor);
    set_text(env, cl, obj, iff, type, ILBM_ID_ANNO, ODT_DTA_ObjAnnotation);
    set_text(env, cl, obj, iff, type, ILBM_ID_COPY, ODT_DTA_ObjCopyright);

    /* the BODY, unpacked whole */
    u32 rb = pic_rowbytes(w);
    bool hasmask = !pbm && masking == PIC_mskHasMask;
    u32 rowsize = pbm ? w + (w & 1) : rb * (depth + (hasmask ? 1 : 0));
    /* 16-bit sizes and up to 32 planes can overflow 32 bits: no such picture */
    if (h && rowsize > 0xffffffffUL / h)
        return DTC_ERROR_NO_FREE_STORE;
    u32 rawlen = rowsize * h;
    u32 blen = bodylen > 0 ? static_cast<u32>(bodylen) : 0;
    u8 *body = static_cast<u8 *>(env->alloc(env, blen + 1));
    u8 *raw = static_cast<u8 *>(env->alloc(env, rawlen));
    u8 *row = static_cast<u8 *>(env->alloc(env, w * 5)); /* a row of ARGB, then HAM's pixels */
    u8 *mask = hasmask ? static_cast<u8 *>(env->alloc(env, rb * h)) : nullptr;
    u32 err = 0;
    if (!body || !raw || !row || (hasmask && !mask)) {
        err = DTC_ERROR_NO_FREE_STORE;
        goto out;
    }
    {
        s32 got = env->iff_read_chunk(env, iff, body, static_cast<s32>(blen));
        if (got < 0) {
            err = ODT_DTERROR_NOT_ENOUGH_DATA;
            goto out;
        }
        /* ABI test: a BODY shorter than the picture: ours leaves the rest 0 */
        if (cmp == PIC_cmpByteRun1)
            ilbm_byterun1_unpack(body, static_cast<u32>(got), raw, rawlen);
        else
            for (u32 i = 0; i < rawlen && i < static_cast<u32>(got); i++)
                raw[i] = body[i];
    }
    for (u32 y = 0; y < h; y++) {
        const u8 *src = raw + y * rowsize;
        u8 hr = 0, hg = 0, hb = 0;      /* HAM: each row starts from colour 0 */
        if (ham) {
            hr = pal[0];
            hg = pal[1];
            hb = pal[2];
        }
        /* the planes' bits as one byte a pixel, eight pixels at a time: a
         * deep ILBM's planes 0-7 are red, 8-15 green, 16-23 blue and 24-31
         * alpha; HAM's pixels go after the ARGB row, the others straight in */
        const u8 *pix = src;            /* PBM: a byte a pixel already */
        if (!pbm) {
            const u8 *planes[8];
            if (deep) {
                for (u32 g = 0; g < depth / 8; g++) {
                    for (u32 d = 0; d < 8; d++)
                        planes[d] = src + (g * 8 + d) * rb;
                    odt_planar_row(planes, 8, w, row + (g == 3 ? 0 : g + 1), 4);
                }
                if (depth == 24)
                    for (u32 x = 0; x < w; x++)
                        row[x * 4] = 0xff;
            } else {
                u8 *to = ham ? row + w * 4 : row;
                for (u32 d = 0; d < depth; d++)
                    planes[d] = src + d * rb;
                odt_planar_row(planes, depth, w, to, 1);
                pix = to;
            }
        }
        if (ham || pbm)                 /* never deep: HAM is 6 or 8 planes, PBM 8 */
            for (u32 x = 0; x < w; x++) {
                u32 v = pix[x];
                if (!ham) {
                    row[x] = static_cast<u8>(v);
                    continue;
                }
                u32 sh = depth - 2, ctl = v >> sh, d = v & ((1U << sh) - 1);
                if (ctl == 0) {
                    hr = pal[d * 3];
                    hg = pal[d * 3 + 1];
                    hb = pal[d * 3 + 2];
                } else {
                    /* HAM6: the 4 bits are the gun's, doubled up (ABI test);
                     * HAM8: the 6 bits are its upper six, the lower two kept */
                    u8 *gun = ctl == 1 ? &hb : ctl == 2 ? &hr : &hg;
                    *gun = static_cast<u8>(depth == 6 ? d * 17 : (d << 2) | (*gun & 3));
                }
                u8 *o = row + x * 4;
                o[0] = 0xff;
                o[1] = hr;
                o[2] = hg;
                o[3] = hb;
            }
        if (hasmask)
            for (u32 i = 0; i < rb; i++)
                mask[y * rb + i] = src[depth * rb + i];
        pic_pdtBlitPixelArray m = { PIC_PDTM_WRITEPIXELARRAY, row,
                                    static_cast<u32>(deep || ham ? PIC_PBPAFMT_ARGB : PIC_PBPAFMT_LUT8),
                                    w * 4, 0, y, w, 1 };
        if (!env->do_super(env, cl, obj, &m)) {
            err = DTC_ERROR_NO_FREE_STORE;
            goto out;
        }
    }
    if (hasmask) {
        odt_TagItem t[] = { { PIC_PDTA_MaskPlane, reinterpret_cast<u32>(mask) }, { ODT_TAG_DONE, 0 } };
        dtc_set_super(env, cl, obj, t);
    }
out:
    env->free(env, body);
    env->free(env, raw);
    env->free(env, row);
    env->free(env, mask);
    return err;
}

} // namespace

extern "C" odt_u32 ilbm_byterun1_unpack(const odt_u8 *in, odt_u32 len, odt_u8 *out, odt_u32 n)
{
    u32 i = 0, o = 0;
    while (o < n && i < len) {
        odt_s8 c = static_cast<odt_s8>(in[i++]);
        if (c >= 0) {
            for (s32 k = 0; k <= c && o < n && i < len; k++)
                out[o++] = in[i++];
        } else if (c != -128) {
            if (i >= len)
                break;
            u8 v = in[i++];
            for (s32 k = 0; k <= -c && o < n; k++)
                out[o++] = v;
        }
    }
    return i;
}

extern "C" odt_u32 ilbm_inst_size(void) { return 0; }

extern "C" odt_u32 ilbm_dispatch(const odt_cenv *env, odt_IClass *cl, void *obj, void *msg)
{
    switch (method(msg)) {
    case ODT_OM_NEW: {
        void *o = reinterpret_cast<void *>(env->do_super(env, cl, obj, msg));
        if (!o)
            return 0;
        u32 err = load(env, cl, o);
        if (err) {
            odt_u32 dispose = ODT_OM_DISPOSE;
            env->coerce(env, cl, o, &dispose);
            set_err(env, static_cast<s32>(err));
            return 0;
        }
        return reinterpret_cast<u32>(o);
    }
    case ODT_DTM_WRITE: {
        /* ILBM is the raw form too: picture.datatype writes it */
        odt_dtWrite w = *static_cast<odt_dtWrite *>(msg);
        w.dtw_Mode = ODT_DTWM_IFF;
        return env->do_super(env, cl, obj, &w);
    }
    }
    return env->do_super(env, cl, obj, msg);
}
