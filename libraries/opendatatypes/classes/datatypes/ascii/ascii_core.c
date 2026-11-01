/* GFOpenDatatypes ascii.datatype core.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 * Clean-room; see ascii_core.h for what this is written from.
 */
#include "ascii_core.h"

/* A growing buffer */
struct grow { char *p; odt_u32 n, size; };

static int room(const struct odt_cenv *env, struct grow *g, odt_u32 more)
{
    char *q;
    odt_u32 size = g->size ? g->size : 4096, i;
    /* more checked before it is added, so a huge one can't wrap round */
    if (g->n >= ASC_MAXTEXT || more > ASC_MAXTEXT - g->n - 1) return 0;
    if (g->n + more + 1 <= g->size) return 1;
    while (size < g->n + more + 1) size *= 2;
    if (!(q = env->alloc(env, size))) return 0;
    for (i = 0; i < g->n; i++) q[i] = g->p[i];
    if (g->p) env->free(env, g->p);
    g->p = q;
    g->size = size;
    return 1;
}

static odt_s32 fail(const struct odt_cenv *env, struct grow *g, odt_s32 err)
{
    if (g->p) env->free(env, g->p);
    g->p = 0;
    env->set_io_err(env, err);
    return err;
}

odt_s32 asc_read_source(const struct odt_cenv *env, void *obj, char **buf, odt_u32 *len)
{
    odt_u32 st = ODT_DTST_FILE, handle = 0, dtv = 0;
    struct grow g = { 0, 0, 0 };
    int iff = 0;
    dtc_get(env, obj, ODT_DTA_SourceType, &st);
    dtc_get(env, obj, ODT_DTA_Handle, &handle);
    if (dtc_get(env, obj, ODT_DTA_DataType, &dtv) && dtv) {
        const struct odt_DataType *dt = (const struct odt_DataType *)dtv;
        if (dt->dtn_Header && (dt->dtn_Header->dth_Flags & ODT_DTF_TYPE_MASK) == ODT_DTF_IFF) iff = 1;
    }
    if (st == ODT_DTST_CLIPBOARD) iff = 1;
    *buf = 0;
    *len = 0;
    if (!room(env, &g, 0)) return fail(env, &g, DTC_ERROR_NO_FREE_STORE);
    if (st == ODT_DTST_RAM) {
        /* nothing to read: an empty text, TDTA_Buffer to come (ABI test) */
    } else if (iff) {
        /* FTXT: every CHRS chunk, one after the other (ABI test: OS 3.2
         * may keep only the first) */
        odt_s32 r;
        if (!handle) return fail(env, &g, DTC_ERROR_REQUIRED_ARG_MISSING);
        if (env->iff_stop(env, (void *)handle, TDT_ID_FTXT, TDT_ID_CHRS) != 0)
            return fail(env, &g, DTC_ERROR_NO_FREE_STORE);
        for (;;) {
            odt_u32 type = 0;
            odt_s32 size = 0, got;
            r = env->iff_parse(env, (void *)handle, ASC_IFFPARSE_SCAN);
            if (r == ASC_IFFERR_EOC) continue;
            if (r != 0) break;
            env->iff_current(env, (void *)handle, &type, &size);
            if (size <= 0) continue;
            if (!room(env, &g, (odt_u32)size)) return fail(env, &g, DTC_ERROR_NO_FREE_STORE);
            got = env->iff_read_chunk(env, (void *)handle, g.p + g.n, size);
            if (got > 0) g.n += (odt_u32)got;
        }
        if (r != ASC_IFFERR_EOF) return fail(env, &g, ODT_DTERROR_INVALID_DATA);
    } else if (st == ODT_DTST_MEMORY) {
        odt_u32 at = 0, size = 0, i;
        dtc_get(env, obj, ODT_DTA_SourceAddress, &at);
        dtc_get(env, obj, ODT_DTA_SourceSize, &size);
        if (!at && size) return fail(env, &g, DTC_ERROR_REQUIRED_ARG_MISSING);
        if (!room(env, &g, size)) return fail(env, &g, DTC_ERROR_NO_FREE_STORE);
        for (i = 0; i < size; i++) g.p[i] = ((const char *)at)[i];
        g.n = size;
    } else {
        /* a file: from its start, read to the end */
        odt_s32 got;
        if (!handle) return fail(env, &g, DTC_ERROR_REQUIRED_ARG_MISSING);
        env->seek(env, handle, 0, ASC_OFFSET_BEGINNING);
        for (;;) {
            if (!room(env, &g, 4096)) return fail(env, &g, DTC_ERROR_NO_FREE_STORE);
            got = env->read(env, handle, g.p + g.n, (odt_s32)(g.size - g.n - 1));
            if (got < 0) return fail(env, &g, env->io_err(env));
            if (got == 0) break;
            g.n += (odt_u32)got;
        }
    }
    g.p[g.n] = 0;
    *buf = g.p;
    *len = g.n;
    return 0;
}

odt_u32 asc_inst_size(void) { return 0; }

odt_u32 asc_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    if (*(odt_u32 *)msg == ODT_OM_NEW) {
        struct odt_opSet *ops = msg;
        void *o = (void *)env->do_super(env, cl, obj, msg);
        char *buf;
        odt_u32 len;
        odt_s32 err;
        struct odt_TagItem tags[3];
        struct odt_opSet set;
        if (!o) return 0;
        /* a buffer given at OM_NEW is the text: no source is read (ABI test) */
        if (dtc_find_tag(ops->ops_AttrList, TDT_TDTA_Buffer)) return (odt_u32)o;
        if ((err = asc_read_source(env, o, &buf, &len)) != 0) {
            struct odt_dtGeneral d;
            d.MethodID = ODT_OM_DISPOSE;
            d.dtg_GInfo = 0;
            env->coerce(env, cl, o, &d);
            env->set_io_err(env, err);
            return 0;
        }
        tags[0].ti_Tag = TDT_TDTA_Buffer;    tags[0].ti_Data = (odt_u32)buf;
        tags[1].ti_Tag = TDT_TDTA_BufferLen; tags[1].ti_Data = len;
        tags[2].ti_Tag = ODT_TAG_DONE;       tags[2].ti_Data = 0;
        set.MethodID = ODT_OM_SET;
        set.ops_AttrList = tags;
        set.ops_GInfo = ops->ops_GInfo;
        env->do_super(env, cl, o, &set);     /* text.datatype keeps its own copy */
        env->free(env, buf);
        {
            /* no copy (no memory): no object with an empty text */
            odt_u32 kept = 0;
            if (len && (!dtc_get(env, o, TDT_TDTA_Buffer, &kept) || !kept)) {
                struct odt_dtGeneral d;
                d.MethodID = ODT_OM_DISPOSE;
                d.dtg_GInfo = 0;
                env->coerce(env, cl, o, &d);
                env->set_io_err(env, DTC_ERROR_NO_FREE_STORE);
                return 0;
            }
        }
        return (odt_u32)o;
    }
    return env->do_super(env, cl, obj, msg);
}
