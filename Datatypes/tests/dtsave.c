/*
 * dtsave: a picture datatype's raw write, checked on the Amiga. Opens SRC
 * through datatypes.library, saves it with DTM_WRITE in raw mode to DST,
 * opens DST again and compares every pixel read back as ARGB (RGB only
 * when either has no alpha). With AS, it converts instead, as
 * docs/DESIGN.md says a program does: an empty object of that class
 * (DTST_RAM with DTA_BaseName), the pixels written into it, and that
 * object saved. With STORE, OIA_Compression 0 goes in dtw_AttrList.
 * With SAMEBYTES, any datatype (a film too) saves SRC raw, and DST must be
 * SRC byte for byte: what cdxl.datatype's raw write promises for an intact
 * film, and a writer's own output read and written again.
 *
 *     dtsave SRC DST [AS gfopentga|gfopenpng|...] [STORE]
 *     dtsave SRC DST SAMEBYTES
 *     dtsave SRC DST IFF
 *
 * IFF saves any datatype's object in its group's IFF form (DTWM_IFF:
 * ILBM, 8SVX, FTXT, ANIM), opens DST again and checks it is the same kind
 * of object with the same size (and, for a film, frame count); tests/dtanim
 * on SRC and DST then compares the frames.
 *
 * Prints "SRC -> DST: SAME w x h alpha=0|1 bytes=N" or what went wrong;
 * returns 0, or 10 when the pictures differ, 20 on an error. Built by
 * tests/build-dtsave.sh.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/animationclass.h>
#include <datatypes/gfopenimage.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

struct Library *DataTypesBase;

/* The picture as ARGB, and its size and alpha. NULL with a message on failure. */
static UBYTE *pixels(Object *o, const char *name, ULONG *w, ULONG *h, ULONG *alpha)
{
    struct BitMapHeader *bmh = NULL;
    UBYTE *px;
    *alpha = 0;
    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, PDTA_AlphaChannel, (ULONG)alpha, TAG_DONE) < 1 || !bmh) {
        printf("%s: NO_BITMAPHEADER\n", name);
        return NULL;
    }
    *w = bmh->bmh_Width;
    *h = bmh->bmh_Height;
    if (!(px = malloc(*w * *h * 4))
        || !DoMethod(o, PDTM_READPIXELARRAY, (ULONG)px, PBPAFMT_ARGB, *w * 4, 0, 0, *w, *h)) {
        printf("%s: READ_FAIL\n", name);
        free(px);
        return NULL;
    }
    return px;
}

/* SAMEBYTES: any datatype's raw write of SRC must be SRC, byte for byte */
static int same_bytes(const char *src, const char *dst)
{
    Object *o = NewDTObject((APTR)src, TAG_DONE);
    BPTR fh, a, b;
    UBYTE x[4096], y[4096];
    LONG n, m, at = 0;
    int rc = 20;
    if (!o) {
        printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", src, (long)IoErr());
        return 20;
    }
    if (!(fh = Open((STRPTR)dst, MODE_NEWFILE))) {
        printf("%s: can't open, ioerr=%ld\n", dst, (long)IoErr());
        DisposeDTObject(o);
        return 20;
    }
    if (!DoMethod(o, DTM_WRITE, NULL, fh, DTWM_RAW, 0UL)) {
        printf("%s -> %s: WRITE_FAIL ioerr=%ld\n", src, dst, (long)IoErr());
        Close(fh);
        DisposeDTObject(o);
        return 20;
    }
    Close(fh);
    DisposeDTObject(o);
    if (!(a = Open((STRPTR)src, MODE_OLDFILE)) || !(b = Open((STRPTR)dst, MODE_OLDFILE))) {
        if (a)
            Close(a);
        printf("%s -> %s: can't read them back\n", src, dst);
        return 20;
    }
    for (;;) {
        n = Read(a, x, sizeof x);
        m = Read(b, y, sizeof y);
        if (n != m || (n > 0 && memcmp(x, y, (size_t)n))) {
            printf("%s -> %s: DIFFERENT from byte %ld\n", src, dst, (long)at);
            rc = 10;
            break;
        }
        if (n <= 0) {
            printf("%s -> %s: SAMEBYTES %ld bytes\n", src, dst, (long)at);
            rc = n < 0 ? 20 : 0;
            break;
        }
        at += n;
    }
    Close(a);
    Close(b);
    return rc;
}

static Object *open_picture(const char *name)
{
    Object *o = NewDTObject((APTR)name, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE, TAG_DONE);
    if (!o)
        printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", name, (long)IoErr());
    else
        DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
    return o;
}

