/* OpenDatatypes: datatypesclass, the superclass of every datatype class,
 * in plain C with no Amiga headers. See dtclass_env.h.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: written from the datatypesclass autodoc, datatypes.library's
 * autodocs and the NDK 3.2 layouts, never from Kickstart's, Hyperion's or
 * AROS's code. Where the autodocs leave a corner open the comment says
 * "ABI test": DatatypesABITest's OBJ part records OS 3.2's answer.
 *
 * datatypesclass is a gadgetclass: an object is a gadget whose SpecialInfo
 * is its struct DTSpecialInfo. This file holds what the class does itself:
 * opening the data source at OM_NEW (a file, the clipboard, memory) and
 * handing it out as DTA_Handle, every DTA_ attribute it keeps, the scroll
 * numbers and their notification, the domain at GM_LAYOUT, drag scrolling
 * and drag selecting (GM_GOACTIVE to GM_GOINACTIVE), and the DTM_ methods'
 * default answers. It also holds the object half of datatypes.library's
 * calls that are more than a DoMethod (DoAsyncLayout's process body,
 * printing's, StartDragSelect, GetDTAttrsA's count), so qemu can test them.
 */
#include "dtclass_env.h"

#define NULLP ((void *)0)
#define END_METHOD (~0UL)

/* dos/dos.h */
#define MODE_OLDFILE 1005
#define OFFSET_BEGINNING (-1)
#define ERROR_NO_FREE_STORE 103
#define ERROR_REQUIRED_ARG_MISSING 116
#define ERROR_NOT_IMPLEMENTED 236

/* ---- small helpers ----------------------------------------------------------- */

static odt_u32 slen(const char *s)
{
    odt_u32 n = 0;
    if (s) while (s[n]) n++;
    return n;
}

static void mcopy(void *d, const void *s, odt_u32 n)
{
    odt_u8 *dd = d;
    const odt_u8 *ss = s;
    while (n--) *dd++ = *ss++;
}

static void mzero(void *d, odt_u32 n)
{
    odt_u8 *dd = d;
    while (n--) *dd++ = 0;
}

/* a copy of s in env memory, or NULL for NULL (or no memory) */
static char *sdup(const struct odt_cenv *env, const char *s)
{
    odt_u32 n;
    char *d;
    if (!s) return NULLP;
    n = slen(s) + 1;
    if ((d = env->alloc(env, n)) != NULLP) mcopy(d, s, n);
    return d;
}

static void sset(const struct odt_cenv *env, char **at, const char *s)
{
    char *d = sdup(env, s);
    if (*at) env->free(env, *at);
    *at = d;
}

static odt_s32 clamp_top(odt_s32 top, odt_s32 vis, odt_s32 tot)
{
    /* ABI test: whether datatypesclass clamps the top itself (we do, as
     * the scroller it notifies would) */
    if (top > tot - vis) top = tot - vis;
    if (top < 0) top = 0;
    return top;
}

static odt_u32 do_msg2(const struct odt_cenv *env, void *obj, odt_u32 id, void *a)
{
    struct { odt_u32 id; void *a; } m;
    m.id = id;
    m.a = a;
    return env->do_method(env, obj, &m);
}

/* datatypesclass's own methods, DTA_Methods' answer for a class that has
 * none of its own (ABI test: OS 3.2's list) */
static const odt_u32 dt_methods[] = {
    ODT_OM_NEW, ODT_OM_GET, ODT_OM_SET, ODT_OM_UPDATE, ODT_OM_DISPOSE,
    ODT_GM_LAYOUT, ODT_GM_HITTEST, ODT_GM_GOACTIVE, ODT_GM_HANDLEINPUT, ODT_GM_GOINACTIVE, ODT_GM_RENDER,
    ODT_DTM_FRAMEBOX, ODT_DTM_PROCLAYOUT, ODT_DTM_ASYNCLAYOUT, ODT_DTM_REMOVEDTOBJECT,
    ODT_DTM_SELECT, ODT_DTM_CLEARSELECTED, ODT_DTM_WRITE,
    END_METHOD
};

odt_u32 odt_dtclass_inst_size(void) { return sizeof(struct odt_dtinst); }

/* ---- notification ----------------------------------------------------------- */

/* OM_NOTIFY through the object's own class (gadgetclass sends it on to
 * ICA_TARGET): the scroll numbers, then extra, then GA_ID */
static void notify(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi, int interim,
                   const struct odt_TagItem *extra, int nextra)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_TagItem t[14];
    struct odt_opUpdate m;
    int n = 0, i;
    t[n].ti_Tag = ODT_DTA_TopVert;      t[n++].ti_Data = (odt_u32)d->si.si_TopVert;
    t[n].ti_Tag = ODT_DTA_VisibleVert;  t[n++].ti_Data = (odt_u32)d->si.si_VisVert;
    t[n].ti_Tag = ODT_DTA_TotalVert;    t[n++].ti_Data = (odt_u32)d->si.si_TotVert;
    t[n].ti_Tag = ODT_DTA_TopHoriz;     t[n++].ti_Data = (odt_u32)d->si.si_TopHoriz;
    t[n].ti_Tag = ODT_DTA_VisibleHoriz; t[n++].ti_Data = (odt_u32)d->si.si_VisHoriz;
    t[n].ti_Tag = ODT_DTA_TotalHoriz;   t[n++].ti_Data = (odt_u32)d->si.si_TotHoriz;
    for (i = 0; i < nextra && i < 6; i++) t[n++] = extra[i];
    t[n].ti_Tag = ODT_GA_ID;            t[n++].ti_Data = ((struct odt_Gadget *)obj)->GadgetID;
    t[n].ti_Tag = ODT_TAG_DONE;         t[n].ti_Data = 0;
    m.MethodID = ODT_OM_NOTIFY;
    m.opu_AttrList = t;
    m.opu_GInfo = gi;
    m.opu_Flags = interim ? ODT_OPUF_INTERIM : 0;
    env->do_method(env, obj, &m);
}

/* A layout or print process's notification at its end, proc_lock held.
 * Its target may dispose the object from inside it, on this process; the
 * object can't go then (OM_NOTIFY's caller, ic's forwarding, still uses
 * it, and the class library is still on the stack), so the dispose waits
 * (d->dispose_pending) until the object's last process has unwound its
 * notification, and that process finishes it, once however many asked
 * (proc_end). Every process notifying has its own record, linked in the
 * object under si_Lock, so a dispose can tell it comes from inside. */
#define PN_DISPOSE   1                  /* OM_DISPOSE came: the subclasses have had theirs */
#define PN_DTDISPOSE 2                  /* DisposeDTObject came: the library's record goes too */
#define PN_TAKEN     4                  /* the last process is disposing it (with the bits it took) */
#define PN_OWNED     8                  /* a dispose from outside the notifications is disposing it */
#define PN_OWNED_DT  16                 /* ... and it is DisposeDTObject, which frees the record */
#define PN_RECORD    32                 /* the record's free is settled */
#define PN_BUSY      (PN_OWNED | PN_TAKEN)
struct proc_note {
    struct proc_note *next;
    void *task;
};
static void proc_notify(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi,
                        const struct odt_TagItem *extra, int nextra, struct proc_note *pn)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct proc_note **pp;
    pn->task = env->this_task ? env->this_task(env) : NULLP;
    env->sem_obtain(env, &d->si.si_Lock);
    pn->next = d->notifying;
    d->notifying = pn;
    env->sem_release(env, &d->si.si_Lock);
    notify(env, obj, gi, 0, extra, nextra);
    env->sem_obtain(env, &d->si.si_Lock);
    for (pp = (struct proc_note **)&d->notifying; *pp; pp = &(*pp)->next)
        if (*pp == pn) {
            *pp = pn->next;
            break;
        }
    env->sem_release(env, &d->si.si_Lock);
}

