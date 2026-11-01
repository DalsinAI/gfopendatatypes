/*
 * stream.datatype: the "data" class (datatypes/streamclass.h), a subclass of
 * datatypesclass. It holds the source (a file, memory, a hook, or what the
 * program feeds it), the queue of coded bytes and the whole-buffer result,
 * and answers SDTM_FEED, SDTM_TAKE, SDTM_END, OM_GET and DTM_WRITE. The
 * decoding itself is the subclass's SDTM_CODEC (gzip.datatype,
 * brotli.datatype), sent to the object so that it reaches the subclass.
 * The queue and the loops are sdtcore.c, which the host tests run.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <utility/hooks.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <intuition/classes.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

#include <datatypes/streamclass.h>
#include "dtlib.h"
#include "sdtcore.h"

const char LibName[] = "stream.datatype";
const char LibIdString[] = "stream.datatype 47.1 (23.10.2026) AmiCradle Team";
const UWORD LibVersion = 47;
const UWORD LibRevision = 1;
static const char version[] __attribute__((used)) = "$VER: stream.datatype 47.1 (23.10.2026)";

const char dt_superclass[] = "datatypesclass";
const UWORD dt_superversion = 0;

#define WRITE_CHUNK 8192

struct sdata {
    struct sdt_core core;
    struct sdt_env env;
    Object *self;
    UBYTE *data;                   /* whole-buffer result (SDTA_Data) */
    ULONG size, pos;
    UBYTE whole, inited, pad[2];
    BPTR fh;                       /* streamed file (the object's own handle) */
    const UBYTE *mem;              /* streamed memory */
    ULONG memlen, mempos;
    struct Hook *hook;
    STRPTR format;
};

ULONG dt_instsize = sizeof(struct sdata);

BOOL dt_init(void)
{
    return TRUE;
}

void dt_cleanup(void)
{
}

static void *e_alloc(struct sdt_env *e, ULONG n)
{
    (void)e;
    return AllocVec(n, MEMF_ANY);
}

static void e_free(struct sdt_env *e, void *p)
{
    (void)e;
    FreeVec(p);
}

/* sdtcore's decoder: a SDTM_CODEC to the object, so the subclass gets it */
static LONG codec(void *ctx, struct sdt_call *c)
{
    struct sdata *sd = ctx;
    struct sdtCodec m;
    ULONG rc;
    m.MethodID = SDTM_CODEC;
    m.sdtc_GInfo = NULL;
    m.sdtc_Op = c->op;
    m.sdtc_Flags = c->flags;
    m.sdtc_In = c->in;
    m.sdtc_InLen = c->in_len;
    m.sdtc_Out = c->out;
    m.sdtc_OutLen = c->out_len;
    m.sdtc_InUsed = m.sdtc_OutMade = 0;
    m.sdtc_Error = 0;
    m.sdtc_Tags = NULL;
    m.sdtc_Format = NULL;
    rc = DoMethodA(sd->self, (Msg)&m);
    c->in_used = m.sdtc_InUsed;
    c->out_made = m.sdtc_OutMade;
    c->error = m.sdtc_Error;
    if (m.sdtc_Format)
        sd->format = m.sdtc_Format;
    return (LONG)rc;
}

static ULONG pull_file(void *ctx, UBYTE *buf, ULONG len, LONG *err)
{
    struct sdata *sd = ctx;
    LONG n = Read(sd->fh, buf, len);
    if (n < 0) {
        *err = IoErr();
        return (ULONG)-1;
    }
    return (ULONG)n;
}

static ULONG pull_mem(void *ctx, UBYTE *buf, ULONG len, LONG *err)
{
    struct sdata *sd = ctx;
    ULONG n = sd->memlen - sd->mempos, i;
    (void)err;
    if (n > len)
        n = len;
    for (i = 0; i < n; i++)
        buf[i] = sd->mem[sd->mempos + i];
    sd->mempos += n;
    return n;
}

static ULONG pull_hook(void *ctx, UBYTE *buf, ULONG len, LONG *err)
{
    struct sdata *sd = ctx;
    struct sdtInputMsg m;
    ULONG n;
    m.MethodID = SDTM_INPUT;
    m.sim_Buffer = buf;
    m.sim_Length = len;
    n = CallHookPkt(sd->hook, sd->self, &m);
    if (n == (ULONG)-1)
        *err = IoErr();
    else if (n > len)
        n = len;
    return n;
}

