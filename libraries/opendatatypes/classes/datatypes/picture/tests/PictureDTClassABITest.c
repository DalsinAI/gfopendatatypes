/* PictureDTClassABITest: OS 3.2's picture.datatype and GFOpenPicture side by
 * side.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Makes pictures in memory (DTST_RAM objects of picture.datatype itself)
 * and asks each class the same: its defaults, PDTM_WRITEPIXELARRAY and
 * PDTM_READPIXELARRAY in every format, rectangles partly outside,
 * PDTM_SCALE, DTM_FRAMEBOX, DTM_WRITE (the ILBM it writes, chunk by
 * chunk), and a layout on the Workbench screen through ObtainDTDrawInfoA
 * (pens obtained, the destination's depth, the mask). The PROBE lines
 * answer what picture_core.cpp marks "ABI test".
 *   PictureDTClassABITest [PROBE] [LIB=dir] [DIR=dir]
 */
#define DTC_TEST_CLASS "picture.datatype"
#include "dtc_abitest.h"
#include <datatypes/pictureclass.h>
#include <graphics/gfx.h>
#include <proto/graphics.h>

/* NDK 3.2's datatypes_lib.fd keeps ObtainDTDrawInfoA and ReleaseDTDrawInfo
 * private, so neither NDK's proto has them: called at their V44 LVOs */
static APTR ObtainDTDrawInfoA(Object *o, struct TagItem *attrs)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1");
    register Object *a0 __asm("a0") = o;
    register struct TagItem *a1 __asm("a1") = attrs;
    register struct Library *a6 __asm("a6") = DataTypesBase;
    __asm__ __volatile__("jsr -120(%%a6)" : "=r"(d0), "=r"(d1), "+r"(a0), "+r"(a1) : "r"(a6) : "cc", "memory");
    return (APTR)d0;
}

static void ReleaseDTDrawInfo(Object *o, APTR handle)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1");
    register Object *a0 __asm("a0") = o;
    register APTR a1 __asm("a1") = handle;
    register struct Library *a6 __asm("a6") = DataTypesBase;
    __asm__ __volatile__("jsr -132(%%a6)" : "=r"(d0), "=r"(d1), "+r"(a0), "+r"(a1) : "r"(a6) : "cc", "memory");
}

#ifndef PDTM_WRITEPIXELARRAY
#define PDTM_WRITEPIXELARRAY (PDTM_Dummy + 0)
#define PDTM_READPIXELARRAY (PDTM_Dummy + 1)
#define PBPAFMT_RGB 0
#define PBPAFMT_RGBA 1
#define PBPAFMT_ARGB 2
#define PBPAFMT_LUT8 3
#define PBPAFMT_GREY8 4
#endif
#ifndef PDTM_SCALE
#define PDTM_SCALE (PDTM_Dummy + 2)
#endif

struct GfxBase *GfxBase;

static const char *const fmt_name[] = { "RGB", "RGBA", "ARGB", "LUT8", "GREY8" };

static Object *make(const char *cls, UWORD w, UWORD h, UBYTE depth, ULONG ncol)
{
    Object *o = new_ram_obj(cls, NULL);
    struct BitMapHeader *b;
    ULONG *cregs, i;
    if (!o) return NULL;
    b = (struct BitMapHeader *)get1(o, PDTA_BitMapHeader);
    cregs = (ULONG *)get1(o, PDTA_CRegs);
    b->bmh_Width = w;
    b->bmh_Height = h;
    b->bmh_Depth = depth;
    for (i = 0; i < ncol * 3; i++) cregs[i] = ((i * 37 + 11) & 0xff) * 0x01010101UL;
    SetAttrs(o, PDTA_NumColors, ncol, TAG_DONE);
    return o;
}

