/*
 * gfopendoc.datatype: documents (DOCX, XLSX, PPTX, ODT, ODS, ODP, DOC, XLS,
 * PPT, RTF, WordPerfect, PostScript, EPS, EPUB, Markdown, CSV) for AmigaOS
 * 3.x, shown as a picture of their pages, one under the other, like a PDF
 * viewer's continuous view. The pages are laid out by LibreOffice (or
 * Ghostscript, or pandoc then LibreOffice) through the doc.render/1 service
 * (DalsinAI/gfopenservice docs/DOC_RENDER.md) on the services card or a
 * paired Cradle, at ENV:GFOpenImage/DocWidth pixels wide (default 800), the
 * first ENV:GFOpenImage/DocPages pages (default 8).
 *
 * With neither a card nor a Cradle the document does not open yet (a text
 * view laid out on the 68k comes later).
 *
 * 47.2: the file goes to the service once (kept there by PROBE, named by
 * each RENDER), not with every page, and the next page is rendered while
 * this one goes into the picture, when there is memory for a second page.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
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
const char LibIdString[] = "gfopendoc.datatype 47.2 (7.11.2026) AmiCradle Team, laid out by doc.render/1";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gfopendoc.datatype 47.2 (7.11.2026)";

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
    struct dt_file file = { NULL, 0, 0, { 0 } };
    struct OSBuffer buf[4], abuf[4];
    ULONG extra[4], aextra[4], size = 0, pages, shown, w, h, total, got_w, got_h, p;
    UBYTE info[24], *data, *argb = NULL, *next = NULL;
    LONG ahead = -1;
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
    file.data = data;
    file.size = size;
    buf[1].ob_Data = info;
    buf[1].ob_Length = sizeof info;
    /* The file goes once: kept on the host by PROBE, named by RENDER. */
    if (dt_file_call(&svc, &file, DR_PROBE, 0, 2, buf, extra, &pages, NULL, FALSE) != OSERR_OK
        || get32(info) != DR_KIND_DOCUMENT)
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
    if (shown > 1)
        next = AllocVec(w * h * 4, MEMF_ANY);  /* the read-ahead's page: none when memory is short */
    for (p = 0; p < shown; p++) {
        BOOL have = FALSE;
        if (ahead == (LONG)p) {
            /* Rendered while the page before went into the picture. */
            if (dt_service_wait(&svc, &got_w, &got_h) == OSERR_OK && got_w == w && got_h == h) {
                UBYTE *t = argb;
                argb = next;
                next = t;
                have = TRUE;
            }
            ahead = -1;
            if (p + 1 == shown)
                file.kept = 0;                 /* let go with that RENDER (DT_DROP) */
        }
        if (!have) {
            buf[0].ob_Data = NULL;
            buf[0].ob_Length = 0;
            buf[1].ob_Data = argb;
            buf[1].ob_Length = w * h * 4;
            if (dt_file_call(&svc, &file, DR_RENDER, p, 2, buf, extra, &got_w, &got_h, p + 1 == shown) != OSERR_OK
                || got_w != w || got_h != h)
                goto out;
        }
        /* The next page, while this one goes into the picture. */
        if (next && p + 1 < shown) {
            memcpy(aextra, extra, sizeof aextra);
            memset(abuf, 0, sizeof abuf);
            if (file.kept)
                aextra[3] = DT_KEPT(file.kept) | (p + 2 == shown ? DT_DROP : 0);
            else {
                abuf[0].ob_Data = data;
                abuf[0].ob_Length = size;
            }
            abuf[1].ob_Data = next;
            abuf[1].ob_Length = w * h * 4;
            if (dt_service_send(&svc, DR_RENDER, p + 1, 2, abuf, aextra))
                ahead = p + 1;
        }
        if (!DoSuperMethod(cl, o, PDTM_WRITEPIXELARRAY, (ULONG)argb, PBPAFMT_ARGB, w * 4, 0, p * (h + GAP), w, h))
            goto out;
        if (p + 1 < shown && !writeGap(cl, o, w, p * (h + GAP) + h))
            goto out;
    }
    ok = TRUE;
out:
    if (svc.sent)
        dt_service_wait(&svc, NULL, NULL);     /* before its page goes */
    dt_file_release(&svc, &file);
    dt_service_close(&svc);
    if (argb)
        FreeVec(argb);
    if (next)
        FreeVec(next);
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