static void *this_task(const struct odt_cenv *env)
{
    return env->this_task ? env->this_task(env) : NULLP;
}

/* The running task's record, when it is inside its completion
 * notification; si_Lock held */
static struct proc_note *own_note_locked(const struct odt_cenv *env, struct odt_dtinst *d)
{
    struct proc_note *pn;
    void *me;
    if (!env->this_task || !d->notifying) return NULLP;
    me = env->this_task(env);
    for (pn = d->notifying; pn && pn->task != me; pn = pn->next)
        ;
    return pn;
}

/* GM_RENDER with GREDRAW_UPDATE in the gadget's own RastPort: what a
 * scroll does (the class draws the difference from si_OTop*) */
static void render_update(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi)
{
    struct odt_gpRender m;
    void *rp;
    if (!gi || !env->obtain_rp || !(rp = env->obtain_rp(env, gi))) return;
    m.MethodID = ODT_GM_RENDER;
    m.gpr_GInfo = gi;
    m.gpr_RPort = rp;
    m.gpr_Redraw = (odt_s32)ODT_GREDRAW_UPDATE;
    env->do_method(env, obj, &m);
    env->release_rp(env, rp);
}

/* ---- the data source ------------------------------------------------------------ */

static int is_iff_type(const struct odt_DataType *dt)
{
    return dt && dt->dtn_Header && (dt->dtn_Header->dth_Flags & ODT_DTF_TYPE_MASK) == ODT_DTF_IFF;
}

/* Opens what DTA_SourceType names; 0, or the IoErr to fail OM_NEW with */
static odt_s32 open_source(const struct odt_cenv *env, struct odt_dtinst *d, odt_u32 unit, void *given)
{
    switch (d->source_type) {
    case ODT_DTST_FILE: {
        odt_u32 fh;
        if (!d->name) return ERROR_REQUIRED_ARG_MISSING;
        if (!env->open || !(fh = env->open(env, d->name, MODE_OLDFILE)))
            return env->io_err ? env->io_err(env) : ODT_DTERROR_COULDNT_OPEN;
        if (is_iff_type(d->datatype)) {
            void *iff = env->iff_open_dos ? env->iff_open_dos(env, fh, 0) : NULLP;
            if (!iff) { env->close(env, fh); return ODT_DTERROR_COULDNT_OPEN; }
            d->handle = iff;
            d->handle_kind = ODT_HANDLE_IFF;
            d->fh = fh;
        } else {
            d->handle = (void *)fh;
            d->handle_kind = ODT_HANDLE_FILE;
        }
        return 0;
    }
    case ODT_DTST_CLIPBOARD: {
        /* the clipboard is always IFF (FTXT, ILBM...); the unit is the
         * number NewDTObjectA had for a name, or DTA_Handle is the
         * program's IFFHandle on it (the autodoc), used as it is and left
         * to the program to free (ABI test) */
        void *iff;
        if (given) {
            d->handle = given;
            d->handle_kind = ODT_HANDLE_GIVEN;
            return 0;
        }
        iff = env->iff_open_clip ? env->iff_open_clip(env, unit, 0) : NULLP;
        if (!iff) return ODT_DTERROR_COULDNT_OPEN_CLIPBOARD;
        d->handle = iff;
        d->handle_kind = ODT_HANDLE_IFF;
        return 0;
    }
    case ODT_DTST_MEMORY:
        if (!d->source_address || !d->source_size) return ERROR_REQUIRED_ARG_MISSING;
        if (is_iff_type(d->datatype)) {
            void *iff = env->iff_open_mem ? env->iff_open_mem(env, d->source_address, d->source_size) : NULLP;
            if (!iff) return ERROR_NO_FREE_STORE;
            d->handle = iff;
            d->handle_kind = ODT_HANDLE_IFF;
        }
        /* ABI test: a non-IFF type's DTA_Handle for memory. Ours is NULL:
         * the class reads DTA_SourceAddress and DTA_SourceSize. */
        return 0;
    case ODT_DTST_RAM:
        return 0;                           /* made empty, filled by the program */
    default:
        /* DTST_HOTLINK and anything else: ABI test */
        return ODT_DTERROR_NOT_AVAILABLE;
    }
}

static void close_source(const struct odt_cenv *env, struct odt_dtinst *d)
{
    if (d->handle_kind == ODT_HANDLE_IFF) {
        env->iff_close(env, d->handle);
        if (d->fh) env->close(env, d->fh);
    } else if (d->handle_kind == ODT_HANDLE_FILE) {
        env->close(env, (odt_u32)d->handle);
    }
    d->handle = NULLP;
    d->handle_kind = ODT_HANDLE_NONE;
    d->fh = 0;
    /* DTA_Handle's lock became the object's (the autodoc's NOTE) */
    if (d->lock && env->unlock) env->unlock(env, d->lock);
    d->lock = 0;
}

/* ---- attributes ------------------------------------------------------------------ */

/* The geometry tags the NDK keeps for old programs: DTA_Left and the rest
 * are GA_Left and the rest (ABI test: OS 3.2 still takes them) */
static int geometry(struct odt_Gadget *g, odt_u32 tag, odt_u32 v)
{
    odt_u16 rel = 0;
    switch (tag) {
    case ODT_DTA_Left:      g->LeftEdge = (odt_s16)v; g->Flags &= ~ODT_GFLG_RELRIGHT; return 1;
    case ODT_DTA_Top:       g->TopEdge = (odt_s16)v; g->Flags &= ~ODT_GFLG_RELBOTTOM; return 1;
    case ODT_DTA_Width:     g->Width = (odt_s16)v; g->Flags &= ~ODT_GFLG_RELWIDTH; return 1;
    case ODT_DTA_Height:    g->Height = (odt_s16)v; g->Flags &= ~ODT_GFLG_RELHEIGHT; return 1;
    case ODT_DTA_RelRight:  g->LeftEdge = (odt_s16)v; rel = ODT_GFLG_RELRIGHT; break;
    case ODT_DTA_RelBottom: g->TopEdge = (odt_s16)v; rel = ODT_GFLG_RELBOTTOM; break;
    case ODT_DTA_RelWidth:  g->Width = (odt_s16)v; rel = ODT_GFLG_RELWIDTH; break;
    case ODT_DTA_RelHeight: g->Height = (odt_s16)v; rel = ODT_GFLG_RELHEIGHT; break;
    default: return 0;
    }
    g->Flags |= rel;
    return 1;
}

static void flag(odt_u16 *f, odt_u16 bit, odt_u32 on)
{
    if (on) *f |= bit; else *f &= (odt_u16)~bit;
}