static void readback(Object *o, const char *tag, UWORD w, UWORD h)
{
    UBYTE px[4 * 8 * 4];
    ULONG f, y, x;
    for (f = 0; f <= PBPAFMT_GREY8; f++) {
        ULONG bpp = f == 0 ? 3 : f <= 2 ? 4 : 1;
        char line[200];
        ULONG k = 0, r;
        memset(px, 0xaa, sizeof px);
        r = DoMethod(o, PDTM_READPIXELARRAY, px, f, w * bpp, 0, 0, w, h);
        for (y = 0; y < h; y++)
            for (x = 0; x < w * bpp && k < sizeof line - 3; x++) k += sprintf(line + k, "%02x", px[y * w * bpp + x]);
        line[k] = 0;
        probe("%s read %s %lu %s", tag, fmt_name[f], r, line);
    }
}

static void dump_ilbm(const char *tag, Object *o, ULONG mode)
{
    char p[256];
    BPTR fh;
    UBYTE buf[2048];
    LONG n, i;
    path_of(p, sizeof p, "odt_pic_write.iff");
    if (!(fh = Open((CONST_STRPTR)p, MODE_NEWFILE))) return;
    SetIoErr(0);
    n = DoMethod(o, DTM_WRITE, NULL, fh, mode, NULL);
    probe("%s write mode %lu = %ld ioerr %ld", tag, mode, n, (long)IoErr());
    Close(fh);
    if (!(fh = Open((CONST_STRPTR)p, MODE_OLDFILE))) return;
    n = Read(fh, buf, sizeof buf);
    Close(fh);
    for (i = 12; i + 8 <= n;) {
        ULONG size = (buf[i + 4] << 24) | (buf[i + 5] << 16) | (buf[i + 6] << 8) | buf[i + 7], k;
        char line[100];
        int m = 0;
        for (k = 0; k < size && k < 24 && i + 8 + k < (ULONG)n; k++) m += sprintf(line + m, "%02x", buf[i + 8 + k]);
        line[m] = 0;
        probe("%s chunk %.4s %lu %s", tag, buf + i, size, line);
        i += 8 + size + (size & 1);
    }
}

