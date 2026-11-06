/* GFOpenPicture: picture.datatype's class (V43 and up) as a core with no
 * Amiga headers, over struct odt_cenv and struct dtc_ext.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the picture_dtc autodoc, datatypes/pictureclass.h's
 * published layouts (NDK 3.2), the IFF ILBM specification (EA, public)
 * and OS 3.2's answers measured by tests/PictureDTClassABITest; never from
 * Kickstart, Hyperion or AROS code.
 */
#ifndef OPENDATATYPES_PICTURE_CORE_H
#define OPENDATATYPES_PICTURE_CORE_H

#include "dtc_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- datatypes/pictureclass.h -------------------------------------------------- */

#define PIC_PDTA_Dummy           (ODT_DTA_Dummy + 199)   /* PDTA_ModeID is DTA_Dummy + 200 */
#define PIC_PDTA_ModeID          (PIC_PDTA_Dummy + 1)
#define PIC_PDTA_BitMapHeader    (PIC_PDTA_Dummy + 2)
#define PIC_PDTA_BitMap          (PIC_PDTA_Dummy + 3)
#define PIC_PDTA_ColorRegisters  (PIC_PDTA_Dummy + 4)
#define PIC_PDTA_CRegs           (PIC_PDTA_Dummy + 5)
#define PIC_PDTA_GRegs           (PIC_PDTA_Dummy + 6)
#define PIC_PDTA_ColorTable      (PIC_PDTA_Dummy + 7)
#define PIC_PDTA_ColorTable2     (PIC_PDTA_Dummy + 8)
#define PIC_PDTA_Allocated       (PIC_PDTA_Dummy + 9)
#define PIC_PDTA_NumColors       (PIC_PDTA_Dummy + 10)
#define PIC_PDTA_NumAlloc        (PIC_PDTA_Dummy + 11)
#define PIC_PDTA_Remap           (PIC_PDTA_Dummy + 12)
#define PIC_PDTA_Screen          (PIC_PDTA_Dummy + 13)
#define PIC_PDTA_FreeSourceBitMap (PIC_PDTA_Dummy + 14)
#define PIC_PDTA_Grab            (PIC_PDTA_Dummy + 15)
#define PIC_PDTA_DestBitMap      (PIC_PDTA_Dummy + 16)
#define PIC_PDTA_ClassBitMap     (PIC_PDTA_Dummy + 17)
#define PIC_PDTA_NumSparse       (PIC_PDTA_Dummy + 18)
#define PIC_PDTA_SparseTable     (PIC_PDTA_Dummy + 19)
#define PIC_PDTA_WhichPicture    (PIC_PDTA_Dummy + 20)
#define PIC_PDTA_GetNumPictures  (PIC_PDTA_Dummy + 21)
#define PIC_PDTA_MaxDitherPens   (PIC_PDTA_Dummy + 22)
#define PIC_PDTA_DitherQuality   (PIC_PDTA_Dummy + 23)
#define PIC_PDTA_AllocatedPens   (PIC_PDTA_Dummy + 24)
#define PIC_PDTA_ScaleQuality    (PIC_PDTA_Dummy + 25)
#define PIC_PDTA_SourceMode      (ODT_DTA_Dummy + 250)
#define PIC_PDTA_DestMode        (ODT_DTA_Dummy + 251)
#define PIC_PDTA_UseFriendBitMap (ODT_DTA_Dummy + 255)
#define PIC_PDTA_MaskPlane       (ODT_DTA_Dummy + 258)
/* Whether the picture's ARGB pixels carry alpha (the subclass says; AROS's
 * and the Team's datatypes set it, 0x80001100). Not set: TRUE when the BMHD
 * says mskHasAlpha. */
#define PIC_PDTA_AlphaChannel    (ODT_DTA_Dummy + 256)

#define PIC_PMODE_V42 0
#define PIC_PMODE_V43 1

#define PIC_PDTM_Dummy           (ODT_DTM_Dummy + 0x60)
#define PIC_PDTM_WRITEPIXELARRAY (PIC_PDTM_Dummy + 0)
#define PIC_PDTM_READPIXELARRAY  (PIC_PDTM_Dummy + 1)
#define PIC_PDTM_SCALE           (PIC_PDTM_Dummy + 2)

#define PIC_PBPAFMT_RGB   0
#define PIC_PBPAFMT_RGBA  1
#define PIC_PBPAFMT_ARGB  2
#define PIC_PBPAFMT_LUT8  3
#define PIC_PBPAFMT_GREY8 4

#define PIC_PScale_KeepAspect 0x10

/* BitMapHeader.bmh_Masking and bmh_Compression */
#define PIC_mskNone                0
#define PIC_mskHasMask             1
#define PIC_mskHasTransparentColor 2
#define PIC_mskLasso               3
#define PIC_mskHasAlpha            4
#define PIC_cmpNone     0
#define PIC_cmpByteRun1 1
#define PIC_BMHDF_CMAPOK 0x80

/* graphics/modeid.h, graphics/view.h */
#define PIC_INVALID_ID      0xffffffffUL
#define PIC_MODE_LACE       0x0004UL
#define PIC_MODE_EHB        0x0080UL
#define PIC_MODE_HAM        0x0800UL
#define PIC_MODE_HIRES      0x8000UL
#define PIC_PRECISION_IMAGE 0
#define PIC_PRECISION_EXACT (-1)
#define PIC_OBP_Precision   0x84000000UL /* graphics/view.h: taken as an attribute, as bitmap.image passes it */