/* OM_SET and OM_UPDATE's own attributes: 1 when something visible changed */
static odt_u32 set_attrs(const struct odt_cenv *env, void *obj, struct odt_TagItem *tags,
                         struct odt_GadgetInfo *gi, int update, int initial)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_DTSpecialInfo *si = &d->si;
    const struct odt_TagItem *tp = tags, *t;
    odt_s32 otv = si->si_TopVert, oth = si->si_TopHoriz;
    odt_s32 ovv = si->si_VisVert, ovh = si->si_VisHoriz, otov = si->si_TotVert, otoh = si->si_TotHoriz;
    odt_u32 refresh = 0;
    int top_set = 0, extra_n = 0;
    struct odt_TagItem extra[2];

    while ((t = odt_next_tag(&tp)) != NULLP) {
        odt_u32 v = t->ti_Data;
        if (geometry((struct odt_Gadget *)obj, t->ti_Tag, v)) continue;
        switch (t->ti_Tag) {
        case ODT_DTA_TopVert:      si->si_TopVert = (odt_s32)v; top_set = 1; break;
        case ODT_DTA_VisibleVert:  si->si_VisVert = (odt_s32)v; break;
        case ODT_DTA_TotalVert:    si->si_TotVert = (odt_s32)v; break;
        case ODT_DTA_VertUnit:     si->si_VertUnit = (odt_s32)v ? (odt_s32)v : 1; break;
        case ODT_DTA_TopHoriz:     si->si_TopHoriz = (odt_s32)v; top_set = 1; break;
        case ODT_DTA_VisibleHoriz: si->si_VisHoriz = (odt_s32)v; break;
        case ODT_DTA_TotalHoriz:   si->si_TotHoriz = (odt_s32)v; break;
        case ODT_DTA_HorizUnit:    si->si_HorizUnit = (odt_s32)v ? (odt_s32)v : 1; break;
        case ODT_DTA_NodeName:     sset(env, &d->node_name, (const char *)v); break;
        case ODT_DTA_Title:        sset(env, &d->title, (const char *)v); break;
        case ODT_DTA_TextAttr:     d->text_attr = (void *)v; break;
        case ODT_DTA_ObjName:      sset(env, &d->obj_name, (const char *)v); break;
        case ODT_DTA_ObjAuthor:    sset(env, &d->obj_author, (const char *)v); break;
        case ODT_DTA_ObjAnnotation:sset(env, &d->obj_annotation, (const char *)v); break;
        case ODT_DTA_ObjCopyright: sset(env, &d->obj_copyright, (const char *)v); break;
        case ODT_DTA_ObjVersion:   sset(env, &d->obj_version, (const char *)v); break;
        case ODT_DTA_ObjectID:     d->object_id = v; break;
        case ODT_DTA_UserData:     d->user_data = v; break;
        case ODT_DTA_TotalPVert:   d->total_pvert = (odt_s32)v; break;
        case ODT_DTA_TotalPHoriz:  d->total_phoriz = (odt_s32)v; break;
        case ODT_DTA_NominalVert:  d->nominal_vert = (odt_s32)v; break;
        case ODT_DTA_NominalHoriz: d->nominal_horiz = (odt_s32)v; break;
        case ODT_DTA_SelectDomain:
            if (v) d->select_domain = *(const struct odt_IBox *)v;
            break;
        case ODT_DTA_Immediate:    flag(&d->flags, ODT_DTF_IMMEDIATE, v); break;
        case ODT_DTA_Repeat:       flag(&d->flags, ODT_DTF_REPEAT, v); break;
        case ODT_DTA_ControlPanel: flag(&d->flags, ODT_DTF_PANEL, v); break;
        case ODT_DTA_Busy:         flag(&d->flags, ODT_DTF_BUSY, v); break;
        case ODT_DTA_Sync:         flag(&d->flags, ODT_DTF_SYNC, v); break;
        case ODT_DTA_ARexxPortName:sset(env, &d->arexx_port, (const char *)v); break;
        case ODT_DTA_DestCols:     d->dest_cols = v; break;
        case ODT_DTA_DestRows:     d->dest_rows = v; break;
        case ODT_DTA_Special:      d->special = v; break;
        case ODT_DTA_RastPort:     d->rastport = (void *)v; break;
        case ODT_DTA_PrinterStatus:d->printer_status = v; break;
        /* A class's load failing: it sets these and they go out to the
         * program (ABI test: OS 3.2 notifies them as here) */
        case ODT_DTA_ErrorLevel:
            d->error_level = v;
            if (!initial && extra_n < 2) { extra[extra_n].ti_Tag = t->ti_Tag; extra[extra_n++].ti_Data = v; }
            break;
        case ODT_DTA_ErrorNumber:
            d->error_number = v;
            if (!initial && extra_n < 2) { extra[extra_n].ti_Tag = t->ti_Tag; extra[extra_n++].ti_Data = v; }
            break;
        default:
            break;
        }
    }
    if (top_set) {
        si->si_TopVert = clamp_top(si->si_TopVert, si->si_VisVert, si->si_TotVert);
        si->si_TopHoriz = clamp_top(si->si_TopHoriz, si->si_VisHoriz, si->si_TotHoriz);
    }
    if (si->si_TopVert != otv || si->si_TopHoriz != oth) {
        si->si_OTopVert = otv;
        si->si_OTopHoriz = oth;
        refresh = 1;
        /* a scroller moved it: draw the difference now, unless a layout
         * holds the object (ABI test: OS 3.2 draws at OM_UPDATE) */
        if (update && !(si->si_Flags & ODT_DTSIF_LAYOUT)) render_update(env, obj, gi);
    }
    if (initial) return refresh;
    /* the program's scrollers follow what the class or the program set;
     * not what came from them (OM_UPDATE), which would loop */
    if (!update && (refresh || si->si_VisVert != ovv || si->si_VisHoriz != ovh || si->si_TotVert != otov ||
                    si->si_TotHoriz != otoh || extra_n))
        notify(env, obj, gi, 0, extra, extra_n);
    return refresh;
}