static int save_iff(const char *src, const char *dst)
{
    Object *o = NewDTObject((APTR)src, TAG_DONE), *b;
    struct DataType *dt = NULL;
    ULONG w = 0, h = 0, f = 0, w2 = 0, h2 = 0, f2 = 0, gid = 0, gid2 = 0;
    BPTR fh;
    LONG bytes;
    if (!o) {
        printf("%s: NEWDTOBJECT_FAIL ioerr=%ld\n", src, (long)IoErr());
        return 20;
    }
    GetDTAttrs(o, DTA_DataType, (ULONG)&dt, DTA_NominalHoriz, (ULONG)&w, DTA_NominalVert, (ULONG)&h, TAG_DONE);
    if (dt) gid = dt->dtn_Header->dth_GroupID;
    if (gid == GID_ANIMATION)
        GetDTAttrs(o, ADTA_Width, (ULONG)&w, ADTA_Height, (ULONG)&h, ADTA_Frames, (ULONG)&f, TAG_DONE);
    if (!(fh = Open((STRPTR)dst, MODE_NEWFILE))) {
        printf("%s: can't open, ioerr=%ld\n", dst, (long)IoErr());
        DisposeDTObject(o);
        return 20;
    }
    if (!DoMethod(o, DTM_WRITE, NULL, fh, DTWM_IFF, 0UL)) {
        printf("%s -> %s: WRITE_FAIL ioerr=%ld\n", src, dst, (long)IoErr());
        Close(fh);
        DisposeDTObject(o);
        return 20;
    }
    bytes = Seek(fh, 0, OFFSET_CURRENT);
    Close(fh);
    DisposeDTObject(o);
    if (!(b = NewDTObject((APTR)dst, TAG_DONE))) {
        printf("%s -> %s: REOPEN_FAIL ioerr=%ld\n", src, dst, (long)IoErr());
        return 20;
    }
    dt = NULL;
    GetDTAttrs(b, DTA_DataType, (ULONG)&dt, DTA_NominalHoriz, (ULONG)&w2, DTA_NominalVert, (ULONG)&h2, TAG_DONE);
    if (dt) gid2 = dt->dtn_Header->dth_GroupID;
    if (gid2 == GID_ANIMATION)
        GetDTAttrs(b, ADTA_Width, (ULONG)&w2, ADTA_Height, (ULONG)&h2, ADTA_Frames, (ULONG)&f2, TAG_DONE);
    printf("%s -> %s: %s %s %lux%lu frames=%lu bytes=%ld\n", src, dst,
           gid2 == gid && w2 == w && h2 == h && f2 == f ? "SAMEKIND" : "DIFFERENT",
           dt ? (char *)dt->dtn_Header->dth_Name : "?", (unsigned long)w2, (unsigned long)h2, (unsigned long)f2,
           (long)bytes);
    DisposeDTObject(b);
    return gid2 == gid && w2 == w && h2 == h && f2 == f ? 0 : 10;
}

