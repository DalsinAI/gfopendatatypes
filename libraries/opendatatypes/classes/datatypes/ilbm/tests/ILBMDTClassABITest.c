/* ILBMDTClassABITest: OS 3.2's ilbm.datatype and GFOpenILBM side by side.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Writes ILBM and PBM files made here (no media of anyone's) to DIR= (T:)
 * and loads each through OS 3.2's ilbm.datatype and through
 * gfopenilbm.datatype (with gfopenpicture.datatype under it), printing what
 * each answers: the error, the BitMapHeader, ModeID, colours, and pixels
 * read back as ARGB. The PROBE lines are the corners the ILBM
 * specification leaves open, which ilbm_core.cpp marks "ABI test".
 *   ILBMDTClassABITest [PROBE] [LIB=dir] [DIR=dir]
 */
#define DTC_TEST_CLASS "ilbm.datatype"
#include "../../picture/tests/dtc_abitest.h"
#include <datatypes/pictureclass.h>

#ifndef PDTM_READPIXELARRAY
#define PDTM_READPIXELARRAY (PDTM_Dummy + 1)
#define PBPAFMT_ARGB 2
#endif

struct ilbm_spec {
    const char *name;
    UWORD w, h;
    UBYTE depth, masking, cmp, pad;
    UWORD transparent;
    int ncmap;                          /* -1: no CMAP */
    int four;                           /* 4-bit guns */
    LONG camg;                          /* -1: no CAMG */
    int pbm, short_body, no_bmhd;
};

static ULONG rnd = 12345;
static UBYTE rbyte(void) { rnd = rnd * 1103515245UL + 12345; return (UBYTE)(rnd >> 16); }

static void make_ilbm(const struct ilbm_spec *s)
{
    static struct iffw w;
    UBYTE bmhd[20], cmap[768], body[8192];
    ULONG rb = ((s->w + 15) >> 4) << 1, rows = s->pbm ? (ULONG)(s->w + (s->w & 1)) : rb * (s->depth + (s->masking == 1)),
          n = rows * s->h, i;
    iff_begin(&w, s->pbm ? "PBM " : "ILBM");
    bmhd[0] = s->w >> 8; bmhd[1] = s->w; bmhd[2] = s->h >> 8; bmhd[3] = s->h;
    bmhd[4] = bmhd[5] = bmhd[6] = bmhd[7] = 0;
    bmhd[8] = s->depth; bmhd[9] = s->masking; bmhd[10] = s->cmp; bmhd[11] = s->pad;
    bmhd[12] = s->transparent >> 8; bmhd[13] = s->transparent;
    bmhd[14] = 10; bmhd[15] = 11; bmhd[16] = 1; bmhd[17] = 64; bmhd[18] = 1; bmhd[19] = 0;
    if (!s->no_bmhd) iff_chunk(&w, "BMHD", bmhd, 20);
    if (s->ncmap >= 0) {
        for (i = 0; i < (ULONG)s->ncmap * 3; i++) cmap[i] = s->four ? rbyte() & 0xf0 : rbyte();
        iff_chunk(&w, "CMAP", cmap, s->ncmap * 3);
    }
    if (s->camg >= 0) {
        UBYTE c[4];
        put32(c, s->camg);
        iff_chunk(&w, "CAMG", c, 4);
    }
    if (n > sizeof body) n = sizeof body;
    for (i = 0; i < n; i++) body[i] = rbyte();
    if (s->cmp == 1) {                  /* literal runs only: what it holds is what we wrote */
        static UBYTE packed[8192 + 8192 / 128 + 8];
        ULONG o = 0, k;
        for (i = 0; i < n; i += 128) {
            k = n - i < 128 ? n - i : 128;
            packed[o++] = k - 1;
            memcpy(packed + o, body + i, k);
            o += k;
        }
        iff_chunk(&w, "BODY", packed, s->short_body ? o / 2 : o);
    } else {
        iff_chunk(&w, "BODY", body, s->short_body ? n / 2 : n);
    }
    iff_end(&w);
    write_file(s->name, w.buf, w.len);
}

