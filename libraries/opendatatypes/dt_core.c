/* GFOpenDatatypes core: descriptors, recognition, method tables, tools and
 * strings. See dt_core.h.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Every choice the autodocs leave open is marked "OS 3.2:" when measured by
 * tests/DatatypesABITest's probe, and "guess:" until it has been.
 */
#include "dt_core.h"

#define NULLP ((void *)0)

/* ---- small helpers ------------------------------------------------------------- */

static odt_u32 be32(const odt_u8 *p)
{
    return ((odt_u32)p[0] << 24) | ((odt_u32)p[1] << 16) | ((odt_u32)p[2] << 8) | p[3];
}

static odt_u16 be16(const odt_u8 *p)
{
    return (odt_u16)(((unsigned)p[0] << 8) | p[1]);
}

static odt_u32 str_len(const char *s)
{
    odt_u32 n = 0;
    while (s[n]) n++;
    return n;
}

static void mem_copy(void *d, const void *s, odt_u32 n)
{
    odt_u8 *dd = d;
    const odt_u8 *ss = s;
    while (n--) *dd++ = *ss++;
}

/* Latin-1 lower case, as utility.library's ToLower does with no locale */
static unsigned lower(unsigned c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) return c + 0x20;
    return c;
}

int odt_stricmp(const char *a, const char *b)
{
    const odt_u8 *x = (const odt_u8 *)a, *y = (const odt_u8 *)b;
    for (;;) {
        unsigned p = lower(*x++), q = lower(*y++);
        if (p != q) return (int)p - (int)q;
        if (!p) return 0;
    }
}

static odt_u32 align4(odt_u32 n) { return (n + 3) & ~3UL; }

void odt_list_init(struct odt_List *l)
{
    l->lh_Head = (struct odt_Node *)&l->lh_Tail;
    l->lh_Tail = NULLP;
    l->lh_TailPred = (struct odt_Node *)&l->lh_Head;
    l->lh_Type = 0;
    l->l_pad = 0;
}

static void node_insert_before(struct odt_Node *n, struct odt_Node *at)
{
    n->ln_Succ = at;
    n->ln_Pred = at->ln_Pred;
    at->ln_Pred->ln_Succ = n;
    at->ln_Pred = n;
}

static void node_add_tail(struct odt_List *l, struct odt_Node *n)
{
    node_insert_before(n, (struct odt_Node *)&l->lh_Tail);
}

static void node_remove(struct odt_Node *n)
{
    n->ln_Pred->ln_Succ = n->ln_Succ;
    n->ln_Succ->ln_Pred = n->ln_Pred;
    n->ln_Succ = n->ln_Pred = NULLP;
}

#define FOR_NODES(l, n) for ((n) = (l)->lh_Head; (n)->ln_Succ; (n) = (n)->ln_Succ)

/* the entry a list node belongs to: dtn_Node1 is at the start of it */
static struct odt_entry *entry1(struct odt_Node *n) { return (struct odt_entry *)n; }
static struct odt_entry *entry2(struct odt_Node *n)
{
    return (struct odt_entry *)((odt_u8 *)n - (odt_u32)&((struct odt_DataType *)0)->dtn_Node2);
}

void odt_db_init(struct odt_db *db)
{
    int i;
    for (i = 0; i < 4; i++) odt_list_init(&db->kind[i]);
    odt_list_init(&db->all);
    db->serial = 0;
}

/* ---- descriptor files ---------------------------------------------------------- */

/* A string at offset off in a chunk of size n: its length, or -1 when it
 * runs out of the chunk. Offset 0 is no string here; the header then points
 * at itself, as OS 3.2's does (measured: its name is the header's bytes). */
static odt_s32 chunk_str(const odt_u8 *c, odt_u32 n, odt_u32 off)
{
    odt_u32 i;
    if (!off) return 0;
    if (off >= n) return -1;
    for (i = off; i < n; i++)
        if (!c[i]) return (odt_s32)(i - off);
    return -1;
}

/* A chunk, as found */
struct chunk { odt_u32 id, size; const odt_u8 *data; };

