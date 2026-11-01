/* GFOpenDatatypes core under qemu-m68k: descriptor files made here are read,
 * ordered and asked to recognise data; the V45 method tables, tools and
 * strings. What OS 3.2 answers is DatatypesABITest's to check; this holds
 * the core to what it means to do.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "dt_core.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static long live;
static void *t_alloc(void *ctx, odt_u32 n) { (void)ctx; live++; return calloc(1, n); }
static void t_free(void *ctx, void *p) { (void)ctx; if (p) live--; free(p); }
/* "#?" and "?" as the shell's "*" and "?", enough for the patterns here;
 * -1 for a pattern without wildcards, as the library's MatchPatternNoCase glue */
static int t_match(void *ctx, const char *pat, const char *name)
{
    char g[64]; int i = 0;
    (void)ctx;
    if (!strchr(pat, '#') && !strchr(pat, '?')) return -1;
    for (; *pat && i < 62; pat++) {
        if (pat[0] == '#' && pat[1] == '?') { g[i++] = '*'; pat++; }
        else g[i++] = *pat;
    }
    g[i] = 0;
    return fnmatch(g, name, FNM_CASEFOLD) == 0;
}
static int hook_calls, hook_answer;
static odt_u32 hook_len;
static int t_hook(void *ctx, struct odt_entry *e, struct odt_DTHookContext *d)
{
    (void)ctx; (void)e;
    hook_calls++;
    hook_len = d ? d->dthc_BufferLength : 0;
    return hook_answer;
}
static const struct odt_env env = { NULL, t_alloc, t_free, t_match, t_hook };

/* ---- descriptor files ---------------------------------------------------------- */

static unsigned char form[1024];
static unsigned flen;

static void put32(unsigned char *p, unsigned long v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }

static void begin(void)
{
    memcpy(form, "FORM\0\0\0\0DTYP", 12);
    flen = 12;
}
static void end(void) { put32(form + 4, flen - 8); }
static unsigned chunk(const char *id, unsigned n)
{
    unsigned at = flen + 8;
    memcpy(form + flen, id, 4);
    put32(form + flen + 4, n);
    memset(form + at, 0, (n + 1) & ~1u);
    flen = at + ((n + 1) & ~1u);
    return at;
}

/* a DTHD: mask as a string where '?' is -1 */
static void head(const char *name, const char *base, const char *pat, const char *mask,
                 unsigned long gid, unsigned long id, unsigned flags, unsigned pri)
{
    unsigned ln = strlen(name) + 1, lb = strlen(base) + 1, lp = pat ? strlen(pat) + 1 : 0;
    unsigned lm = mask ? strlen(mask) : 0, n = 32 + ln + lb + lp + 2 * lm, at, o = 32, i;
    at = chunk("DTHD", n);
    put32(form + at + 0, o); memcpy(form + at + o, name, ln); o += ln;
    put32(form + at + 4, o); memcpy(form + at + o, base, lb); o += lb;
    if (pat) { put32(form + at + 8, o); memcpy(form + at + o, pat, lp); o += lp; }
    if (lm) {
        put32(form + at + 12, o);
        for (i = 0; i < lm; i++) put16(form + at + o + 2 * i, mask[i] == '?' ? 0xffff : (unsigned char)mask[i]);
    }
    put32(form + at + 16, gid); put32(form + at + 20, id);
    put16(form + at + 24, lm); put16(form + at + 28, flags); put16(form + at + 30, pri);
}

static void tool(unsigned which, unsigned flags, const char *prog)
{
    unsigned lp = strlen(prog) + 1, at = chunk("DTTL", 8 + lp);
    put16(form + at, which); put16(form + at + 2, flags); put32(form + at + 4, 8);
    memcpy(form + at + 8, prog, lp);
}

static struct odt_db db;

static struct odt_entry *add(void)
{
    struct odt_entry *e, *old;
    end();
    CHECK(odt_parse(&env, form, flen, &e) == ODT_OK);
    if (!e) return NULL;
    old = odt_db_add(&db, e);
    if (old) odt_free_entry(&env, old);
    return e;
}