static int get_attr(const struct odt_cenv *env, void *obj, odt_u32 id, odt_u32 *store)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_DTSpecialInfo *si = &d->si;
    const struct odt_DataTypeHeader *h = d->datatype ? d->datatype->dtn_Header : NULLP;
    odt_u32 v;
    switch (id) {
    case ODT_DTA_TopVert:      v = (odt_u32)si->si_TopVert; break;
    case ODT_DTA_VisibleVert:  v = (odt_u32)si->si_VisVert; break;
    case ODT_DTA_TotalVert:    v = (odt_u32)si->si_TotVert; break;
    case ODT_DTA_VertUnit:     v = (odt_u32)si->si_VertUnit; break;
    case ODT_DTA_TopHoriz:     v = (odt_u32)si->si_TopHoriz; break;
    case ODT_DTA_VisibleHoriz: v = (odt_u32)si->si_VisHoriz; break;
    case ODT_DTA_TotalHoriz:   v = (odt_u32)si->si_TotHoriz; break;
    case ODT_DTA_HorizUnit:    v = (odt_u32)si->si_HorizUnit; break;
    case ODT_DTA_NodeName:     v = (odt_u32)d->node_name; break;
    case ODT_DTA_Title:        v = (odt_u32)d->title; break;
    case ODT_DTA_TextAttr:     v = (odt_u32)d->text_attr; break;
    case ODT_DTA_Methods:      v = (odt_u32)dt_methods; break;
    case ODT_DTA_TriggerMethods: v = 0; break;
    case ODT_DTA_PrinterStatus:v = d->printer_status; break;
    case ODT_DTA_PrinterProc:  v = (odt_u32)d->printer_proc; break;
    case ODT_DTA_LayoutProc:   v = (odt_u32)d->layout_proc; break;
    case ODT_DTA_Busy:         v = (d->flags & ODT_DTF_BUSY) || (si->si_Flags & ODT_DTSIF_LAYOUT) ? 1 : 0; break;
    case ODT_DTA_Sync:         v = (d->flags & ODT_DTF_SYNC) ? 1 : 0; break;
    case ODT_DTA_BaseName:     v = (odt_u32)(h ? h->dth_BaseName : NULLP); break;
    case ODT_DTA_GroupID:      v = h ? h->dth_GroupID : 0; break;
    case ODT_DTA_ErrorLevel:   v = d->error_level; break;
    case ODT_DTA_ErrorNumber:  v = d->error_number; break;
    case ODT_DTA_ErrorString:
        v = (odt_u32)(d->error_number && env->dt_string ? env->dt_string(env, d->error_number) : NULLP);
        break;
    case ODT_DTA_Immediate:    v = (d->flags & ODT_DTF_IMMEDIATE) ? 1 : 0; break;
    case ODT_DTA_Repeat:       v = (d->flags & ODT_DTF_REPEAT) ? 1 : 0; break;
    case ODT_DTA_ControlPanel: v = (d->flags & ODT_DTF_PANEL) ? 1 : 0; break;
    case ODT_DTA_SourceAddress:v = (odt_u32)d->source_address; break;
    case ODT_DTA_SourceSize:   v = d->source_size; break;
    case ODT_DTA_Name:         v = (odt_u32)d->name; break;
    case ODT_DTA_SourceType:   v = d->source_type; break;
    case ODT_DTA_Handle:       v = (odt_u32)d->handle; break;
    case ODT_DTA_DataType:     v = (odt_u32)d->datatype; break;
    case ODT_DTA_Domain:       v = (odt_u32)&d->domain; break;
    case ODT_DTA_ObjName:      v = (odt_u32)d->obj_name; break;
    case ODT_DTA_ObjAuthor:    v = (odt_u32)d->obj_author; break;
    case ODT_DTA_ObjAnnotation:v = (odt_u32)d->obj_annotation; break;
    case ODT_DTA_ObjCopyright: v = (odt_u32)d->obj_copyright; break;
    case ODT_DTA_ObjVersion:   v = (odt_u32)d->obj_version; break;
    case ODT_DTA_ObjectID:     v = d->object_id; break;
    case ODT_DTA_UserData:     v = d->user_data; break;
    case ODT_DTA_FrameInfo:    v = (odt_u32)&d->frame; break;
    case ODT_DTA_SelectDomain: v = (odt_u32)&d->select_domain; break;
    case ODT_DTA_TotalPVert:   v = (odt_u32)d->total_pvert; break;
    case ODT_DTA_TotalPHoriz:  v = (odt_u32)d->total_phoriz; break;
    case ODT_DTA_NominalVert:  v = (odt_u32)d->nominal_vert; break;
    case ODT_DTA_NominalHoriz: v = (odt_u32)d->nominal_horiz; break;
    case ODT_DTA_DestCols:     v = d->dest_cols; break;
    case ODT_DTA_DestRows:     v = d->dest_rows; break;
    case ODT_DTA_Special:      v = d->special; break;
    case ODT_DTA_RastPort:     v = (odt_u32)d->rastport; break;
    case ODT_DTA_ARexxPortName:v = (odt_u32)d->arexx_port; break;
    case ODT_DTA_Left:         v = (odt_u32)(odt_s32)((struct odt_Gadget *)obj)->LeftEdge; break;
    case ODT_DTA_Top:          v = (odt_u32)(odt_s32)((struct odt_Gadget *)obj)->TopEdge; break;
    case ODT_DTA_Width:        v = (odt_u32)(odt_s32)((struct odt_Gadget *)obj)->Width; break;
    case ODT_DTA_Height:       v = (odt_u32)(odt_s32)((struct odt_Gadget *)obj)->Height; break;
    default: return 0;
    }
    *store = v;
    return 1;
}

/* ---- OM_NEW and OM_DISPOSE ------------------------------------------------------ */

static void free_strings(const struct odt_cenv *env, struct odt_dtinst *d)
{
    char **s[] = { &d->name, &d->obj_name, &d->obj_author, &d->obj_annotation, &d->obj_copyright,
                   &d->obj_version, &d->title, &d->node_name, &d->arexx_port };
    unsigned i;
    for (i = 0; i < sizeof s / sizeof s[0]; i++) {
        if (*s[i]) env->free(env, *s[i]);
        *s[i] = NULLP;
    }
}

/* Waits for a running layout to stop: asks it, then takes si_Lock, which
 * it holds while it runs */
static void stop_layout(const struct odt_cenv *env, struct odt_dtinst *d)
{
    if (d->layout_proc && env->signal_stop) env->signal_stop(env, d->layout_proc);
    if (d->si.si_Flags & (ODT_DTSIF_LAYOUT | ODT_DTSIF_LAYOUTPROC)) {
        env->sem_obtain(env, &d->si.si_Lock);
        env->sem_release(env, &d->si.si_Lock);
    }
}

static void stop_print(const struct odt_cenv *env, struct odt_dtinst *d)
{
    if (d->printer_proc && env->signal_stop) env->signal_stop(env, d->printer_proc);
}

/* Disposal: asks a running print and layout to stop, then waits for them
 * to end. Each process holds proc_lock, shared, from before the call that
 * started it returns (odt_proc_hold) to its last touch of the object, so
 * taking it, always, waits out a process however far it has got. */
static void end_procs(const struct odt_cenv *env, struct odt_dtinst *d)
{
    stop_print(env, d);
    stop_layout(env, d);
    env->sem_obtain(env, &d->proc_lock);
    env->sem_release(env, &d->proc_lock);
}

static void *om_new(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, struct odt_opSet *m)
{
    struct odt_dtinst *d;
    struct odt_Gadget *g;
    const struct odt_TagItem *tp = m->ops_AttrList, *t;
    odt_u32 unit = 0, name = 0, handle = 0;
    odt_s32 err;
    void *o = (void *)env->do_super(env, cl, obj, m);
    if (!o) return NULLP;
    g = o;
    d = ODT_INST_DATA(cl, o);
    env->sem_init(env, &d->si.si_Lock);
    env->sem_init(env, &d->proc_lock);
    d->notifying = NULLP;
    d->procs = 0;
    d->dispose_pending = 0;
    d->dispose_cl = NULLP;
    d->dispose_owner = NULLP;
    d->si.si_VertUnit = d->si.si_HorizUnit = 1;
    d->source_type = ODT_DTST_FILE;
    g->SpecialInfo = &d->si;
    while ((t = odt_next_tag(&tp)) != NULLP) {
        switch (t->ti_Tag) {
        case ODT_DTA_Name:          name = t->ti_Data; break;
        case ODT_DTA_SourceType:    d->source_type = t->ti_Data; break;
        case ODT_DTA_DataType:      d->datatype = (struct odt_DataType *)t->ti_Data; break;
        case ODT_DTA_SourceAddress: d->source_address = (const void *)t->ti_Data; break;
        case ODT_DTA_SourceSize:    d->source_size = t->ti_Data; break;
        case ODT_DTA_Handle:        handle = t->ti_Data; break;
        }
    }
    /* DTA_Handle: a lock for DTST_FILE (the object's from now; the name is
     * still what is opened, the autodoc's BUGS), an IFFHandle for
     * DTST_CLIPBOARD, nothing for the others */

    /* for the clipboard, the name is the unit's number */
    if (d->source_type == ODT_DTST_CLIPBOARD) unit = name;
    else sset(env, &d->name, (const char *)name);
    set_attrs(env, o, m->ops_AttrList, NULLP, 0, 1);
    if ((err = open_source(env, d, unit,
                           d->source_type == ODT_DTST_CLIPBOARD ? (void *)handle : NULLP)) != 0) {
        /* the superclass's half goes; the class gets OM_DISPOSE for ours */
        env->coerce(env, cl, o, &(odt_u32){ ODT_OM_DISPOSE });
        if (env->set_io_err) env->set_io_err(env, err);
        return NULLP;
    }
    if (d->source_type == ODT_DTST_FILE) d->lock = handle;
    return o;
}