static void run_set(const char *cls)
{
    static const UBYTE lut[12] = { 0, 1, 2, 3, 3, 2, 1, 0, 1, 1, 2, 2 };
    static const UBYTE rgb[12 * 4] = { 0x10, 0x20, 0x30, 0x40, 0xff, 0x00, 0x00, 0x80, 0x00, 0xff, 0x00, 0x00,
                                       0x00, 0x00, 0xff, 0xff, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 };
    Object *o;
    struct Screen *scr;
    ULONG f;

    /* defaults of a new, empty picture */
    o = new_ram_obj(cls, NULL);
    if (!o) { out("new fail %ld", (long)IoErr()); return; }
    probe("default mode %08lx remap %lu numcolors %lu", get1(o, PDTA_ModeID), get1(o, PDTA_Remap),
          get1(o, PDTA_NumColors));
    probe("default sourcemode %lu destmode %lu freesource %lu", get1(o, PDTA_SourceMode), get1(o, PDTA_DestMode),
          get1(o, PDTA_FreeSourceBitMap));
    probe("default maskplane %lu numpictures %lu", get1(o, PDTA_MaskPlane) != 0, get1(o, PDTA_GetNumPictures));
    probe("default usefriendbitmap %lu", get1(o, PDTA_UseFriendBitMap));
    probe("write before bmhd %lu", DoMethod(o, PDTM_WRITEPIXELARRAY, lut, PBPAFMT_LUT8, 4, 0, 0, 4, 3));
    probe("maskplane settable %lu", SetAttrs(o, PDTA_MaskPlane, (ULONG)lut, TAG_DONE));
    DisposeObject(o);

    /* each format into an index picture and into a true colour one */
    for (f = 0; f <= PBPAFMT_GREY8; f++) {
        char tag[32];
        ULONG bpp = f == 0 ? 3 : f <= 2 ? 4 : 1;
        o = make(cls, 4, 3, 2, 4);
        if (!o) continue;
        sprintf(tag, "idx<-%s", fmt_name[f]);
        probe("%s write %lu", tag,
              DoMethod(o, PDTM_WRITEPIXELARRAY, f == 3 || f == 4 ? (APTR)lut : (APTR)rgb, f, 4 * bpp, 0, 0, 4, 3));
        readback(o, tag, 4, 3);
        DisposeObject(o);
        o = make(cls, 4, 3, 24, 0);
        if (!o) continue;
        sprintf(tag, "rgb<-%s", fmt_name[f]);
        probe("%s write %lu", tag,
              DoMethod(o, PDTM_WRITEPIXELARRAY, f == 3 || f == 4 ? (APTR)lut : (APTR)rgb, f, 4 * bpp, 0, 0, 4, 3));
        readback(o, tag, 4, 3);
        DisposeObject(o);
    }

    /* a rectangle partly outside; PDTM_SCALE; DTM_FRAMEBOX; DTM_WRITE */
    o = make(cls, 4, 3, 2, 4);
    if (o) {
        struct FrameInfo fi;
        struct dtFrameBox fb;
        DoMethod(o, PDTM_WRITEPIXELARRAY, lut, PBPAFMT_LUT8, 4, 0, 0, 4, 3);
        probe("outside write %lu", DoMethod(o, PDTM_WRITEPIXELARRAY, lut, PBPAFMT_LUT8, 4, 2, 1, 4, 4));
        probe("outside write far %lu", DoMethod(o, PDTM_WRITEPIXELARRAY, lut, PBPAFMT_LUT8, 4, 9, 9, 4, 4));
        readback(o, "outside", 4, 3);
        probe("scale %lu", DoMethod(o, PDTM_SCALE, 9, 9, 0x10));
        memset(&fi, 0, sizeof fi);
        fb.MethodID = DTM_FRAMEBOX; fb.dtf_GInfo = NULL; fb.dtf_ContentsInfo = NULL; fb.dtf_FrameInfo = &fi;
        fb.dtf_SizeFrameInfo = sizeof fi; fb.dtf_FrameFlags = 0;
        probe("framebox %lu flags %08lx res %d %d bits %u %u %u dim %lu %lu %lu fif %lu", DoMethodA(o, (Msg)&fb),
              fi.fri_PropertyFlags, fi.fri_Resolution.x, fi.fri_Resolution.y, fi.fri_RedBits, fi.fri_GreenBits,
              fi.fri_BlueBits, fi.fri_Dimensions.Width, fi.fri_Dimensions.Height, fi.fri_Dimensions.Depth,
              fi.fri_Flags);
        dump_ilbm("idx", o, DTWM_IFF);
        dump_ilbm("idx", o, DTWM_RAW);
        DisposeObject(o);
    }
    o = make(cls, 4, 3, 24, 0);
    if (o) {
        DoMethod(o, PDTM_WRITEPIXELARRAY, rgb, PBPAFMT_RGBA, 16, 0, 0, 4, 3);
        dump_ilbm("rgb", o, DTWM_IFF);
        DisposeObject(o);
    }

    /* a layout for the Workbench screen */
    if ((scr = LockPubScreen(NULL)) != NULL) {
        o = make(cls, 9, 5, 3, 8);
        if (o) {
            static UBYTE px[45];
            APTR h;
            ULONG i;
            for (i = 0; i < 45; i++) px[i] = i % 8;
            DoMethod(o, PDTM_WRITEPIXELARRAY, px, PBPAFMT_LUT8, 9, 0, 0, 9, 5);
            h = ObtainDTDrawInfoA(o, (struct TagItem *)(ULONG[]){ PDTA_Screen, (ULONG)scr, TAG_DONE });
            out("drawinfo %lu", h != NULL);
            if (h) {
                struct BitMap *bm = (struct BitMap *)get1(o, PDTA_DestBitMap);
                probe("layout numalloc %lu dest %lu depth %lu screen %lu", get1(o, PDTA_NumAlloc), bm != NULL,
                      bm ? GetBitMapAttr(bm, BMA_DEPTH) : 0, GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH));
                ReleaseDTDrawInfo(o, h);
            }
            DisposeObject(o);
        }
        UnlockPubScreen(NULL, scr);
    }
}

int main(int argc, char **argv)
{
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    return dtc_main(argc, argv);
}
