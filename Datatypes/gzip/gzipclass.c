/*
 * gzip.datatype: gzip, zlib-wrapped and raw deflate, a stream.datatype
 * subclass (datatypes/streamclass.h). It answers SDTM_CODEC only; the
 * inflate is z.library's (GFOpenZlib), opened for each object, so no zlib is
 * linked in and nothing is kept in globals. SDTA_Wrapper (OM_NEW) names
 * the wrapper; by default it is told from the first bytes (gzcore.c).
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
#include "dtlib.h"
#include "sdtglue.h"
#include "gzcore.h"

const char LibName[] = "gzip.datatype";
const char LibIdString[] = "gzip.datatype 47.1 (23.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: gzip.datatype 47.1 (23.10.2026)";

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

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    if (msg->MethodID == SDTM_CODEC)
        return codec(INST_DATA(cl, o), (struct sdtCodec *)msg);
    return DoSuperMethodA(cl, o, msg);
}