static odt_u32 om_dispose(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *m)
{
    struct odt_dtinst *d = ODT_INST_DATA(cl, obj);
    env->sem_obtain(env, &d->si.si_Lock);
    if (own_note_locked(env, d)) {      /* from a completion notification: later (proc_end) */
        d->dispose_pending |= PN_DISPOSE;
        d->dispose_cl = cl;
        env->sem_release(env, &d->si.si_Lock);
        return 0;
    }
    /* another task is disposing it: joined (a second DisposeObject is the
     * program's double free; the subclasses have had theirs again) */
    if ((d->dispose_pending & PN_BUSY) && d->dispose_owner != this_task(env)) {
        env->sem_release(env, &d->si.si_Lock);
        return 0;
    }
    /* from outside: this is the object's disposer, before it waits for
     * the processes, so a notification's DisposeDTObject joins it */
    if (!(d->dispose_pending & PN_BUSY)) {
        d->dispose_pending |= PN_OWNED;
        d->dispose_owner = this_task(env);
    }
    env->sem_release(env, &d->si.si_Lock);
    end_procs(env, d);
    close_source(env, d);
    free_strings(env, d);
    return env->do_super(env, cl, obj, m);
}

/* ---- the gadget ----------------------------------------------------------------------- */

static void gadget_box(struct odt_Gadget *g, struct odt_GadgetInfo *gi, struct odt_IBox *box)
{
    box->Left = g->LeftEdge;
    box->Top = g->TopEdge;
    box->Width = g->Width;
    box->Height = g->Height;
    if (!gi) return;
    box->Left = (odt_s16)(box->Left + gi->gi_Domain.Left);
    box->Top = (odt_s16)(box->Top + gi->gi_Domain.Top);
    if (g->Flags & ODT_GFLG_RELSPECIAL) return;
    if (g->Flags & ODT_GFLG_RELRIGHT) box->Left = (odt_s16)(box->Left + gi->gi_Domain.Width - 1);
    if (g->Flags & ODT_GFLG_RELBOTTOM) box->Top = (odt_s16)(box->Top + gi->gi_Domain.Height - 1);
    if (g->Flags & ODT_GFLG_RELWIDTH) box->Width = (odt_s16)(box->Width + gi->gi_Domain.Width);
    if (g->Flags & ODT_GFLG_RELHEIGHT) box->Height = (odt_s16)(box->Height + gi->gi_Domain.Height);
}

/* GM_LAYOUT and DTM_PROCLAYOUT's own part: the domain, and what is
 * visible in it in the object's units; DTSIF_NEWSIZE when it changed */
static odt_u32 layout(const struct odt_cenv *env, void *obj, struct odt_gpLayout *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_IBox box;
    gadget_box((struct odt_Gadget *)obj, m->gpl_GInfo, &box);
    if (box.Width < 0) box.Width = 0;
    if (box.Height < 0) box.Height = 0;
    if (m->gpl_Initial || box.Width != d->domain.Width || box.Height != d->domain.Height)
        d->si.si_Flags |= ODT_DTSIF_NEWSIZE;
    d->domain = box;
    /* ABI test: OS 3.2 sets the visible numbers here or leaves them to
     * the class; ours sets them and the class may set its own */
    d->si.si_VisVert = box.Height / (d->si.si_VertUnit > 0 ? d->si.si_VertUnit : 1);
    d->si.si_VisHoriz = box.Width / (d->si.si_HorizUnit > 0 ? d->si.si_HorizUnit : 1);
    d->si.si_TopVert = clamp_top(d->si.si_TopVert, d->si.si_VisVert, d->si.si_TotVert);
    d->si.si_TopHoriz = clamp_top(d->si.si_TopHoriz, d->si.si_VisHoriz, d->si.si_TotHoriz);
    d->flags &= (odt_u16)~ODT_DTF_SYNC;
    return 1;
}

static int scrollable(const struct odt_DTSpecialInfo *si)
{
    return si->si_TotVert > si->si_VisVert || si->si_TotHoriz > si->si_VisHoriz;
}

/* the band, in window coordinates, from where the button went down to
 * the mouse, both inside the domain */
static void band(const struct odt_cenv *env, struct odt_dtinst *d, struct odt_GadgetInfo *gi)
{
    void *rp;
    odt_s32 x0 = d->domain.Left + d->drag_x, y0 = d->domain.Top + d->drag_y;
    odt_s32 x1 = d->domain.Left + d->drag_lx, y1 = d->domain.Top + d->drag_ly;
    if (!env->complement_box || !env->obtain_rp || !(rp = env->obtain_rp(env, gi))) return;
    env->complement_box(env, rp, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0);
    env->release_rp(env, rp);
    d->flags ^= ODT_DTF_BAND;
}

static odt_s16 inside(odt_s16 v, odt_s16 size)
{
    if (v < 0) return 0;
    if (v >= size) return (odt_s16)(size > 0 ? size - 1 : 0);
    return v;
}

static odt_u32 go_active(const struct odt_cenv *env, void *obj, struct odt_gpInput *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_DTSpecialInfo *si = &d->si;
    if (si->si_Flags & ODT_DTSIF_LAYOUT) return ODT_GMR_NOREUSE;   /* busy */
    if (!m->gpi_IEvent) return ODT_GMR_NOREUSE;                    /* ActivateGadget: nothing to do */
    d->drag_x = d->drag_lx = inside(m->gpi_Mouse.x, d->domain.Width);
    d->drag_y = d->drag_ly = inside(m->gpi_Mouse.y, d->domain.Height);
    if (si->si_Flags & ODT_DTSIF_DRAGSELECT) {
        band(env, d, m->gpi_GInfo);
        return ODT_GMR_MEACTIVE;
    }
    if (!scrollable(si)) return ODT_GMR_NOREUSE;
    si->si_Flags |= ODT_DTSIF_DRAGGING;
    d->drag_top_vert = si->si_TopVert;
    d->drag_top_horiz = si->si_TopHoriz;
    return ODT_GMR_MEACTIVE;
}

static void finish_select(const struct odt_cenv *env, void *obj, struct odt_GadgetInfo *gi)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_dtSelect s;
    odt_s32 ox = d->si.si_TopHoriz * d->si.si_HorizUnit, oy = d->si.si_TopVert * d->si.si_VertUnit;
    if (d->flags & ODT_DTF_BAND) band(env, d, gi);
    d->si.si_Flags &= ~ODT_DTSIF_DRAGSELECT;
    s.MethodID = ODT_DTM_SELECT;
    s.dts_GInfo = gi;
    /* the object's own pixels: the band plus where the object is scrolled
     * to (ABI test: OS 3.2's units for dts_Select) */
    s.dts_Select.MinX = (odt_s16)((d->drag_x < d->drag_lx ? d->drag_x : d->drag_lx) + ox);
    s.dts_Select.MinY = (odt_s16)((d->drag_y < d->drag_ly ? d->drag_y : d->drag_ly) + oy);
    s.dts_Select.MaxX = (odt_s16)((d->drag_x < d->drag_lx ? d->drag_lx : d->drag_x) + ox);
    s.dts_Select.MaxY = (odt_s16)((d->drag_y < d->drag_ly ? d->drag_ly : d->drag_y) + oy);
    env->do_method(env, obj, &s);
}

