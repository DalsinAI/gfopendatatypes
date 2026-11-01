/*
 * brotli.datatype: Brotli (RFC 7932), a stream.datatype subclass
 * (datatypes/streamclass.h), with our own decoder (brdecode.c). It answers
 * SDTM_CODEC only.
 *
 * Brotli has no magic bytes: a program names the class (DTA_BaseName
 * "brotli", as a browser does from Content-Encoding: br), and
 * DEVS:DataTypes/Brotli (build.sh) takes files by their name, .br.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>

#include <datatypes/streamclass.h>
#include "dtlib.h"
#include "sdtglue.h"
#include "brdecode.h"

const char LibName[] = "brotli.datatype";
const char LibIdString[] = "brotli.datatype 47.1 (23.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: brotli.datatype 47.1 (23.10.2026)";

const char dt_superclass[] = STREAMDTCLASS;
const UWORD dt_superversion = 47;

struct bdata {
    struct sdt_env env;
    struct br_state *br;
};

ULONG dt_instsize = sizeof(struct bdata);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

/* Fast RAM when there is any: the window can be megabytes. */
static void *e_alloc(struct sdt_env *e, ULONG n)
{
    void *p = AllocVec(n, MEMF_FAST);
    (void)e;
    return p ? p : AllocVec(n, MEMF_ANY);
}

static void e_free(struct sdt_env *e, void *p)
{
    (void)e;
    FreeVec(p);
}

static ULONG codec(struct bdata *b, struct sdtCodec *m)
{
    struct sdt_call c;
    sdt_call_in(&c, m);
    switch (m->sdtc_Op) {
    case SDTCO_INIT:
        b->env.alloc = e_alloc;
        b->env.free = e_free;
        if (!(b->br = br_new(&b->env))) {
            m->sdtc_Error = SDT_ERR_MEMORY;
            return SDTC_ERROR;
        }
        m->sdtc_Format = (STRPTR)"brotli";
        return SDTC_MORE;
    case SDTCO_RUN:
        if (!b->br) {
            m->sdtc_Error = SDT_ERR_MEMORY;
            return SDTC_ERROR;
        }
        return sdt_call_out(m, &c, br_run(b->br, &c));
    default:                           /* SDTCO_FREE */
        br_free(b->br);
        b->br = NULL;
        return SDTC_DONE;
    }
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    if (msg->MethodID == SDTM_CODEC)
        return codec(INST_DATA(cl, o), (struct sdtCodec *)msg);
    return DoSuperMethodA(cl, o, msg);
}