#define MAX_TOOLS 16

/* Two passes over the same chunks: the first sizes the block, the second
 * fills it. */
int odt_parse(const struct odt_env *env, const odt_u8 *form, odt_u32 len, struct odt_entry **out)
{
    struct chunk head = { 0, 0, NULLP }, code = { 0, 0, NULLP }, tags = { 0, 0, NULLP };
    struct chunk tools[MAX_TOOLS];
    odt_u32 ntools = 0, at, end, size, i;
    odt_s32 ln, lb, lp, lt[MAX_TOOLS];
    odt_u32 mask_off, mask_len;
    struct odt_entry *e;
    odt_u8 *p;

    *out = NULLP;
    if (len < 12 || be32(form) != ODT_ID_FORM || be32(form + 8) != ODT_ID_DTYP) return ODT_ERR_FORM;
    end = be32(form + 4) + 8;
    if (end > len || end < 12) return ODT_ERR_FORM;

    for (at = 12; at + 8 <= end; ) {
        struct chunk c;
        c.id = be32(form + at);
        c.size = be32(form + at + 4);
        c.data = form + at + 8;
        if (c.size > end - at - 8) return ODT_ERR_BAD;
        if (c.id == ODT_ID_DTHD) head = c;
        else if (c.id == ODT_ID_CODE) code = c;
        else if (c.id == ODT_ID_TAGS) tags = c;
        else if (c.id == ODT_ID_TOOL && ntools < MAX_TOOLS) tools[ntools++] = c;
        at += 8 + ((c.size + 1) & ~1UL);
    }
    if (!head.data) return ODT_ERR_NOHEAD;
    if (head.size < 32) return ODT_ERR_BAD;

    /* the header's strings and mask, offsets from the chunk's start */
    ln = chunk_str(head.data, head.size, be32(head.data + 0));
    lb = chunk_str(head.data, head.size, be32(head.data + 4));
    lp = chunk_str(head.data, head.size, be32(head.data + 8));
    mask_off = be32(head.data + 12);
    mask_len = be16(head.data + 24) & 0x7fff;
    if (ln < 0 || lb < 0 || lp < 0) return ODT_ERR_BAD;
    if (mask_len && (!mask_off || mask_off > head.size || mask_len * 2 > head.size - mask_off))
        return ODT_ERR_BAD;
    for (i = 0; i < ntools; i++) {
        if (tools[i].size < 8) return ODT_ERR_BAD;
        lt[i] = chunk_str(tools[i].data, tools[i].size, be32(tools[i].data + 4));
        if (lt[i] < 0) return ODT_ERR_BAD;
    }
    tags.size &= ~7UL;

    size = align4(sizeof(struct odt_entry));
    size += align4(mask_len * 2);
    size += align4(tags.size ? tags.size + 8 : 0);
    for (i = 0; i < ntools; i++) size += align4(sizeof(struct odt_ToolNode) + lt[i] + 1);
    size += ln + 1 + lb + 1 + lp + 1;
    size += code.size;

    e = env->alloc(env->ctx, size);
    if (!e) return ODT_ERR_MEMORY;
    p = (odt_u8 *)e + align4(sizeof(struct odt_entry));

    e->dt.dtn_Header = &e->dth;
    e->dt.dtn_Length = size;
    odt_list_init(&e->dt.dtn_ToolList);
    odt_list_init(&e->tools);
    e->dth.dth_GroupID = be32(head.data + 16);
    e->dth.dth_ID = be32(head.data + 20);
    e->dth.dth_MaskLen = (odt_s16)mask_len;
    e->dth.dth_Pad = (odt_s16)be16(head.data + 26);
    e->dth.dth_Flags = be16(head.data + 28);
    e->dth.dth_Priority = be16(head.data + 30);

    if (mask_len) {
        e->dth.dth_Mask = (odt_s16 *)p;
        for (i = 0; i < mask_len; i++)
            e->dth.dth_Mask[i] = (odt_s16)be16(head.data + mask_off + 2 * i);
        p += align4(mask_len * 2);
    }
    if (tags.size) {
        struct odt_TagItem *t = (struct odt_TagItem *)p;
        for (i = 0; i < tags.size / 8; i++) {
            t[i].ti_Tag = be32(tags.data + 8 * i);
            t[i].ti_Data = be32(tags.data + 8 * i + 4);
        }
        t[i].ti_Tag = ODT_TAG_DONE;
        t[i].ti_Data = 0;
        e->dt.dtn_AttrList = t;
        p += align4(tags.size + 8);
    }
    for (i = 0; i < ntools; i++) {
        struct odt_ToolNode *tn = (struct odt_ToolNode *)p;
        odt_u32 n = align4(sizeof(struct odt_ToolNode) + lt[i] + 1);
        tn->tn_Tool.tn_Which = be16(tools[i].data);
        tn->tn_Tool.tn_Flags = be16(tools[i].data + 2);
        tn->tn_Tool.tn_Program = (char *)(tn + 1);
        if (lt[i]) mem_copy(tn + 1, tools[i].data + be32(tools[i].data + 4), lt[i]);
        tn->tn_Length = n;
        node_add_tail(&e->tools, &tn->tn_Node);
        p += n;
    }
    e->dth.dth_Name = e->dth.dth_BaseName = e->dth.dth_Pattern = (char *)&e->dth;
    if (be32(head.data + 0)) {
        e->dth.dth_Name = (char *)p;
        mem_copy(p, head.data + be32(head.data + 0), ln);
    }
    p += ln + 1;
    if (be32(head.data + 4)) {
        e->dth.dth_BaseName = (char *)p;
        mem_copy(p, head.data + be32(head.data + 4), lb);
    }
    p += lb + 1;
    if (be32(head.data + 8)) {
        e->dth.dth_Pattern = (char *)p;
        mem_copy(p, head.data + be32(head.data + 8), lp);
    }
    p += lp + 1;
    if (code.size) {
        mem_copy(p, code.data, code.size);
        e->code = p;
        e->code_len = code.size;
    }
    e->dt.dtn_Node1.ln_Name = e->dth.dth_Name;
    e->dt.dtn_Node2.ln_Name = e->dth.dth_Name;
    *out = e;
    return ODT_OK;
}