static odt_u32 handle_input(const struct odt_cenv *env, void *obj, struct odt_gpInput *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_DTSpecialInfo *si = &d->si;
    struct odt_InputEvent *ie = m->gpi_IEvent;
    odt_s16 x, y;
    if (!ie || ie->ie_Class != ODT_IECLASS_RAWMOUSE) return ODT_GMR_MEACTIVE;
    x = inside(m->gpi_Mouse.x, d->domain.Width);
    y = inside(m->gpi_Mouse.y, d->domain.Height);
    switch (ie->ie_Code) {
    case ODT_SELECTUP:
        if (si->si_Flags & ODT_DTSIF_DRAGSELECT) {
            if (d->flags & ODT_DTF_BAND) band(env, d, m->gpi_GInfo);   /* off, where it was drawn */
            d->drag_lx = x;
            d->drag_ly = y;
            finish_select(env, obj, m->gpi_GInfo);
        } else if (si->si_Flags & ODT_DTSIF_DRAGGING) {
            si->si_Flags &= ~ODT_DTSIF_DRAGGING;
            notify(env, obj, m->gpi_GInfo, 0, NULLP, 0);
        }
        if (m->gpi_Termination) *m->gpi_Termination = 0;
        return ODT_GMR_NOREUSE | ODT_GMR_VERIFY;
    case ODT_MENUDOWN:
        /* the other button gives up: the band goes, a drag goes back */
        if (si->si_Flags & ODT_DTSIF_DRAGGING) {
            odt_s32 tv = si->si_TopVert, th = si->si_TopHoriz;
            si->si_TopVert = d->drag_top_vert;
            si->si_TopHoriz = d->drag_top_horiz;
            if (tv != si->si_TopVert || th != si->si_TopHoriz) {
                si->si_OTopVert = tv;
                si->si_OTopHoriz = th;
                render_update(env, obj, m->gpi_GInfo);
                notify(env, obj, m->gpi_GInfo, 0, NULLP, 0);
            }
        }
        return ODT_GMR_NOREUSE;
    case ODT_IECODE_NOBUTTON:
        if (x == d->drag_lx && y == d->drag_ly) return ODT_GMR_MEACTIVE;
        if (si->si_Flags & ODT_DTSIF_DRAGSELECT) {
            if (d->flags & ODT_DTF_BAND) band(env, d, m->gpi_GInfo);
            d->drag_lx = x;
            d->drag_ly = y;
            band(env, d, m->gpi_GInfo);
        } else if (si->si_Flags & ODT_DTSIF_DRAGGING) {
            /* the contents follow the mouse: moving it down shows what is above */
            odt_s32 tv = si->si_TopVert, th = si->si_TopHoriz;
            odt_s32 vu = si->si_VertUnit > 0 ? si->si_VertUnit : 1, hu = si->si_HorizUnit > 0 ? si->si_HorizUnit : 1;
            d->drag_lx = x;
            d->drag_ly = y;
            si->si_TopVert = clamp_top(d->drag_top_vert - (y - d->drag_y) / vu, si->si_VisVert, si->si_TotVert);
            si->si_TopHoriz = clamp_top(d->drag_top_horiz - (x - d->drag_x) / hu, si->si_VisHoriz, si->si_TotHoriz);
            if (tv != si->si_TopVert || th != si->si_TopHoriz) {
                si->si_OTopVert = tv;
                si->si_OTopHoriz = th;
                render_update(env, obj, m->gpi_GInfo);
                notify(env, obj, m->gpi_GInfo, 1, NULLP, 0);
            }
        }
        return ODT_GMR_MEACTIVE;
    default:
        return ODT_GMR_MEACTIVE;
    }
}

static odt_u32 go_inactive(const struct odt_cenv *env, void *obj, struct odt_gpGoInactive *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    if (d->flags & ODT_DTF_BAND) band(env, d, m->gpgi_GInfo);
    d->si.si_Flags &= ~(ODT_DTSIF_DRAGGING | ODT_DTSIF_DRAGSELECT);
    return 0;
}

/* ---- the DTM_ methods' defaults ----------------------------------------------- */

static odt_u32 frame_box(const struct odt_cenv *env, void *obj, struct odt_dtFrameBox *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    odt_u32 n = m->dtf_SizeFrameInfo < sizeof(struct odt_FrameInfo) ? m->dtf_SizeFrameInfo
                                                                       : sizeof(struct odt_FrameInfo);
    if (!m->dtf_FrameInfo) return 0;
    /* ABI test: datatypesclass's own answer. Ours: FRAMEF_SPECIFY keeps
     * the frame given; otherwise the frame wanted is the contents' own
     * (the class fills it), else the one kept, else nothing */
    if (m->dtf_FrameFlags & ODT_FRAMEF_SPECIFY) {
        mzero(&d->frame, sizeof d->frame);
        mcopy(&d->frame, m->dtf_FrameInfo, n);
        return 1;
    }
    if (m->dtf_ContentsInfo && m->dtf_ContentsInfo != m->dtf_FrameInfo) {
        mcopy(m->dtf_FrameInfo, m->dtf_ContentsInfo, n);
        return 1;
    }
    mcopy(m->dtf_FrameInfo, &d->frame, n);
    return 1;
}

/* DTWM_RAW for a class that has no writer: the source's bytes as they are
 * (ABI test: whether OS 3.2's datatypesclass copies or fails) */
static odt_u32 write_raw(const struct odt_cenv *env, void *obj, struct odt_dtWrite *m)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    odt_u8 buf[256];
    odt_s32 n;
    if (m->dtw_Mode != ODT_DTWM_RAW || !m->dtw_FileHandle) goto fail;
    if (d->source_type == ODT_DTST_MEMORY && d->source_address) {
        if (env->write(env, m->dtw_FileHandle, d->source_address, (odt_s32)d->source_size) != (odt_s32)d->source_size)
            return 0;
        return 1;
    }
    {
        /* a file, or the file under an IFF handle we opened: its bytes from
         * the start, the position put back after for whoever still reads */
        odt_u32 fh = d->handle_kind == ODT_HANDLE_FILE ? (odt_u32)d->handle
                   : d->handle_kind == ODT_HANDLE_IFF ? d->fh : 0;
        odt_s32 was;
        if (!fh) goto fail;
        if ((was = env->seek(env, fh, 0, OFFSET_BEGINNING)) < 0) return 0;
        while ((n = env->read(env, fh, buf, sizeof buf)) > 0)
            if (env->write(env, m->dtw_FileHandle, buf, n) != n) break;
        env->seek(env, fh, was, OFFSET_BEGINNING);
        return n == 0;
    }
fail:
    if (env->set_io_err) env->set_io_err(env, ERROR_NOT_IMPLEMENTED);
    return 0;
}

/* ---- the dispatcher ------------------------------------------------------------- */