int main(int argc, char **argv)
{
    const char *src, *dst, *as = NULL;
    struct TagItem store[] = { { OIA_Compression, 0 }, { TAG_DONE, 0 } };
    Object *o = NULL, *target = NULL, *back = NULL;
    UBYTE *a = NULL, *b = NULL;
    ULONG w = 0, h = 0, al = 0, w2 = 0, h2 = 0, al2 = 0, i, diff = 0, ok;
    BPTR fh;
    LONG bytes = 0;
    int rc = 20, k, st = 0, bytes_mode = 0, iff_mode = 0;

    if (argc < 3) {
        printf("usage: dtsave SRC DST [AS class] [STORE] | SRC DST SAMEBYTES | SRC DST IFF\n");
        return 20;
    }
    src = argv[1];
    dst = argv[2];
    for (k = 3; k < argc; k++) {
        if (!strcmp(argv[k], "AS") && k + 1 < argc)
            as = argv[++k];
        else if (!strcmp(argv[k], "STORE"))
            st = 1;
        else if (!strcmp(argv[k], "SAMEBYTES"))
            bytes_mode = 1;
        else if (!strcmp(argv[k], "IFF"))
            iff_mode = 1;
    }
    if (!(DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 43)))
        return 20;
    if (bytes_mode || iff_mode) {
        rc = bytes_mode ? same_bytes(src, dst) : save_iff(src, dst);
        CloseLibrary(DataTypesBase);
        return rc;
    }
    if (!(o = open_picture(src)) || !(a = pixels(o, src, &w, &h, &al)))
        goto out;
    target = o;
    if (as) {
        /* What a V43 picture subclass does when it loads (gfopentga's
         * loadTGA, gfopenpng's, AROS's png.datatype): the BitMapHeader
         * filled in, then its name, the nominal size and V43 mode set in
         * one SetDTAttrs, then the pixels. OS 3.2's own picture.datatype is
         * known to take pixels that way; the object is made in V43 mode
         * from the start as well. */
        struct BitMapHeader *bmh = NULL;
        if (!(target = NewDTObject(NULL, DTA_SourceType, DTST_RAM, DTA_BaseName, (ULONG)as,
                                   DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
                                   PDTA_SourceMode, PMODE_V43, TAG_DONE))) {
            printf("%s: no empty %s object, ioerr=%ld\n", src, as, (long)IoErr());
            goto out;
        }
        if (GetDTAttrs(target, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) < 1 || !bmh) {
            printf("%s: the %s object has no BitMapHeader\n", src, as);
            goto out;
        }
        bmh->bmh_Width = bmh->bmh_PageWidth = (UWORD)w;
        bmh->bmh_Height = bmh->bmh_PageHeight = (UWORD)h;
        bmh->bmh_Depth = 32;
        bmh->bmh_Masking = al ? mskHasAlpha : mskNone;
        SetDTAttrs(target, NULL, NULL,
                   DTA_ObjName, (ULONG)FilePart((STRPTR)dst),
                   DTA_NominalHoriz, w,
                   DTA_NominalVert, h,
                   PDTA_SourceMode, PMODE_V43,
                   PDTA_AlphaChannel, al,
                   TAG_DONE);
        SetIoErr(0);
        if (!DoMethod(target, PDTM_WRITEPIXELARRAY, (ULONG)a, PBPAFMT_ARGB, w * 4, 0, 0, w, h)) {
            /* what the object says of itself, for the lab's log */
            ULONG sm = ~0UL, nh = ~0UL, nv = ~0UL;
            LONG e = IoErr();
            struct BitMapHeader *b2 = NULL;
            GetDTAttrs(target, PDTA_SourceMode, (ULONG)&sm, PDTA_BitMapHeader, (ULONG)&b2,
                       DTA_NominalHoriz, (ULONG)&nh, DTA_NominalVert, (ULONG)&nv, TAG_DONE);
            printf("%s: WRITEPIXELARRAY into %s failed, ioerr=%ld sourcemode=%ld bmhd=%s%lux%lux%u nominal=%ldx%ld\n",
                   src, as, (long)e, (long)sm, b2 == bmh ? "" : "(moved) ",
                   b2 ? (unsigned long)b2->bmh_Width : 0UL, b2 ? (unsigned long)b2->bmh_Height : 0UL,
                   b2 ? b2->bmh_Depth : 0, (long)nh, (long)nv);
            goto out;
        }
    }
    if (!(fh = Open((STRPTR)dst, MODE_NEWFILE))) {
        printf("%s: can't open, ioerr=%ld\n", dst, (long)IoErr());
        goto out;
    }
    ok = DoMethod(target, DTM_WRITE, NULL, fh, DTWM_RAW, st ? (ULONG)store : 0UL);
    if (ok)
        bytes = Seek(fh, 0, OFFSET_CURRENT);
    Close(fh);
    if (!ok) {
        printf("%s -> %s: WRITE_FAIL ioerr=%ld\n", src, dst, (long)IoErr());
        goto out;
    }
    if (!(back = open_picture(dst)) || !(b = pixels(back, dst, &w2, &h2, &al2)))
        goto out;
    if (w2 != w || h2 != h) {
        printf("%s -> %s: SIZE %lux%lu, not %lux%lu\n", src, dst, (unsigned long)w2, (unsigned long)h2,
               (unsigned long)w, (unsigned long)h);
        rc = 10;
        goto out;
    }
    for (i = 0; i < w * h; i++) {
        int j = (al && al2) ? 0 : 1;   /* compare alpha only when both have it */
        for (; j < 4; j++)
            if (a[4 * i + j] != b[4 * i + j])
                diff++;
    }
    printf("%s -> %s: %s %lu x %lu alpha=%d bytes=%ld%s\n", src, dst, diff ? "DIFFERENT" : "SAME",
           (unsigned long)w, (unsigned long)h, al ? 1 : 0, (long)bytes, al != al2 ? " (alpha lost)" : "");
    rc = (diff || al != al2) ? 10 : 0;
out:
    free(a);
    free(b);
    if (back)
        DisposeDTObject(back);
    if (target && target != o)
        DisposeDTObject(target);
    if (o)
        DisposeDTObject(o);
    CloseLibrary(DataTypesBase);
    return rc;
}