static const char *rec(const char *data, unsigned len, const char *name)
{
    struct odt_DTHookContext d;
    struct odt_entry *e;
    memset(&d, 0, sizeof d);
    d.dthc_Buffer = (char *)data; d.dthc_BufferLength = len;
    e = odt_recognize(&env, &db, (const odt_u8 *)data, len, name, &d);
    return e ? e->dth.dth_Name : "(none)";
}

#define ID(a) ODT_ID(a[0], a[1], a[2], a[3])

static void test_descriptors(void)
{
    struct odt_entry *e;
    struct odt_Node *n;
    int i;

    odt_db_init(&db);
    odt_db_add(&db, odt_builtin(&env, "binary", "binary", ODT_GID_SYSTEM, ID("bina"), ODT_DTF_BINARY, 0));
    odt_db_add(&db, odt_builtin(&env, "ascii", "ascii", ODT_GID_TEXT, ID("asci"), ODT_DTF_ASCII, 0));
    odt_db_add(&db, odt_builtin(&env, "iff", "iff", ODT_GID_SYSTEM, ID("iff\0"), ODT_DTF_IFF, 0));

    begin(); head("ILBM", "ilbm", "#?", "FORM????ILBM", ODT_GID_PICTURE, ID("ILBM"), ODT_DTF_IFF, 0);
    tool(ODT_TW_BROWSE, ODT_TF_WORKBENCH, "SYS:Utilities/MultiView");
    tool(ODT_TW_EDIT, ODT_TF_SHELL, "C:Ed");
    e = add();
    CHECK(e && !strcmp(e->dth.dth_BaseName, "ilbm") && e->dth.dth_MaskLen == 12);
    CHECK(e && e->dth.dth_Mask[4] == -1 && e->dth.dth_Mask[0] == 'F');
    CHECK(e && e->dt.dtn_Header == &e->dth && e->dt.dtn_Length > sizeof *e);
    /* OS 3.2 leaves dtn_ToolList empty; the tools are the entry's own */
    CHECK(e && !e->dt.dtn_ToolList.lh_Head->ln_Succ);
    {
        struct odt_TagItem t1[] = { { ODT_TOOLA_Which, ODT_TW_EDIT }, { ODT_TAG_DONE, 0 } };
        struct odt_TagItem t2[] = { { ODT_TOOLA_Program, (odt_u32)"sys:utilities/multiview" },
                                    { ODT_TOOLA_LaunchType, ODT_TF_WORKBENCH }, { ODT_TAG_DONE, 0 } };
        struct odt_TagItem t3[] = { { ODT_TOOLA_Which, ODT_TW_PRINT }, { ODT_TAG_DONE, 0 } };
        struct odt_ToolNode *tn = e ? odt_FindToolNodeA(&e->tools, t1) : NULL;
        CHECK(tn && !strcmp(tn->tn_Tool.tn_Program, "C:Ed") && tn->tn_Tool.tn_Flags == ODT_TF_SHELL);
        tn = e ? odt_FindToolNodeA(&e->tools, t2) : NULL;
        CHECK(tn && tn->tn_Tool.tn_Which == ODT_TW_BROWSE);
        CHECK(e && !odt_FindToolNodeA(&e->tools, t3));
        CHECK(e && odt_FindToolNodeA(&e->tools, NULL) == (struct odt_ToolNode *)e->tools.lh_Head);
    }

    begin(); head("GIF", "gif", "#?", "GIF8", ODT_GID_PICTURE, ID("GIF "), ODT_DTF_BINARY | ODT_DTF_CASE, 0); add();
    begin(); head("GIF87", "gif", "#?", "GIF87a", ODT_GID_PICTURE, ID("GIF "), ODT_DTF_BINARY | ODT_DTF_CASE, 0); add();
    begin(); head("Low", "low", "#?.low", NULL, ODT_GID_SYSTEM, ID("low "), ODT_DTF_BINARY, (odt_u16)-5); add();
    begin(); head("High", "high", "#?.high", NULL, ODT_GID_SYSTEM, ID("high"), ODT_DTF_BINARY, 5); add();
    begin(); head("HTML", "html", "#?.html", "<html", ODT_GID_DOCUMENT, ID("HTML"), ODT_DTF_ASCII, 0); add();
    begin(); head("Guide", "amigaguide", "#?", "@database", ODT_GID_DOCUMENT, ID("AGDB"), ODT_DTF_ASCII, 0); add();
    begin(); head("Hooked", "hooked", "#?", "HK", ODT_GID_SYSTEM, ID("HOOK"), ODT_DTF_BINARY | ODT_DTF_CASE, 0);
    chunk("DTCD", 6); e = add();
    CHECK(e && e->code && e->code_len == 6);

    /* binary order: the longer mask first, then the lower priority as an
     * unsigned word, then the newest (OS 3.2); the builtin last */
    {
        static const char *want[] = { "GIF87", "GIF", "Hooked", "High", "Low", "binary" };
        i = 0;
        for (n = db.kind[ODT_DTF_BINARY].lh_Head; n->ln_Succ; n = n->ln_Succ, i++)
            CHECK(i < 6 && !strcmp(n->ln_Name, want[i]));
        CHECK(i == 6);
    }

    CHECK(!strcmp(rec("FORM\0\0\0\x20" "ILBMBMHD", 16, "pic"), "ILBM"));
    CHECK(!strcmp(rec("FORM\0\0\0\x20" "ACBMBMHD", 16, "pic"), "iff"));
    CHECK(!strcmp(rec("GIF87a\1\2", 8, "x.gif"), "GIF87"));
    CHECK(!strcmp(rec("GIF89a\1\2", 8, "x.gif"), "GIF"));
    CHECK(!strcmp(rec("gif89a\1\2", 8, "x.gif"), "binary"));       /* DTF_CASE */
    CHECK(!strcmp(rec("\1\2\3", 3, "a.high"), "High"));
    CHECK(!strcmp(rec("\1\2\3", 3, "a.LOW"), "Low"));
    CHECK(!strcmp(rec("\1\2\3", 3, NULL), "binary"));              /* no name: "", fits no "#?.x" */
    CHECK(!strcmp(rec("<HTML><body>", 12, "index.html"), "HTML"));    /* no DTF_CASE */
    CHECK(!strcmp(rec("<HTML><body>", 12, "index.txt"), "ascii"));
    CHECK(!strcmp(rec("@DATABASE x\n", 12, "x.guide"), "Guide"));
    CHECK(!strcmp(rec("plain words\n", 12, "x"), "ascii"));
    CHECK(!strcmp(rec("GIF", 3, "x"), "ascii"));                    /* shorter than the mask */
    CHECK(!strcmp(rec("GIF89a", 6, "x"), "ascii"));                 /* text never meets binary types */
    CHECK(!strcmp(rec("", 0, "x"), "(none)"));
    CHECK(!strcmp(rec("FORM\0\0\0\4ODT", 11, "x"), "iff"));          /* short IFF */
    CHECK(!strcmp(rec("LIST\0\0\0\4ILBM", 12, "x"), "iff"));
    /* text: BEL to CR, ESC, DEL and 0xa0 up; not NUL, 0x1a, 1-6 or C1 */
    CHECK(!strcmp(rec("a\7\13\14\33\177\351\n", 8, "x"), "ascii"));
    CHECK(!strcmp(rec("a\205b", 3, "x"), "binary"));
    CHECK(!strcmp(rec("a\32", 2, "x"), "binary"));
    CHECK(!strcmp(rec("a\1", 2, "x"), "binary"));
    CHECK(!strcmp(rec("a\21\23", 3, "x"), "ascii"));                  /* DC1, DC3 */
    CHECK(!strcmp(rec("FORM", 4, "x"), "ascii"));
    CHECK(!strcmp(rec("FORM\0", 5, "x"), "binary"));
    CHECK(!strcmp(rec("FORM\0\0", 6, "x"), "iff"));
    CHECK(!strcmp(rec("ab\0", 3, "x"), "binary"));
    hook_answer = 0; hook_calls = 0;
    CHECK(!strcmp(rec("HKx\0", 4, "x"), "binary") && hook_calls == 1 && hook_len == 4);
    hook_answer = 1;
    CHECK(!strcmp(rec("HKx\0", 4, "x"), "Hooked"));

    CHECK(odt_db_find_name(&db, "ILBM") && !strcmp(odt_db_find_name(&db, "ilbm")->dth.dth_Name, "ILBM"));
    CHECK(odt_db_find_name(&db, "AmigaGuide") && !strcmp(odt_db_find_name(&db, "amigaguide")->dth.dth_Name, "Guide"));
    CHECK(!odt_db_find_name(&db, "nothing"));

    /* the same name again replaces it */
    begin(); head("GIF", "gif2", "#?", "GIF8", ODT_GID_PICTURE, ID("GIF "), ODT_DTF_BINARY | ODT_DTF_CASE, 0);
    e = add();
    CHECK(odt_db_find_name(&db, "gif2") == e && !strcmp(rec("GIF89a\1\2", 8, "x"), "GIF"));

    /* a mask-less type needs a pattern other than "#?" */
    begin(); head("IffNoMask", "inm", "#?", NULL, ODT_GID_PICTURE, ID("ACBM"), ODT_DTF_IFF, 0); add();
    CHECK(!strcmp(rec("FORM\0\0\0\x20" "ACBMBMHD", 16, "pic"), "iff"));

    /* a pattern without wildcards fits every name but its own (OS 3.2) */
    begin(); head("Lit", "lit", "odd.bin", NULL, ODT_GID_SYSTEM, ID("lit "), ODT_DTF_BINARY, 0); add();
    CHECK(!strcmp(rec("\1\2", 2, "y"), "Lit"));
    CHECK(!strcmp(rec("\1\2", 2, NULL), "Lit"));
    CHECK(!strcmp(rec("\1\2", 2, "ODD.bin"), "binary"));

    /* name offset 0: the header points at itself, as OS 3.2's */
    begin(); head("X", "x", "#?", "AB", 0, 0, 0, 0); end();
    put32(form + 20, 0);
    CHECK(odt_parse(&env, form, flen, &e) == ODT_OK && e && e->dth.dth_Name == (char *)&e->dth &&
          !strcmp(e->dth.dth_BaseName, "x"));
    if (e) odt_free_entry(&env, e);

    /* broken files */
    begin(); end(); CHECK(odt_parse(&env, form, flen, &e) == ODT_ERR_NOHEAD && !e);
    memcpy(form + 8, "ILBM", 4); CHECK(odt_parse(&env, form, flen, &e) == ODT_ERR_FORM);
    begin(); head("X", "x", "#?", "AB", 0, 0, 0, 0); end();
    put32(form + 20 + 12, 999); CHECK(odt_parse(&env, form, flen, &e) == ODT_ERR_BAD);
    begin(); head("X", "x", "#?", NULL, 0, 0, 0, 0); end();
    CHECK(odt_parse(&env, form, flen - 4, &e) == ODT_ERR_FORM);

    while (db.all.lh_Head->ln_Succ) {
        e = (struct odt_entry *)((char *)db.all.lh_Head - offsetof(struct odt_DataType, dtn_Node2));
        odt_db_remove(e);
        odt_free_entry(&env, e);
    }
}