odt_u32 odt_dtclass_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg)
{
    odt_u32 id = *(odt_u32 *)msg;
    struct odt_dtinst *d;
    switch (id) {
    case ODT_OM_NEW:
        return (odt_u32)om_new(env, cl, obj, msg);
    case ODT_OM_DISPOSE:
        return om_dispose(env, cl, obj, msg);
    case ODT_OM_SET:
    case ODT_OM_UPDATE: {
        struct odt_opSet *m = msg;
        odt_u32 r = env->do_super(env, cl, obj, msg);
        return r | set_attrs(env, obj, m->ops_AttrList, m->ops_GInfo, id == ODT_OM_UPDATE, 0);
    }
    case ODT_OM_GET: {
        struct odt_opGet *m = msg;
        if (get_attr(env, obj, m->opg_AttrID, m->opg_Storage)) return 1;
        return env->do_super(env, cl, obj, msg);
    }
    case ODT_GM_LAYOUT:
        env->do_super(env, cl, obj, msg);
        return layout(env, obj, msg);
    case ODT_DTM_PROCLAYOUT:
        /* ABI test: OS 3.2's default; ours is GM_LAYOUT's part. The class
         * follows with its DTM_ASYNCLAYOUT, on the program's process. */
        return layout(env, obj, msg);
    case ODT_GM_RENDER:
        return 0;                           /* the class draws */
    case ODT_GM_GOACTIVE:
        return go_active(env, obj, msg);
    case ODT_GM_HANDLEINPUT:
        return handle_input(env, obj, msg);
    case ODT_GM_GOINACTIVE:
        return go_inactive(env, obj, msg);
    case ODT_DTM_FRAMEBOX:
        return frame_box(env, obj, msg);
    case ODT_DTM_ASYNCLAYOUT:
        return 0;                           /* the class lays out */
    case ODT_DTM_REMOVEDTOBJECT:
        d = ODT_INST_DATA(cl, obj);
        stop_print(env, d);
        stop_layout(env, d);
        if (d->flags & ODT_DTF_BAND) band(env, d, ((struct odt_dtGeneral *)msg)->dtg_GInfo);
        d->flags &= (odt_u16)~ODT_DTF_ADDED;
        return 1;
    case ODT_DTM_SELECT:
        d = ODT_INST_DATA(cl, obj);
        d->select = ((struct odt_dtSelect *)msg)->dts_Select;
        d->flags |= ODT_DTF_SELECTED;
        d->si.si_Flags |= ODT_DTSIF_HIGHLIGHT;
        return 1;
    case ODT_DTM_CLEARSELECTED:
        d = ODT_INST_DATA(cl, obj);
        d->flags &= (odt_u16)~ODT_DTF_SELECTED;
        d->si.si_Flags &= ~ODT_DTSIF_HIGHLIGHT;
        return 1;
    case ODT_DTM_ABORTPRINT:
        stop_print(env, ODT_INST_DATA(cl, obj));
        return 1;
    case ODT_DTM_WRITE:
        return write_raw(env, obj, msg);
    case ODT_DTM_COPY:
    case ODT_DTM_PRINT:
    case ODT_DTM_GOTO:
    case ODT_DTM_TRIGGER:
    case ODT_DTM_OBTAINDRAWINFO:
    case ODT_DTM_DRAW:
    case ODT_DTM_RELEASEDRAWINFO:
    case ODT_DTM_NEWMEMBER:
    case ODT_DTM_DISPOSEMEMBER:
        /* the class's to do; datatypesclass has nothing (ABI test) */
        return 0;
    default:
        return env->do_super(env, cl, obj, msg);
    }
}

/* ---- the library's object calls -------------------------------------------------- */

static odt_u32 layout_body(const struct odt_cenv *env, void *obj, struct odt_gpLayout *gpl, struct proc_note *pn)
{
    int process = pn != NULLP;
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_gpLayout m;
    struct odt_TagItem t[2];
    odt_u32 r;
    int stopped;
    env->sem_obtain(env, &d->si.si_Lock);
    /* stopped before it began: a newer layout has replaced this one and
     * owns the object's state */
    if (process && env->check_stop && env->check_stop(env)) {
        env->sem_release(env, &d->si.si_Lock);
        return 0;
    }
    d->si.si_Flags |= ODT_DTSIF_LAYOUT;
    m.MethodID = ODT_DTM_ASYNCLAYOUT;
    m.gpl_GInfo = gpl->gpl_GInfo;
    m.gpl_Initial = gpl->gpl_Initial;
    r = env->do_method(env, obj, &m);
    stopped = process && env->check_stop ? env->check_stop(env) : 0;
    d->si.si_Flags &= ~(ODT_DTSIF_LAYOUT | ODT_DTSIF_LAYOUTPROC | ODT_DTSIF_NEWSIZE);
    d->layout_proc = NULLP;
    if (!stopped) d->flags |= ODT_DTF_SYNC;
    d->flags &= (odt_u16)~ODT_DTF_BUSY;
    env->sem_release(env, &d->si.si_Lock);
    if (stopped) return r;
    /* the program hears it's done (DTA_Sync) and redraws the object */
    t[0].ti_Tag = ODT_DTA_Busy; t[0].ti_Data = 0;
    t[1].ti_Tag = ODT_DTA_Sync; t[1].ti_Data = 1;
    if (pn) proc_notify(env, obj, gpl->gpl_GInfo, t, 2, pn);
    else notify(env, obj, gpl->gpl_GInfo, 0, t, 2);
    return r;
}

/* A process's end, after its completion notification: proc_lock goes.
 * A dispose that came from inside a notification, this one's or another
 * process's, is done by the object's last process, once: OM_DISPOSE from
 * datatypesclass up (the subclasses having had theirs), and the library's
 * record left to the caller (DisposeDTObject: *dispose). An earlier
 * process asks the others to stop and leaves it. */
static void proc_end(const struct odt_cenv *env, void *obj, int *dispose)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_IClass *cl;
    odt_u32 m = ODT_OM_DISPOSE;
    int what = 0, last;
    odt_u16 p;
    env->sem_obtain(env, &d->si.si_Lock);
    p = d->dispose_pending;
    /* an owner from outside has asked them already (end_procs) */
    if (p && !(p & PN_OWNED) && d->procs > 1) {
        if (d->layout_proc && env->signal_stop) env->signal_stop(env, d->layout_proc);
        stop_print(env, d);
    }
    last = d->procs <= 1;
    if (d->procs) d->procs--;
    if (last && p && !(p & PN_OWNED)) {
        /* a dispose from elsewhere from now on joins this one */
        what = p;
        d->dispose_pending |= PN_TAKEN;
        d->dispose_owner = this_task(env);
    }
    cl = d->dispose_cl;
    env->sem_release(env, &d->si.si_Lock);
    /* not the last: the object may go as soon as proc_lock does */
    env->sem_release(env, &d->proc_lock);
    if (what & PN_DISPOSE) {
        end_procs(env, d);
        close_source(env, d);
        free_strings(env, d);
        env->do_super(env, cl, obj, &m);
    }
    if (!dispose) return;
    if (what & PN_DTDISPOSE) *dispose = what & PN_DISPOSE ? ODT_DISPOSE_RECORD : ODT_DISPOSE_ALL;
}

/* The layout process's work, proc_lock held (odt_proc_hold) until after
 * its last touch of the object (the notification) */
odt_u32 odt_layout_run(const struct odt_cenv *env, void *obj, struct odt_gpLayout *gpl, int *dispose)
{
    struct proc_note pn;
    odt_u32 r;
    if (dispose) *dispose = 0;
    r = layout_body(env, obj, gpl, &pn);
    proc_end(env, obj, dispose);
    return r;
}

/* DisposeDTObject's first step: the object has one disposer, and its
 * library record one freer.
 * - From a process inside its completion notification, or while such a
 *   dispose waits, the dispose waits for the object's last process to end,
 *   and that process's caller (odt_layout_run's or odt_print_run's
 *   *dispose) does it then: 1.
 * - While one is under way (the last process's, or another task's): joined,
 *   1; but when the last process's is OM_DISPOSE alone, the record is the
 *   caller's to free: ODT_DISPOSE_RECORD.
 * - Otherwise the caller disposes it all now, and is its disposer: 0. */
int odt_dispose_later(const struct odt_cenv *env, void *obj)
{
    struct odt_dtinst *d;
    struct proc_note *pn;
    odt_u16 p;
    int r = 1;
    if (!obj) return 0;
    d = ODT_DTINST(env, obj);
    env->sem_obtain(env, &d->si.si_Lock);
    pn = own_note_locked(env, d);
    p = d->dispose_pending;
    if (p & PN_BUSY) {
        /* after the last process's OM_DISPOSE alone, the record is the
         * caller's: that dispose runs in datatypes.library, past the class.
         * An outside DisposeObject (PN_OWNED alone) may still be in the
         * class's code, so its record stays: a program that disposes a
         * datatype object with DisposeObject and DisposeDTObject both
         * keeps the class library open. */
        if ((p & PN_TAKEN) && !(p & (PN_DTDISPOSE | PN_RECORD))) r = ODT_DISPOSE_RECORD;
        d->dispose_pending |= PN_DTDISPOSE | PN_RECORD;
    } else if (pn || p) {
        d->dispose_pending |= PN_DTDISPOSE;
    } else {
        d->dispose_pending = PN_OWNED | PN_OWNED_DT;
        d->dispose_owner = this_task(env);
        r = 0;
    }
    env->sem_release(env, &d->si.si_Lock);
    return r;
}