/* The source, the decoder's start and, unless streamed, the whole decode.
 * 0, or an IoErr() code. */
static LONG setup(struct sdata *sd, struct TagItem *tags)
{
    struct sdtCodec m;
    ULONG type = GetTagData(DTA_SourceType, DTST_FILE, tags);
    BOOL streamed = GetTagData(SDTA_Streaming, FALSE, tags) ? TRUE : FALSE;
    LONG err;

    sd->env.alloc = e_alloc;
    sd->env.free = e_free;
    sdt_init(&sd->core, &sd->env, codec, sd);
    m.MethodID = SDTM_CODEC;
    m.sdtc_GInfo = NULL;
    m.sdtc_Op = SDTCO_INIT;
    m.sdtc_Flags = 0;
    m.sdtc_In = NULL;
    m.sdtc_InLen = 0;
    m.sdtc_Out = NULL;
    m.sdtc_OutLen = 0;
    m.sdtc_InUsed = m.sdtc_OutMade = 0;
    m.sdtc_Error = 0;
    m.sdtc_Tags = tags;
    m.sdtc_Format = NULL;
    sd->inited = 1;                /* FREE from now on, whatever INIT says */
    if (DoMethodA(sd->self, (Msg)&m) != SDTC_MORE)
        return m.sdtc_Error ? m.sdtc_Error : ERROR_OBJECT_WRONG_TYPE;
    if (m.sdtc_Format)
        sd->format = m.sdtc_Format;

    if (type == DTST_FILE) {
        GetDTAttrs(sd->self, DTA_Handle, (ULONG)&sd->fh, TAG_DONE);
        if (!sd->fh)
            return ERROR_OBJECT_WRONG_TYPE;
        if (Seek(sd->fh, 0, OFFSET_BEGINNING) < 0)
            return IoErr();
        sdt_set_pull(&sd->core, pull_file, sd);
#ifdef DTST_MEMORY
    } else if (type == DTST_MEMORY) {
        sd->mem = (const UBYTE *)GetTagData(DTA_SourceAddress, 0, tags);
        sd->memlen = GetTagData(DTA_SourceSize, 0, tags);
        if (!sd->mem && sd->memlen)
            return ERROR_REQUIRED_ARG_MISSING;
        sdt_set_pull(&sd->core, pull_mem, sd);
#endif
    } else if (type == DTST_RAM) {
        if ((sd->hook = (struct Hook *)GetTagData(SDTA_InputHook, 0, tags)))
            sdt_set_pull(&sd->core, pull_hook, sd);
        return 0;
    } else
        return ERROR_OBJECT_WRONG_TYPE;
    if (streamed)
        return 0;
    if ((err = sdt_decode_all(&sd->core, &sd->data, &sd->size)))
        return err;
    sd->whole = 1;
    sdt_free(&sd->core);
    return 0;
}

static void teardown(struct sdata *sd)
{
    if (sd->inited) {
        struct sdtCodec m;
        m.MethodID = SDTM_CODEC;
        m.sdtc_GInfo = NULL;
        m.sdtc_Op = SDTCO_FREE;
        m.sdtc_Flags = 0;
        m.sdtc_In = NULL;
        m.sdtc_InLen = 0;
        m.sdtc_Out = NULL;
        m.sdtc_OutLen = 0;
        m.sdtc_Error = 0;
        m.sdtc_Tags = NULL;
        m.sdtc_Format = NULL;
        DoMethodA(sd->self, (Msg)&m);
        sd->inited = 0;
    }
    sdt_free(&sd->core);
    if (sd->data)
        FreeVec(sd->data);
    sd->data = NULL;
}

static ULONG take(struct sdata *sd, struct sdtTake *t)
{
    ULONG n, flags = 0;
    if (sd->whole) {
        ULONG i;
        n = sd->size - sd->pos;
        if (n > t->sdtt_Length)
            n = t->sdtt_Length;
        for (i = 0; i < n; i++)
            ((UBYTE *)t->sdtt_Buffer)[i] = sd->data[sd->pos + i];
        sd->pos += n;
        if (sd->pos == sd->size)
            flags = SDTF_END;
    } else {
        ULONG f;
        n = sdt_take(&sd->core, t->sdtt_Buffer, t->sdtt_Length, &f);
        if (f & SDT_F_END)
            flags |= SDTF_END;
        if (f & SDT_F_HUNGRY)
            flags |= SDTF_HUNGRY;
        if (f & SDT_F_ERROR) {
            flags |= SDTF_ERROR;
            SetIoErr(sd->core.error);
        }
    }
    t->sdtt_Actual = n;
    t->sdtt_Flags = flags;
    t->sdtt_Error = (flags & SDTF_ERROR) ? sd->core.error : 0;
    return n;
}

