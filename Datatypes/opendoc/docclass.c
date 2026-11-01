/*
 * gfopendoc.datatype: documents (DOCX, XLSX, PPTX, ODT, ODS, ODP, DOC, XLS,
 * PPT, RTF, WordPerfect, PostScript, EPS, EPUB, Markdown, CSV) for AmigaOS
 * 3.x, shown as a picture of their pages, one under the other, like a PDF
 * viewer's continuous view. The pages are laid out by LibreOffice (or
 * Ghostscript, or pandoc then LibreOffice) through the doc.render/1 service
 * (DalsinAI/openamigaservice docs/DOC_RENDER.md) on the services card or a
 * paired Cradle, at ENV:GFOpenImage/DocWidth pixels wide (default 800), the
 * first ENV:GFOpenImage/DocPages pages (default 8).
 *
 * With neither a card nor a Cradle the document does not open yet (a text
 * view laid out on the 68k comes later).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

#include "dtlib.h"
#include "dtservice.h"

const char LibName[] = "gfopendoc.datatype";
const char LibIdString[] = "gfopendoc.datatype 47.1 (5.10.2026) AmiCradle Team, laid out by doc.render/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gfopendoc.datatype 47.1 (5.10.2026)";

const char dt_superclass[] = "picture.datatype";
const UWORD dt_superversion = 43;
ULONG dt_instsize = 0;

/* doc.render/1 (docs/DOC_RENDER.md) */
#define DR_PROBE  1
#define DR_RENDER 2
#define DR_KIND_DOCUMENT 4

/* Grey between pages, in pixels. */
#define GAP 8

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG get32(const UBYTE *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

static ULONG envNumber(const char *name, ULONG fallback, ULONG least)
{
    char buf[16];
    LONG n = GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    ULONG v = 0;
    LONG i;

    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v >= least ? v : fallback;
}

/* A grey band GAP pixels high across the picture at y. */
static BOOL writeGap(Class *cl, Object *o, ULONG w, ULONG y)
{
    ULONG *grey = AllocVec(w * 4 * GAP, MEMF_ANY), i;
    BOOL ok;

    if (!grey)
        return FALSE;
    for (i = 0; i < w * GAP; i++)
        grey[i] = 0xff808080UL;
    ok = DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)grey, PBPAFMT_ARGB, w * 4, 0, y, w, GAP) ? TRUE : FALSE;
    FreeVec(grey);
    return ok;
}

static BOOL loadDoc(Class *cl, Object *o)
{
    struct BitMapHeader *bmh = NULL;
    struct dt_service svc;
    struct OSBuffer buf[4];
    ULONG extra[4], size = 0, pages, shown, w, h, total, got_w, got_h, p;
    UBYTE info[24], *data, *argb = NULL;
    STRPTR name = NULL;
    BOOL ok = FALSE;
    LONG err = DTERROR_INVALID_DATA;

    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, DTA_Name, (ULONG)&name, TAG_DONE) < 1 || !bmh) {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }
    if (!(data = dt_read_source(o, &size)))
        return FALSE;
    if (!dt_service_open(&svc, "doc.render/1")) {
        FreeVec(data);
        SetIoErr(ERROR_NOT_IMPLEMENTED);       /* no card and no paired Cradle */
        return FALSE;
    }

    memset(buf, 0, sizeof buf);
    extra[0] = envNumber("GFOpenImage/DocWidth", 800, 100);
    extra[1] = extra[3] = 0;
    extra[2] = dt_name_hint(name);             /* CSV, Markdown: no signature */
    buf[0].ob_Data = data;
    buf[0].ob_Length = size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    if (dt_service_call(&svc, DR_PROBE, 0, 2, buf, extra, &pages, NULL) != OSERR_OK || get32(info) != DR_KIND_DOCUMENT)
        goto out;
    w = get32(info + 16);
    h = get32(info + 20);
    shown = pages < envNumber("GFOpenImage/DocPages", 8, 1) ? pages : envNumber("GFOpenImage/DocPages", 8, 1);
    if (!w || !h || !shown || w > 32767)
        goto out;
    while (shown > 1 && shown * (h + GAP) - GAP > 32767)   /* a bitmap's height limit */
        shown--;
    if (h > 32767)
        goto out;
    total = shown * (h + GAP) - GAP;

    bmh->bmh_Width = bmh->bmh_PageWidth = w;
    bmh->bmh_Height = bmh->bmh_PageHeight = total;
    bmh->bmh_Depth = 24;
    bmh->bmh_Masking = mskNone;
    SetDTAttrs(o, NULL, NULL,
        DTA_ObjName, (ULONG)(name ? FilePart(name) : (STRPTR)"Document"),
        DTA_NominalHoriz, w,
        DTA_NominalVert, total,
        PDTA_SourceMode, PMODE_V43,
        TAG_DONE);

    if (!(argb = AllocVec(w * h * 4, MEMF_ANY))) {
        err = ERROR_NO_FREE_STORE;
        goto out;
    }
    buf[1].ob_Data = argb;
    buf[1].ob_Length = w * h * 4;
    for (p = 0; p < shown; p++) {
        if (dt_service_call(&svc, DR_RENDER, p, 2, buf, extra, &got_w, &got_h) != OSERR_OK || got_w != w || got_h != h)
            goto out;
        if (!DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, w * 4, 0, p * (h + GAP), w, h))
            goto out;
        if (p + 1 < shown && !writeGap(cl, o, w, p * (h + GAP) + h))
            goto out;
    }
    ok = TRUE;
out:
    if (argb)
        FreeVec(argb);
    dt_service_close(&svc);
    FreeVec(data);
    if (!ok)
        SetIoErr(err);
    return ok;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj && !loadDoc(cl, obj)) {
            LONG err = IoErr();
            CoerceMethod(cl, obj, OM_DISPOSE);
            SetIoErr(err);
            obj = NULL;
        }
        return (ULONG)obj;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