struct odt_entry *odt_builtin(const struct odt_env *env, const char *name, const char *base,
                              odt_u32 group, odt_u32 id, odt_u16 flags, odt_u16 pri)
{
    odt_u32 ln = str_len(name), lb = str_len(base);
    odt_u32 size = align4(sizeof(struct odt_entry)) + ln + 1 + lb + 1;
    struct odt_entry *e = env->alloc(env->ctx, size);
    char *p;
    if (!e) return NULLP;
    p = (char *)e + align4(sizeof(struct odt_entry));
    e->dt.dtn_Header = &e->dth;
    e->dt.dtn_Length = size;
    odt_list_init(&e->dt.dtn_ToolList);
    odt_list_init(&e->tools);
    e->dth.dth_Name = p;
    mem_copy(p, name, ln + 1);
    p += ln + 1;
    e->dth.dth_BaseName = p;
    mem_copy(p, base, lb + 1);
    /* no pattern (measured) */
    e->dth.dth_GroupID = group;
    e->dth.dth_ID = id;
    e->dth.dth_Flags = flags;
    e->dth.dth_Priority = pri;
    e->dt.dtn_Node1.ln_Name = e->dth.dth_Name;
    e->dt.dtn_Node2.ln_Name = e->dth.dth_Name;
    e->builtin = 1;
    return e;
}

/* Does a come before b in its kind's list? The library's own types last;
 * then the longer mask; then the LOWER priority, as an unsigned word; then
 * the one added last. Measured on OS 3.2: "TIE!" beats an older "TIE"; a
 * newer "TA" beats an older "TA"; priority 0 beats 5 whichever is newer,
 * and 1 beats 0xffff. guess: the mask before the priority. */