static const struct ilbm_spec specs[] = {
    { "odt_i1.iff", 13, 5, 1, 0, 1, 0, 0, 2, 0, -1, 0, 0, 0 },
    { "odt_i4.iff", 20, 6, 4, 0, 1, 0, 0, 16, 0, -1, 0, 0, 0 },
    { "odt_cmap4.iff", 12, 4, 4, 0, 0, 0, 0, 16, 1, -1, 0, 0, 0 },       /* 4-bit guns: scaled? */
    { "odt_cmapok.iff", 12, 4, 4, 0, 0, 0x80, 0, 16, 1, -1, 0, 0, 0 },   /* BMHDF_CMAPOK */
    { "odt_short.iff", 12, 4, 4, 0, 0, 0, 0, 5, 0, -1, 0, 0, 0 },        /* CMAP too short */
    { "odt_nocmap.iff", 12, 4, 3, 0, 0, 0, 0, -1, 0, -1, 0, 0, 0 },      /* no CMAP */
    { "odt_mask.iff", 14, 6, 4, 1, 1, 0, 0, 16, 0, -1, 0, 0, 0 },
    { "odt_transp.iff", 14, 6, 3, 2, 1, 0, 5, 8, 0, -1, 0, 0, 0 },
    { "odt_lasso.iff", 14, 6, 3, 3, 1, 0, 0, 8, 0, -1, 0, 0, 0 },
    { "odt_pbm.iff", 11, 5, 8, 0, 1, 0, 0, 256, 0, -1, 1, 0, 0 },
    { "odt_pbm4.iff", 11, 5, 4, 0, 1, 0, 0, 16, 0, -1, 1, 0, 0 },
    { "odt_ham6.iff", 12, 4, 6, 0, 1, 0, 0, 16, 0, 0x800, 0, 0, 0 },
    { "odt_ham8.iff", 12, 4, 8, 0, 1, 0, 0, 64, 0, 0x800, 0, 0, 0 },
    { "odt_ham4.iff", 12, 4, 4, 0, 1, 0, 0, 16, 0, 0x800, 0, 0, 0 },     /* HAM of another depth */
    { "odt_ehb.iff", 12, 4, 6, 0, 1, 0, 0, 32, 0, 0x80, 0, 0, 0 },
    { "odt_6nocamg.iff", 12, 4, 6, 0, 1, 0, 0, 32, 0, -1, 0, 0, 0 },     /* EHB guessed? */
    { "odt_sprites.iff", 12, 4, 4, 0, 1, 0, 0, 16, 0, 0x6004, 0, 0, 0 }, /* SPRITES|VP_HIDE|LACE */
    { "odt_big.iff", 640, 2, 1, 0, 1, 0, 0, 2, 0, -1, 0, 0, 0 },         /* no CAMG, 640 wide */
    { "odt_24.iff", 8, 3, 24, 0, 1, 0, 0, -1, 0, -1, 0, 0, 0 },
    { "odt_32.iff", 7, 3, 32, 0, 1, 0, 0, -1, 0, -1, 0, 0, 0 },
    { "odt_12.iff", 7, 3, 12, 0, 1, 0, 0, -1, 0, -1, 0, 0, 0 },          /* a deep ILBM of 12 planes */
    { "odt_trunc.iff", 40, 3, 2, 0, 1, 0, 0, 4, 0, -1, 0, 1, 0 },
    { "odt_nobmhd.iff", 8, 3, 2, 0, 1, 0, 0, 4, 0, -1, 0, 0, 1 },
    { "odt_cmp2.iff", 8, 3, 2, 0, 2, 0, 0, 4, 0, -1, 0, 0, 0 },
    { "odt_depth9.iff", 8, 3, 9, 0, 1, 0, 0, 4, 0, -1, 0, 0, 0 },
};

static void dump(const char *cls, const struct ilbm_spec *s)
{
    Object *o = new_file_obj(cls, s->name, NULL);
    struct BitMapHeader *b;
    ULONG n, i, *cregs, y;
    UBYTE *px;
    if (!o) {
        probe("%s fail %ld", s->name, (long)IoErr());
        return;
    }
    b = (struct BitMapHeader *)get1(o, PDTA_BitMapHeader);
    n = get1(o, PDTA_NumColors);
    cregs = (ULONG *)get1(o, PDTA_CRegs);
    out("%s bmhd %u %u %u %u %u %u %u %u", s->name, b->bmh_Width, b->bmh_Height, b->bmh_Depth, b->bmh_Masking,
        b->bmh_Compression, b->bmh_Transparent, b->bmh_XAspect, b->bmh_YAspect);
    probe("%s mode %08lx ncol %lu", s->name, get1(o, PDTA_ModeID), n);
    for (i = 0; i < n && i < 64; i++)
        probe("%s cregs %lu %02lx%02lx%02lx", s->name, i, cregs[i * 3] >> 24, cregs[i * 3 + 1] >> 24,
              cregs[i * 3 + 2] >> 24);
    px = AllocVec(b->bmh_Width * 4, MEMF_ANY | MEMF_CLEAR);
    for (y = 0; px && y < b->bmh_Height && y < 6; y++) {
        char line[256 * 9];
        ULONG x, k = 0, r = DoMethod(o, PDTM_READPIXELARRAY, px, PBPAFMT_ARGB, b->bmh_Width * 4, 0, y,
                                     b->bmh_Width, 1);
        for (x = 0; x < b->bmh_Width && x < 32; x++)
            k += sprintf(line + k, "%02x%02x%02x%02x", px[x * 4], px[x * 4 + 1], px[x * 4 + 2], px[x * 4 + 3]);
        line[k] = 0;
        probe("%s row %lu %lu %s", s->name, y, r, line);
    }
    if (px) FreeVec(px);
    probe("%s maskplane-set %lu", s->name, get1(o, PDTA_MaskPlane) != 0);
    DisposeObject(o);
}

static void run_set(const char *cls)
{
    ULONG i;
    rnd = 12345;
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) make_ilbm(&specs[i]);
    for (i = 0; i < sizeof specs / sizeof specs[0]; i++) dump(cls, &specs[i]);
}

int main(int argc, char **argv) { return dtc_main(argc, argv); }