struct pic_BitMapHeader {
    odt_u16 bmh_Width, bmh_Height;
    odt_s16 bmh_Left, bmh_Top;
    odt_u8 bmh_Depth, bmh_Masking, bmh_Compression, bmh_Pad;
    odt_u16 bmh_Transparent;
    odt_u8 bmh_XAspect, bmh_YAspect;
    odt_s16 bmh_PageWidth, bmh_PageHeight;
};
ODT_STATIC_ASSERT(sizeof(struct pic_BitMapHeader) == 20, "BitMapHeader");

struct pic_ColorRegister { odt_u8 red, green, blue; };
ODT_STATIC_ASSERT(sizeof(struct pic_ColorRegister) == 3, "ColorRegister");

struct pic_pdtBlitPixelArray {
    odt_u32 MethodID;
    void *pbpa_PixelData;
    odt_u32 pbpa_PixelFormat;
    odt_u32 pbpa_PixelArrayMod;
    odt_u32 pbpa_Left, pbpa_Top, pbpa_Width, pbpa_Height;
};
ODT_STATIC_ASSERT(sizeof(struct pic_pdtBlitPixelArray) == 32, "pdtBlitPixelArray");
struct pic_pdtScale { odt_u32 MethodID, ps_NewWidth, ps_NewHeight, ps_Flags; };

/* ---- the instance ------------------------------------------------------------- */

#define PIC_STORE_NONE 0
#define PIC_STORE_LUT8 1                /* one byte a pixel, an index into cregs */
#define PIC_STORE_ARGB 2                /* four bytes a pixel */

struct pic_inst {
    struct pic_BitMapHeader bmhd;
    odt_u16 numcolors;                  /* PDTA_NumColors */
    odt_u16 numalloc;                   /* pens obtained (pens[]) */
    odt_u16 numsparse;
    odt_u16 flags;                      /* PIC_F_ */
    odt_u32 modeid;
    struct odt_Point grab;
    struct pic_ColorRegister cmap[256];
    odt_u32 cregs[768];                 /* 32-bit guns of the picture's colours */
    odt_u32 gregs[768];                 /* of the pens it was remapped to */
    odt_u8 colortable[256];             /* colour -> pen */
    odt_u8 colortable2[256];
    odt_u8 allocated[256];              /* PDTA_Allocated: 1 for each pen obtained */
    odt_u8 pens[256];                   /* each ObtainBestPen, to release */
    odt_u8 sparse[256];
    odt_s32 precision;                  /* OBP_Precision */
    odt_u32 sourcemode, destmode;
    void *screen;                       /* PDTA_Screen */
    void *pen_cm;                       /* the colour map pens[] came from */
    void *laid_screen;                  /* the screen of the last layout */
    struct odt_BitMap *srcbm;           /* PDTA_BitMap set by a V42 subclass */
    struct odt_BitMap *classbm;         /* PDTA_BitMap made from the pixels */
    struct odt_BitMap *destbm;
    odt_u8 *mask;                       /* PDTA_MaskPlane, of the destination */
    odt_u8 *alpha;                      /* a byte a source pixel: 0 transparent */
    odt_u8 *pixels;
    odt_u32 store;                      /* PIC_STORE_ */
    odt_u32 pw, ph;                     /* size of pixels */
    odt_u32 dw, dh;                     /* size of destbm */
    odt_u32 scale_w, scale_h, scale_flags;
    odt_u32 draw_holds;                 /* ObtainDTDrawInfo handles holding si_Lock */
};
#define PIC_F_REMAP      0x0001
#define PIC_F_FREESRC    0x0002
#define PIC_F_FRIEND     0x0004
#define PIC_F_LAIDOUT    0x0008
#define PIC_F_DESTARGB   0x0010
#define PIC_F_ALPHA      0x0020      /* PDTA_AlphaChannel TRUE */
#define PIC_F_ALPHASET   0x0040      /* PDTA_AlphaChannel was set */

odt_u32 pic_inst_size(void);
odt_u32 pic_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);

/* Pure helpers, for the subclasses and the tests */
/* RASSIZE(w, 1): a plane row, even bytes */
static inline odt_u32 pic_rowbytes(odt_u32 w) { return ((w + 15) >> 4) << 1; }
void pic_planar_to_chunky(const struct odt_BitMap *bm, odt_u32 w, odt_u32 h, odt_u8 *out);
void pic_chunky_to_planar(const odt_u8 *in, odt_u32 w, odt_u32 h, odt_u32 depth, odt_u8 *const *planes,
                          odt_u32 bpr);
/* ByteRun1, one row: bytes written to out (at most n + (n + 127) / 128) */
odt_u32 pic_byterun1_pack(const odt_u8 *in, odt_u32 n, odt_u8 *out);
/* ILBM of the picture to a file handle: 1 written, 0 not (IoErr set) */
int pic_write_ilbm(const struct odt_cenv *env, struct pic_inst *p, odt_u32 fh);

#ifdef __cplusplus
}
#endif
#endif