static int before(const struct odt_entry *a, const struct odt_entry *b)
{
    if (a->builtin != b->builtin) return !a->builtin;
    if (a->dth.dth_MaskLen != b->dth.dth_MaskLen) return a->dth.dth_MaskLen > b->dth.dth_MaskLen;
    if (a->dth.dth_Priority != b->dth.dth_Priority) return a->dth.dth_Priority < b->dth.dth_Priority;
    return a->serial > b->serial;
}

struct odt_entry *odt_db_add(struct odt_db *db, struct odt_entry *e)
{
    struct odt_List *l = &db->kind[e->dth.dth_Flags & 3];
    struct odt_Node *n;
    struct odt_entry *old = NULLP;

    FOR_NODES(&db->all, n) {
        struct odt_entry *o = entry2(n);
        if (!odt_stricmp(o->dth.dth_Name, e->dth.dth_Name)) { old = o; break; }
    }
    if (old) odt_db_remove(old);
    e->serial = db->serial++;
    FOR_NODES(l, n)
        if (before(e, entry1(n))) break;
    node_insert_before(&e->dt.dtn_Node1, n);
    node_add_tail(&db->all, &e->dt.dtn_Node2);
    return old;
}

void odt_db_remove(struct odt_entry *e)
{
    if (e->dt.dtn_Node1.ln_Succ) node_remove(&e->dt.dtn_Node1);
    if (e->dt.dtn_Node2.ln_Succ) node_remove(&e->dt.dtn_Node2);
}

void odt_free_entry(const struct odt_env *env, struct odt_entry *e)
{
    if (e->pat_tok) env->free(env->ctx, e->pat_tok);
    env->free(env->ctx, e);
}

struct odt_entry *odt_db_find_name(struct odt_db *db, const char *name)
{
    struct odt_Node *n;
    if (!name) return NULLP;
    FOR_NODES(&db->all, n) {
        struct odt_entry *e = entry2(n);
        if (e->dth.dth_BaseName && !odt_stricmp(e->dth.dth_BaseName, name)) return e;
    }
    FOR_NODES(&db->all, n) {
        struct odt_entry *e = entry2(n);
        if (!odt_stricmp(e->dth.dth_Name, name)) return e;
    }
    return NULLP;
}

/* ---- recognition ---------------------------------------------------------------- */

/* Text is every byte but the C1 controls (0x80-0x9f) and the C0 controls
 * other than BEL to CR, DC1, DC3 and ESC; one other byte makes it binary
 * (measured on OS 3.2, every byte value). */
static int is_text(unsigned c)
{
    if (c >= 0x20) return c < 0x80 || c >= 0xa0;
    return (c >= 7 && c <= 13) || c == 0x11 || c == 0x13 || c == 0x1b;
}

odt_u16 odt_classify(const odt_u8 *buf, odt_u32 len, odt_u32 *form)
{
    odt_u32 i;
    *form = 0;
    if (len >= 6) {
        /* measured: FORM is IFF from 6 bytes on, not at 4 or 5; CAT and
         * LIST are too */
        odt_u32 id = be32(buf);
        if (id == ODT_ID_FORM || id == ODT_ID_CAT || id == ODT_ID_LIST) {
            if (len >= 12) *form = be32(buf + 8);
            return ODT_DTF_IFF;
        }
    }
    if (!len) return ODT_DTF_BINARY;
    for (i = 0; i < len; i++)
        if (!is_text(buf[i])) return ODT_DTF_BINARY;
    return ODT_DTF_ASCII;
}

int odt_mask_match(const struct odt_DataTypeHeader *h, const odt_u8 *buf, odt_u32 len)
{
    odt_u32 i, n = (odt_u32)(odt_u16)h->dth_MaskLen;
    if (!h->dth_Mask) return 1;
    if (n > len) return 0;                  /* guess: the data must be as long as the mask */
    for (i = 0; i < n; i++) {
        odt_s16 m = h->dth_Mask[i];
        if (m < 0) continue;                /* -1: any byte (guess: any negative word) */
        if (h->dth_Flags & ODT_DTF_CASE) {
            if ((odt_u8)m != buf[i]) return 0;
        } else if (lower((odt_u8)m) != lower(buf[i])) return 0;
    }
    return 1;
}