/* DoAsyncLayout: one layout at a time; a new one stops the old first */
odt_u32 odt_do_async_layout(const struct odt_cenv *env, void *obj, struct odt_gpLayout *gpl)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_TagItem t[1];
    if (!obj || !gpl) return 0;
    stop_layout(env, d);
    d->flags |= ODT_DTF_BUSY;
    d->flags &= (odt_u16)~ODT_DTF_SYNC;
    t[0].ti_Tag = ODT_DTA_Busy; t[0].ti_Data = 1;
    notify(env, obj, gpl->gpl_GInfo, 1, t, 1);
    /* si_Lock held until the process is recorded: the process's first
     * step is to take it, so it can't finish before DTA_LayoutProc is set */
    env->sem_obtain(env, &d->si.si_Lock);
    d->si.si_Flags |= ODT_DTSIF_LAYOUTPROC;
    if (env->start_process && (d->layout_proc = env->start_process(env, 0, obj, NULLP, NULLP, gpl)) != NULLP) {
        d->procs++;                     /* before it can end: it needs si_Lock first */
        env->sem_release(env, &d->si.si_Lock);
        return 1;
    }
    /* no process: lay out here (ABI test: whether OS 3.2 fails instead) */
    layout_body(env, obj, gpl, NULLP);
    env->sem_release(env, &d->si.si_Lock);
    return 1;
}

/* A layout or print process's first step, before start_process answers:
 * it holds proc_lock (shared: a print and a layout may both run) until its
 * work's end, so OM_DISPOSE waits for it */
void odt_proc_hold(const struct odt_cenv *env, void *obj)
{
    env->sem_obtain_shared(env, &ODT_DTINST(env, obj)->proc_lock);
}

/* The print process's work, proc_lock held (odt_proc_hold). Printing
 * shows (DTA_PrinterProc, PrintDTObjectA refusing another) until the last
 * touch of the object but the lock's release. */
odt_u32 odt_print_run(const struct odt_cenv *env, void *obj, struct odt_dtPrint *msg, int *dispose)
{
    struct odt_dtinst *d = ODT_DTINST(env, obj);
    struct odt_TagItem t[1];
    struct proc_note pn;
    odt_u32 r;
    env->sem_obtain(env, &d->si.si_Lock);     /* PrintDTObjectA has recorded us */
    env->sem_release(env, &d->si.si_Lock);
    r = env->do_method(env, obj, msg);
    d->printer_status = r;
    t[0].ti_Tag = ODT_DTA_PrinterStatus; t[0].ti_Data = r;
    if (dispose) *dispose = 0;
    proc_notify(env, obj, msg->dtp_GInfo, t, 1, &pn);
    d->printer_proc = NULLP;
    d->si.si_Flags &= ~ODT_DTSIF_PRINTING;
    proc_end(env, obj, dispose);
    return r;
}

/* PrintDTObjectA: one printing at a time, on its own process */
odt_u32 odt_print_object(const struct odt_cenv *env, void *obj, void *win, void *req, struct odt_dtPrint *msg)
{
    struct odt_dtinst *d;
    if (!obj || !msg) return 0;
    d = ODT_DTINST(env, obj);
    if (d->printer_proc || (d->si.si_Flags & ODT_DTSIF_PRINTING)) return 0;
    env->sem_obtain(env, &d->si.si_Lock);
    d->si.si_Flags |= ODT_DTSIF_PRINTING;
    if (env->start_process && (d->printer_proc = env->start_process(env, 1, obj, win, req, msg)) != NULLP) {
        d->procs++;                     /* before it can end: it needs si_Lock first */
        env->sem_release(env, &d->si.si_Lock);
        return 1;
    }
    d->si.si_Flags &= ~ODT_DTSIF_PRINTING;
    env->sem_release(env, &d->si.si_Lock);
    return 0;
}

/* StartDragSelect: only for a class that selects, and not while a layout
 * or a drag runs */
odt_u32 odt_start_drag_select(const struct odt_cenv *env, void *obj)
{
    struct odt_dtinst *d;
    struct odt_opGet g;
    odt_u32 methods = 0;
    if (!obj) return 0;
    d = ODT_DTINST(env, obj);
    if (d->si.si_Flags & (ODT_DTSIF_LAYOUT | ODT_DTSIF_DRAGGING | ODT_DTSIF_DRAGSELECT)) return 0;
    g.MethodID = ODT_OM_GET;
    g.opg_AttrID = ODT_DTA_Methods;
    g.opg_Storage = &methods;
    env->do_method(env, obj, &g);
    if (!odt_FindMethod((odt_u32 *)methods, ODT_DTM_SELECT)) return 0;
    env->sem_obtain(env, &d->si.si_Lock);
    d->si.si_Flags |= ODT_DTSIF_DRAGSELECT;
    env->sem_release(env, &d->si.si_Lock);
    return 1;
}

/* GetDTAttrsA: each tag's ti_Data is where to store; the answer counts
 * those the object knew */
odt_u32 odt_get_dt_attrs(const struct odt_cenv *env, void *obj, struct odt_TagItem *tags)
{
    const struct odt_TagItem *tp = tags, *t;
    odt_u32 n = 0;
    if (!obj) return 0;
    while ((t = odt_next_tag(&tp)) != NULLP) {
        struct odt_opGet g;
        g.MethodID = ODT_OM_GET;
        g.opg_AttrID = t->ti_Tag;
        g.opg_Storage = (odt_u32 *)t->ti_Data;
        if (g.opg_Storage && env->do_method(env, obj, &g)) n++;
    }
    return n;
}

/* RemoveDTObject's object half: stop what runs, then DTM_REMOVEDTOBJECT */
odt_u32 odt_remove_object(const struct odt_cenv *env, void *obj)
{
    if (!obj) return 0;
    return do_msg2(env, obj, ODT_DTM_REMOVEDTOBJECT, NULLP);
}

/* DTST_RAM's class when only DTA_GroupID names it (ABI test: OS 3.2's
 * choice for each group) */
const char *odt_group_base(odt_u32 gid)
{
    switch (gid) {
    case ODT_GID_PICTURE:    return "picture";
    case ODT_GID_SOUND:
    case ODT_GID_INSTRUMENT: return "sound";
    case ODT_GID_TEXT:       return "text";
    case ODT_GID_ANIMATION:
    case ODT_GID_MOVIE:      return "animation";
    case ODT_GID_DOCUMENT:   return "amigaguide";
    default:                 return NULLP;
    }
}

/* LaunchToolA's command line: the program, a space, the project in
 * quotes (a quote in it escaped as AmigaDOS does, *"); 0 when it doesn't
 * fit or the project has a control character, else its length. A line
 * feed would end the Shell's command there and run the rest of the name
 * as a second command, so a name with one (or any other C0 control) is
 * refused, not quoted. */
odt_u32 odt_tool_command(char *buf, odt_u32 size, const char *program, const char *project)
{
    odt_u32 n = 0;
    const char *s;
    if (!buf || !size || !program) return 0;
    for (s = program; *s; s++) { if (n + 1 >= size) return 0; buf[n++] = *s; }
    if (project && *project) {
        if (n + 3 >= size) return 0;
        buf[n++] = ' ';
        buf[n++] = '"';
        for (s = project; *s; s++) {
            if ((unsigned char)*s < 0x20) return 0;
            if (*s == '"' || *s == '*') { if (n + 1 >= size) return 0; buf[n++] = '*'; }
            if (n + 2 >= size) return 0;
            buf[n++] = *s;
        }
        buf[n++] = '"';
    }
    buf[n] = 0;
    return n;
}
