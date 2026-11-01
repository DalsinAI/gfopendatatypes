/*
 * gzip.datatype: gzip, zlib-wrapped and raw deflate, a stream.datatype
 * subclass (datatypes/streamclass.h). It answers SDTM_CODEC only; the
 * inflate is z.library's (GFOpenZlib), opened for each object, so no zlib is
 * linked in and nothing is kept in globals. SDTA_Wrapper (OM_NEW) names
 * the wrapper; by default it is told from the first bytes (gzcore.c).
 *
 * DTM_WRITE with SDTA_WriteCoded in its dtw_AttrList (47.2) writes the
 * plain bytes deflated again in the wrapper they came in (gzip, zlib or raw
 * deflate), at OIA_Compression's level when given (datatypes/gfopenimage.h);
 * without it, stream.datatype writes the plain bytes.
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
const char LibIdString[] = "gzip.datatype 47.2 (23.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 2;
static const char version[] __attribute__((used)) = "$VER: gzip.datatype 47.2 (23.10.2026)";

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
        if (!(g->zbase = OpenLibrary((CONST_STRPTR)ZL_NAME, ZL_VERSION))) {
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
    default:                           /* SDTCO_FREE */
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

#define WRITE_ROOM 16384UL

/* DTM_WRITE with SDTA_WriteCoded: the plain bytes deflated again. 0, or an IoErr() code. */
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
        return ERROR_OBJECT_WRONG_TYPE;     /* streaming: the bytes have been handed out */
    if (!data)
        data = (UBYTE *)"";                 /* nothing in: a valid empty stream out */
    kind = format && !strcmp((const char *)format, "zlib") ? GZ_ZLIB
         : format && !strcmp((const char *)format, "deflate") ? GZ_RAW : GZ_GZIP;
    if (!(work = AllocVec(WRITE_ROOM, MEMF_ANY)))
        return ERROR_NO_FREE_STORE;
    if (!(zbase = OpenLibrary((CONST_STRPTR)ZL_NAME, ZL_VERSION))) {
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
