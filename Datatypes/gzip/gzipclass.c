/* gzip.datatype: gzip, zlib-wrapped and raw deflate, a stream.datatype
 * subclass (datatypes/streamclass.h). It answers SDTM_CODEC and SDTM_CODE.
 * The inflate and the deflate are z.library's (GFOpenZlib), opened for each
 * object, so no zlib is linked in and nothing is kept in globals.
 * SDTA_Wrapper (OM_NEW) names the wrapper; by default it is told from the
 * first bytes (gzcore.c).
 *
 * DTM_WRITE with SDTA_WriteCoded in its dtw_AttrList (47.2) writes the
 * plain bytes deflated again in the wrapper they came in (gzip, zlib or raw
 * deflate), at OIA_Compression's level when given (datatypes/gfopenimage.h);
 * without it, stream.datatype writes the plain bytes.
 *
 * SDTM_CODE is the same deflate for a program that has the plain bytes and
 * no file: one call, any program, the coded bytes come back. The program
 * FreeVec()s them when it did not pass its own buffer.
 *
 * DEVS:DataTypes/GZip (build.sh) matches gzip's 1f 8b 08, so any program
 * opens a .gz file through datatypes; HTTP bodies name the class.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <datatypes/streamclass.h>
#include <datatypes/gfopenimage.h>
#include <proto/dos.h>
#include <proto/datatypes.h>
#include <string.h>
#include "dtlib.h"
#include "sdtglue.h"
#include "gzcore.h"

const char LibName[] = "gzip.datatype";
const char LibIdString[] = "gzip.datatype 47.3 (6.11.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 3;
static const char version[] __attribute__((used)) = "$VER: gzip.datatype 47.3 (6.11.2026)";

const char dt_superclass[] = STREAMDTCLASS;
const UWORD dt_superversion = 47;

struct gdata {
    struct gz_state gz;
    struct Library *zbase;
};

ULONG dt_instsize = sizeof(struct gdata);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static ULONG codec(struct gdata *g, struct sdtCodec *m)
{
    struct sdt_call c;
    LONG rc;
    sdt_call_in(&c, m);
    switch (m->sdtc_Op) {
    case SDTCO_INIT:
        if (!(g->zbase = zl_Open())) {
            m->sdtc_Error = SDT_ERR_NOLIB;
            return SDTC_ERROR;
        }
        gz_init(&g->gz, g->zbase, (LONG)GetTagData(SDTA_Wrapper, SDTW_AUTO, m->sdtc_Tags));
        return SDTC_MORE;
    case SDTCO_RUN:
        if (!g->zbase) {
            m->sdtc_Error = SDT_ERR_NOLIB;
            return SDTC_ERROR;
        }
        rc = gz_run(&g->gz, &c);
        m->sdtc_Format = (STRPTR)gz_format(&g->gz);
        return sdt_call_out(m, &c, rc);
    default:
        if (g->zbase) {
            gz_free(&g->gz);
            CloseLibrary(g->zbase);
            g->zbase = NULL;
        }
        return SDTC_DONE;
    }
}

static LONG put_fh(void *h, const UBYTE *p, ULONG n)
{
    if (Write((BPTR)h, (APTR)p, n) != (LONG)n)
        return IoErr() ? IoErr() : ERROR_DISK_FULL;
    return 0;
}

struct codebuf {
    UBYTE *p;
    ULONG n, cap;
    UBYTE *fixed;
    ULONG fixedlen;
};

static LONG put_code(void *h, const UBYTE *p, ULONG n)
{
    struct codebuf *g = h;
    UBYTE *np;
    ULONG i, cap;

    if (g->fixed) {
        if (g->n + n > g->fixedlen)
            return ERROR_BUFFER_OVERFLOW;
        for (i = 0; i < n; i++)
            g->fixed[g->n + i] = p[i];
        g->n += n;
        return 0;
    }
    if (g->n + n > g->cap) {
        cap = g->cap ? g->cap : 256;
        while (cap < g->n + n)
            cap *= 2;
        if (!(np = AllocVec(cap, MEMF_ANY)))
            return ERROR_NO_FREE_STORE;
        for (i = 0; i < g->n; i++)
            np[i] = g->p[i];
        FreeVec(g->p);
        g->p = np;
        g->cap = cap;
    }
    for (i = 0; i < n; i++)
        g->p[g->n + i] = p[i];
    g->n += n;
    return 0;
}

/* One compress, for every program. gz_encode is z.library; this only hands
 * it the bytes and gives them back. */
