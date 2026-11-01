/* GFOpenDatatypes classes: small helpers the text, ascii, animation and
 * anim cores share, over dtclass_env.h (datatypesclass's numbers,
 * layouts and struct odt_cenv). Plain C, no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room: utility.library's tag walking and exec's list calls as
 * their autodocs describe them, written here so a class core calls
 * nothing but its env; never Kickstart's, Hyperion's or AROS's code.
 */
#ifndef OPENDATATYPES_DTC_UTIL_H
#define OPENDATATYPES_DTC_UTIL_H

#include "../../../dtclass_env.h"

#define DTC_ERROR_NO_FREE_STORE        103   /* dos/dos.h */
#define DTC_ERROR_REQUIRED_ARG_MISSING 116
#define DTC_ERROR_OBJECT_WRONG_TYPE    212

/* NextTagItem, FindTagItem and GetTagData over a core's tag lists */
static inline const struct odt_TagItem *dtc_next_tag(const struct odt_TagItem **tp)
{
    const struct odt_TagItem *t = *tp;
    for (;;) {
        if (!t) return 0;
        switch (t->ti_Tag) {
        case ODT_TAG_DONE: *tp = 0; return 0;
        case ODT_TAG_IGNORE: t++; break;
        case ODT_TAG_MORE: t = (const struct odt_TagItem *)t->ti_Data; break;
        case ODT_TAG_SKIP: t += t->ti_Data + 1; break;
        default: *tp = t + 1; return t;
        }
    }
}

static inline const struct odt_TagItem *dtc_find_tag(const struct odt_TagItem *tags, odt_u32 tag)
{
    const struct odt_TagItem *t;
    while ((t = dtc_next_tag(&tags)) != 0)
        if (t->ti_Tag == tag) return t;
    return 0;
}

static inline odt_u32 dtc_tag_data(const struct odt_TagItem *tags, odt_u32 tag, odt_u32 def)
{
    const struct odt_TagItem *t = dtc_find_tag(tags, tag);
    return t ? t->ti_Data : def;
}

/* exec's NewList, AddTail and RemHead on a MinList */
static inline void dtc_list_init(struct odt_MinList *l)
{
    l->mlh_Head = (struct odt_MinNode *)&l->mlh_Tail;
    l->mlh_Tail = 0;
    l->mlh_TailPred = (struct odt_MinNode *)&l->mlh_Head;
}

static inline void dtc_add_tail(struct odt_MinList *l, struct odt_MinNode *n)
{
    struct odt_MinNode *tail = (struct odt_MinNode *)&l->mlh_Tail;
    n->mln_Succ = tail;
    n->mln_Pred = l->mlh_TailPred;
    l->mlh_TailPred->mln_Succ = n;
    l->mlh_TailPred = n;
}

static inline void dtc_insert_after(struct odt_MinNode *pred, struct odt_MinNode *n)
{
    n->mln_Succ = pred->mln_Succ;
    n->mln_Pred = pred;
    pred->mln_Succ->mln_Pred = n;
    pred->mln_Succ = n;
}

static inline struct odt_MinNode *dtc_rem_head(struct odt_MinList *l)
{
    struct odt_MinNode *n = l->mlh_Head;
    if (!n->mln_Succ) return 0;
    l->mlh_Head = n->mln_Succ;
    n->mln_Succ->mln_Pred = (struct odt_MinNode *)&l->mlh_Head;
    return n;
}

/* A byte copy, so struct copies call no memcpy (with
 * -fno-tree-loop-distribute-patterns) */
static inline void dtc_copy(void *dst, const void *src, odt_u32 n)
{
    odt_u8 *d = (odt_u8 *)dst;
    const odt_u8 *s = (const odt_u8 *)src;
    while (n--) *d++ = *s++;
}

/* GetDTAttrs(obj, tag, &v, TAG_DONE) through the env: OM_GET on the whole
 * object. 1 when it answered. */
static inline int dtc_get(const struct odt_cenv *env, void *obj, odt_u32 tag, odt_u32 *v)
{
    struct odt_opGet g;
    g.MethodID = ODT_OM_GET;
    g.opg_AttrID = tag;
    g.opg_Storage = v;
    return env->do_method(env, obj, &g) != 0;
}

/* The DTSpecialInfo datatypesclass hangs on the gadget (public in the NDK) */
static inline struct odt_DTSpecialInfo *dtc_si(void *obj)
{
    return (struct odt_DTSpecialInfo *)((struct odt_Gadget *)obj)->SpecialInfo;
}

#endif