/* Does the name fit the entry's pattern? Data without a name (memory) has
 * the name "". Measured on OS 3.2: "#?" fits "", "#?.odtpm" doesn't; and a
 * pattern without wildcards fits every name but its own ("odtexact.bin"
 * took every binary file and memory, but not the file odtexact.bin). */
static int name_fits(const struct odt_env *env, struct odt_entry *e, const char *name)
{
    const char *pattern = e->dth.dth_Pattern;
    if (!pattern) return 1;
    if (!name) name = "";
    if (e->pat_state == ODT_PAT_ANY) return 1;
    if (e->pat_state == ODT_PAT_NEW) {
        /* the first time this entry's pattern is asked: parsed once, the
         * token kept (no memory for it: no fit, and asked again next time).
         * "#?" fits every name (OS 3.2: "" too), so it needs no token. */
        odt_u32 n = 2 * str_len(pattern) + 2;
        char *tok;
        int r;
        if (pattern[0] == '#' && pattern[1] == '?' && !pattern[2]) {
            e->pat_state = ODT_PAT_ANY;
            return 1;
        }
        if (!(tok = env->alloc(env->ctx, n))) return 0;
        r = env->parse_name(env->ctx, pattern, tok, n);
        if (r > 0) {
            e->pat_tok = tok;
            e->pat_state = ODT_PAT_WILD;
        } else {
            env->free(env->ctx, tok);
            e->pat_state = r == 0 ? ODT_PAT_LITERAL : ODT_PAT_BAD;
        }
    }
    if (e->pat_state == ODT_PAT_WILD) return env->match_name(env->ctx, e->pat_tok, name);
    if (e->pat_state == ODT_PAT_LITERAL) return odt_stricmp(pattern, name) != 0;
    return 0;
}

static int fits(const struct odt_env *env, struct odt_entry *e,
                const odt_u8 *buf, odt_u32 len, const char *name, struct odt_DTHookContext *dthc)
{
    const struct odt_DataTypeHeader *h = &e->dth;
    /* measured: a type with no mask needs a pattern other than "#?" (an IFF
     * one with "#?" never matched, one with "#?.zi" did); an IFF type's ID
     * isn't compared to the FORM's */
    if (!h->dth_MaskLen && h->dth_Pattern && h->dth_Pattern[0] == '#' && h->dth_Pattern[1] == '?' &&
        !h->dth_Pattern[2])
        return 0;
    if (!odt_mask_match(h, buf, len)) return 0;
    if (!name_fits(env, e, name)) return 0;
    if (e->code && env->run_hook && !env->run_hook(env->ctx, e, dthc)) return 0;
    return 1;
}

struct odt_entry *odt_recognize(const struct odt_env *env, struct odt_db *db,
                                const odt_u8 *buf, odt_u32 len, const char *name,
                                struct odt_DTHookContext *dthc)
{
    odt_u32 form;
    odt_u16 kind;
    struct odt_Node *n;

    if (!len) return NULLP;                 /* measured: no data, no type */
    kind = odt_classify(buf, len, &form);
    /* measured: each kind tries its own list only (text never meets a
     * binary type, binary never an ASCII one, IFF never a binary one) */
    FOR_NODES(&db->kind[kind], n) {
        struct odt_entry *e = entry1(n);
        if (!e->builtin && fits(env, e, buf, len, name, dthc)) return e;
    }
    /* the library's own type for the kind */
    FOR_NODES(&db->kind[kind], n)
        if (entry1(n)->builtin) return entry1(n);
    return NULLP;
}

/* ---- the V45 method tables -------------------------------------------------- */

#define END_METHOD (~0UL)

odt_u32 *odt_FindMethod(odt_u32 *methods, odt_u32 id)
{
    if (!methods) return NULLP;
    for (; *methods != END_METHOD; methods++)
        if (*methods == id) return methods;
    return NULLP;
}