static void test_methods(void)
{
    odt_u32 m[] = { 10, 20, 30, ~0UL }, inc[] = { 40, ~0UL }, exc[] = { 20, ~0UL }, *r;
    struct odt_DTMethod t[] = { { "Play", "PLAY", ODT_STM_PLAY }, { "Stop", "STOP", ODT_STM_STOP },
                                { "Go", "GOTO", ODT_STM_LOCATE }, { NULL, NULL, 0 } };
    struct odt_DTMethod ti[] = { { "Help", "HELP", ODT_STM_HELP }, { "Play", "PLAY", ODT_STM_PLAY }, { NULL, NULL, 0 } };
    struct odt_DTMethod te[] = { { "x", NULL, ODT_STM_STOP }, { NULL, NULL, 0 } };
    struct odt_DTMethod *rt;

    CHECK(odt_FindMethod(m, 20) == &m[1] && !odt_FindMethod(m, 50) && !odt_FindMethod(NULL, 1));
    r = odt_CopyDTMethods(&env, m, inc, exc);
    CHECK(r && r[0] == 10 && r[1] == 30 && r[2] == 40 && r[3] == ~0UL);
    free(r); live--;
    r = odt_CopyDTMethods(&env, m, m, NULL);                 /* OS 3.2: nothing added twice */
    CHECK(r && !memcmp(r, m, sizeof m));
    free(r); live--;
    r = odt_CopyDTMethods(&env, m, NULL, NULL);
    CHECK(r && r[3] == ~0UL && !memcmp(r, m, sizeof m));
    free(r); live--;

    CHECK(odt_FindTriggerMethod(t, "stop", ~0UL) == &t[1]);
    CHECK(odt_FindTriggerMethod(t, NULL, ODT_STM_LOCATE) == &t[2]);
    CHECK(odt_FindTriggerMethod(t, "stop", ODT_STM_PLAY) == &t[0]);   /* OS 3.2: either one fits */
    CHECK(odt_FindTriggerMethod(t, "nope", ODT_STM_LOCATE) == &t[2]);
    CHECK(!odt_FindTriggerMethod(t, "nope", ~0UL));
    rt = odt_CopyDTTriggerMethods(&env, t, ti, te);
    CHECK(rt && rt[0].dtm_Method == ODT_STM_PLAY && rt[1].dtm_Method == ODT_STM_LOCATE &&
          rt[2].dtm_Method == ODT_STM_HELP && !rt[3].dtm_Label);
    free(rt); live--;

    CHECK(odt_GetDTTriggerMethodDataFlags(ODT_STM_PLAY) == ODT_STMD_VOID);
    CHECK(odt_GetDTTriggerMethodDataFlags(ODT_STM_COMMAND) == ODT_STMD_STRPTR);
    CHECK(odt_GetDTTriggerMethodDataFlags(ODT_STM_LOCATE) == ODT_STMD_ULONG);
    CHECK(odt_GetDTTriggerMethodDataFlags(ODT_STM_USER | ODT_STMD_TAGLIST) == ODT_STMD_TAGLIST);
}

static void test_strings(void)
{
    CHECK(!strcmp(odt_GetDTString(ODT_DTERROR_UNKNOWN_DATATYPE), "Unknown data type for %s"));
    CHECK(!strcmp(odt_GetDTString(ODT_GID_PICTURE), "Picture"));
    CHECK(!strcmp(odt_GetDTString(ODT_DTMSG_TYPE_OFFSET + 1), "ASCII"));
    CHECK(!odt_GetDTString(ODT_DTMSG_TYPE_OFFSET + 4) && !odt_GetDTString(2010) && !odt_GetDTString(12345));
    CHECK(!strcmp(odt_GetDTString(0), ""));
    CHECK(odt_GetDTTriggerMethodDataFlags(ODT_STM_SEARCH) == ODT_STMD_VOID);
    CHECK(odt_GetDTTriggerMethodDataFlags(0x01000002UL) == ODT_STMD_VOID);
    CHECK(odt_GetDTTriggerMethodDataFlags(0xffffffffUL) == ODT_STMF_DATA_MASK);
}

int main(void)
{
    test_descriptors();
    test_methods();
    test_strings();
    CHECK(live == 0);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