static ULONG code_plain(struct sdtCode *m)
{
    struct codebuf g;
    UBYTE *work;
    struct Library *zbase;
    LONG kind, err;

    if (!m->sdtk_Plain' && m->sdtk_PlainLen) {
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return FALSE;
    }
    kind = m->sdtk_Wrapper;
    if (kind != SDTW_GZIP && kind != SDTW_ZLIB && kind != SDTW_RAW)
        kind = SDTW_GZIP;
    g.p = NULL;
    g.n = g.cap = 0;
    g.fixed = m->sdtk_Out;
    g.fixedlen = m->sdtk_OutLen;
    if (!(work = AllocVec(16384, MEMF_ANY)) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return FALSE;
    }
    if (!(zbase = zl_Open())) {
        FreeVec(work);
        SetIoErr(SDT_ERR_NOLIB);
        return FALSE;
    }
    err = gz_encode(zbase, kind, m->sdtk_Level,
                    m->sdtk_Plain ? m->sdtk_Plain : (const UBYTE *)"",
                    m->sdtk_PlainLen, put_code, &g, work, 16384);
    CloseLibrary(zbase);
    FreeVec(work);
    if (err) {
        if (!g.fixed)
            FreeVec(g.p);
        SetIoErr(err);
        return FALSE;
    }
    if (!g.fixed)
        m->sdtk_Out = g.p;
    m->sdtk_Actual = g.n;
    return TRUE;
}

#define WRITE_ROOM 16384UL

static LONG writeCoded(Object *o, struct dtWrite *w)
{
    UBYTE *data = NULL, *work;
    ULONG size = 0;
    STRPTR format = NULL;
    struct Library *zbase;
    LONG kind, err;

    if (!w->dtw_FileHandle)
        return ERROR_REQUIRED_ARG_MISSING;
    GetDTAttrs(o, SDTA_Data, (ULONG)&data, SDTA_DataSize, (ULONG)&size, SDTA_Format, (ULONG)&format, TAG_DONE);
    if (!data && size)
        return ERROR_OBJECT_WRONG_TYPE;
    if (!data)
        data = (UBYTE *)"";
    kind = format && !strcmp((const char *)format, "zlib") ? GZ_ZLIB
         : format && !strcmp((const char *)format, "deflate") ? GZ_RAW : GZ_GZIP;
    if (!(work = AllocVec(WRITE_ROOM, MEMF_ANY)))
        return ERROR_NO_FREE_STORE;
    if (!(zbase = zl_Open())) {
        FreeVec(work);
        return SDT_ERR_NOLIB;
    }
    err = gz_encode(zbase, kind, (LONG)GetTagData(OIA_Compression, (ULONG)-1, w->dtw_AttrList),
                    data, size, put_fh, (void *)w->dtw_FileHandle, work, WRITE_ROOM);
    CloseLibrary(zbase);
    FreeVec(work);
    return err;
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    if (msg->MethodID == SDTM_CODEC)
        return codec(INST_DATA(cl, o), (struct sdtCodec *)msg);
    if (msg->MethodID == SDTM_CODE)
        return code_plain((struct sdtCode *)msg);
    if (msg->MethodID == DTM_WRITE) {
        struct dtWrite *w = (struct dtWrite *)msg;
        LONG err;
        if (w->dtw_Mode == DTWM_RAW && GetTagData(SDTA_WriteCoded, FALSE, w->dtw_AttrList)) {
            if ((err = writeCoded(o, w))) {
                SetIoErr(err);
                return FALSE;
            }
            return TRUE;
        }
    }
    return DoSuperMethodA(cl, o, msg);
}