/* DTM_WRITE, DTWM_RAW: the plain bytes to the file */
static ULONG write_raw(struct sdata *sd, BPTR fh)
{
    if (sd->whole)
        return Write(fh, sd->data, sd->size) == (LONG)sd->size;
    {
        UBYTE *buf = AllocVec(WRITE_CHUNK, MEMF_ANY);
        ULONG ok = FALSE;
        if (!buf) {
            SetIoErr(ERROR_NO_FREE_STORE);
            return FALSE;
        }
        if (!sd->core.pull)
            sdt_end(&sd->core);    /* fed only: everything there is, from here */
        for (;;) {
            ULONG f, n = sdt_take(&sd->core, buf, WRITE_CHUNK, &f);
            if (n && Write(fh, buf, n) != (LONG)n)
                break;
            if (f & SDT_F_ERROR) {
                SetIoErr(sd->core.error);
                break;
            }
            if (f & SDT_F_END) {
                ok = TRUE;
                break;
            }
            if (!n && (f & SDT_F_HUNGRY))
                break;
        }
        FreeVec(buf);
        return ok;
    }
}

ULONG dt_dispatch(Class *cl, Object *o, Msg msg)
{
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *obj = (Object *)DoSuperMethodA(cl, o, msg);
        if (obj) {
            struct sdata *sd = INST_DATA(cl, obj);
            LONG err;
            sd->self = obj;
            if ((err = setup(sd, ((struct opSet *)msg)->ops_AttrList))) {
                CoerceMethod(cl, obj, OM_DISPOSE);
                SetIoErr(err);
                obj = NULL;
            }
        }
        return (ULONG)obj;
    }
    case OM_DISPOSE:
        teardown(INST_DATA(cl, o));
        return DoSuperMethodA(cl, o, msg);
    case OM_GET: {
        struct sdata *sd = INST_DATA(cl, o);
        struct opGet *g = (struct opGet *)msg;
        switch (g->opg_AttrID) {
        case SDTA_Data: *g->opg_Storage = (ULONG)sd->data; return TRUE;
        case SDTA_DataSize: *g->opg_Storage = sd->size; return TRUE;
        case SDTA_TotalIn: *g->opg_Storage = sd->core.total_in; return TRUE;
        case SDTA_TotalOut: *g->opg_Storage = sd->whole ? sd->size : sd->core.total_out; return TRUE;
        case SDTA_Format: *g->opg_Storage = (ULONG)sd->format; return TRUE;
        default: return DoSuperMethodA(cl, o, msg);
        }
    }
    case SDTM_FEED: {
        struct sdata *sd = INST_DATA(cl, o);
        struct sdtFeed *f = (struct sdtFeed *)msg;
        LONG err;
        if (sd->whole) {
            SetIoErr(SDT_ERR_ENDED);
            return FALSE;
        }
        if ((err = sdt_feed(&sd->core, f->sdtf_Data, f->sdtf_Length))) {
            SetIoErr(err);
            return FALSE;
        }
        return TRUE;
    }
    case SDTM_TAKE:
        return take(INST_DATA(cl, o), (struct sdtTake *)msg);
    case SDTM_END:
        sdt_end(&((struct sdata *)INST_DATA(cl, o))->core);
        return TRUE;
    case DTM_WRITE: {
        struct dtWrite *w = (struct dtWrite *)msg;
        if (w->dtw_Mode != DTWM_RAW || !w->dtw_FileHandle) {
            SetIoErr(ERROR_NOT_IMPLEMENTED);
            return FALSE;
        }
        return write_raw(INST_DATA(cl, o), w->dtw_FileHandle);
    }
    case SDTM_CODEC: {
        /* no decoder below: refuse */
        struct sdtCodec *m = (struct sdtCodec *)msg;
        if (m->sdtc_Op == SDTCO_FREE)
            return SDTC_DONE;
        m->sdtc_Error = ERROR_OBJECT_WRONG_TYPE;
        return SDTC_ERROR;
    }
    default:
        return DoSuperMethodA(cl, o, msg);
    }
}