/* OS 3.2: the first entry whose command is the one given (without case) or
 * whose method is exactly the one given; ~0 is no method, NULL no command */
struct odt_DTMethod *odt_FindTriggerMethod(struct odt_DTMethod *methods, const char *command,
                                           odt_u32 method)
{
    if (!methods) return NULLP;
    for (; methods->dtm_Label; methods++) {
        if (command && methods->dtm_Command && !odt_stricmp(command, methods->dtm_Command))
            return methods;
        if (methods->dtm_Method == method) return methods;
    }
    return NULLP;
}

static odt_u32 count_methods(const odt_u32 *m)
{
    odt_u32 n = 0;
    if (m) while (m[n] != END_METHOD) n++;
    return n;
}

static int in_methods(const odt_u32 *m, odt_u32 id)
{
    if (m) for (; *m != END_METHOD; m++) if (*m == id) return 1;
    return 0;
}

/* OS 3.2: methods without those in exclude (every copy of each), then
 * those of include the result doesn't have yet */
odt_u32 *odt_CopyDTMethods(const struct odt_env *env, const odt_u32 *methods,
                           const odt_u32 *include, const odt_u32 *exclude)
{
    odt_u32 n = count_methods(methods) + count_methods(include) + 1, i, k = 0;
    odt_u32 *r = env->alloc(env->ctx, n * sizeof(odt_u32));
    if (!r) return NULLP;
    for (i = 0; methods && methods[i] != END_METHOD; i++)
        if (!in_methods(exclude, methods[i])) r[k++] = methods[i];
    for (i = 0; include && include[i] != END_METHOD; i++) {
        r[k] = END_METHOD;
        if (!in_methods(r, include[i])) r[k++] = include[i];
    }
    r[k] = END_METHOD;
    return r;
}

static odt_u32 count_triggers(const struct odt_DTMethod *m)
{
    odt_u32 n = 0;
    if (m) while (m[n].dtm_Label) n++;
    return n;
}

static int in_triggers(const struct odt_DTMethod *m, odt_u32 id)
{
    if (m) for (; m->dtm_Label; m++) if (m->dtm_Method == id) return 1;
    return 0;
}

/* OS 3.2: as CopyDTMethods, an entry being its method exactly; the entries
 * are copied, their strings shared */
struct odt_DTMethod *odt_CopyDTTriggerMethods(const struct odt_env *env,
                                              const struct odt_DTMethod *methods,
                                              const struct odt_DTMethod *include,
                                              const struct odt_DTMethod *exclude)
{
    odt_u32 n = count_triggers(methods) + count_triggers(include) + 1, i, k = 0;
    struct odt_DTMethod *r = env->alloc(env->ctx, n * sizeof(struct odt_DTMethod));
    if (!r) return NULLP;
    for (i = 0; methods && methods[i].dtm_Label; i++)
        if (!in_triggers(exclude, methods[i].dtm_Method)) r[k++] = methods[i];
    for (i = 0; include && include[i].dtm_Label; i++) {
        r[k].dtm_Label = NULLP;
        if (!in_triggers(r, include[i].dtm_Method)) r[k++] = include[i];
    }
    r[k].dtm_Label = NULLP;
    r[k].dtm_Command = NULLP;
    r[k].dtm_Method = 0;
    return r;
}

/* OS 3.2: the method's own data bits when it has any; else STM_COMMAND
 * takes a string, STM_LOCATE a number and everything else nothing. The
 * reserved bits are ignored. */
odt_u32 odt_GetDTTriggerMethodDataFlags(odt_u32 method)
{
    if (method & ODT_STMF_DATA_MASK) return method & ODT_STMF_DATA_MASK;
    switch (method & ODT_STMF_METHOD_MASK) {
    case ODT_STM_COMMAND: return ODT_STMD_STRPTR;
    case ODT_STM_LOCATE: return ODT_STMD_ULONG;
    default: return ODT_STMD_VOID;
    }
}

/* ---- tools and strings ------------------------------------------------------ */

const struct odt_TagItem *odt_next_tag(const struct odt_TagItem **tp)
{
    const struct odt_TagItem *t = *tp;
    if (!t) return NULLP;
    for (;;) {
        switch (t->ti_Tag) {
        case ODT_TAG_DONE: *tp = NULLP; return NULLP;
        case ODT_TAG_IGNORE: t++; break;
        case ODT_TAG_MORE:
            t = (const struct odt_TagItem *)t->ti_Data;
            if (!t) { *tp = NULLP; return NULLP; }
            break;
        case ODT_TAG_SKIP: t += t->ti_Data + 1; break;
        default: *tp = t + 1; return t;
        }
    }
}

/* guess: the first tool that fits every tag given; programs compared
 * without case */
struct odt_ToolNode *odt_FindToolNodeA(struct odt_List *tools, const struct odt_TagItem *tags)
{
    struct odt_Node *n;
    if (!tools) return NULLP;
    FOR_NODES(tools, n) {
        struct odt_ToolNode *tn = (struct odt_ToolNode *)n;
        const struct odt_TagItem *tp = tags, *t;
        int ok = 1;
        while (ok && (t = odt_next_tag(&tp)) != NULLP) {
            switch (t->ti_Tag) {
            case ODT_TOOLA_Program:
                ok = t->ti_Data && tn->tn_Tool.tn_Program &&
                     !odt_stricmp((const char *)t->ti_Data, tn->tn_Tool.tn_Program);
                break;
            case ODT_TOOLA_Which:
                ok = tn->tn_Tool.tn_Which == (odt_u16)t->ti_Data;
                break;
            case ODT_TOOLA_LaunchType:
                ok = (tn->tn_Tool.tn_Flags & ODT_TF_LAUNCH_MASK) == (t->ti_Data & ODT_TF_LAUNCH_MASK);
                break;
            }
        }
        if (ok) return tn;
    }
    return NULLP;
}

/* The built-in English strings (sys/datatypes.catalog replaces them in the
 * library when locale has one). OS 3.2 measured: the errors (three take
 * the object's name), the four kinds of data at DTMSG_TYPE_OFFSET, the
 * groups by their GIDs, "" for 0 and NULL for anything else. */
static const char *const errors[] = {
    "Unknown data type for %s",                           /* 2000 */
    "Couldn't save %s",
    "Couldn't open %s",
    "Couldn't send message",
    "Couldn't open clipboard",
    "Unknown data type",                                  /* 2005 */
    "Unknown compression type",
    "Not enough data",
    "Invalid data",
    "Data not available",                                 /* 2009 */
};

static const char *const kinds[] = { "Binary", "ASCII", "IFF", "Miscellaneous" };

static const struct { odt_u32 gid; const char *name; } groups[] = {
    { ODT_GID_SYSTEM, "System" },
    { ODT_GID_TEXT, "Text" },
    { ODT_GID_DOCUMENT, "Document" },
    { ODT_GID_SOUND, "Sound" },
    { ODT_GID_INSTRUMENT, "Instrument" },
    { ODT_GID_MUSIC, "Music" },
    { ODT_GID_PICTURE, "Picture" },
    { ODT_GID_ANIMATION, "Animation" },
    { ODT_GID_MOVIE, "Movie" },
};

#define NGROUPS (sizeof groups / sizeof groups[0])

const char *odt_GetDTString(odt_u32 id)
{
    odt_u32 i;
    if (id >= ODT_DTERROR_UNKNOWN_DATATYPE && id <= ODT_DTERROR_NOT_AVAILABLE)
        return errors[id - ODT_DTERROR_UNKNOWN_DATATYPE];
    if (id >= ODT_DTMSG_TYPE_OFFSET && id < ODT_DTMSG_TYPE_OFFSET + 4)
        return kinds[id - ODT_DTMSG_TYPE_OFFSET];
    for (i = 0; i < NGROUPS; i++)
        if (groups[i].gid == id) return groups[i].name;
    return id ? NULLP : "";
}
