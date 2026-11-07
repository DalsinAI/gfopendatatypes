/* GFOpenDatatypes core under qemu-m68k: descriptor files made here are read,
 * ordered and asked to recognise data; the V45 method tables, tools and
 * strings. What OS 3.2 answers is DatatypesABITest's to check; this holds
 * the core to what it means to do. Each descriptor's name pattern is
 * parsed once and kept: a corpus of made-up descriptors and files checks
 * every answer against recognition from scratch (patterns parsed for each
 * file). Run as `t DESCDIR FILE...` it does the same with a pack's
 * DEVS:DataTypes and real files, one line a file.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <strings.h>
#include <dirent.h>
#include "dt_core.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static long live;
static int fail_alloc;
static void *t_alloc(void *ctx, odt_u32 n) { (void)ctx; if (fail_alloc) return NULL; live++; return calloc(1, n); }
static void t_free(void *ctx, void *p) { (void)ctx; if (p) live--; free(p); }
/* AmigaDOS patterns, enough of them for descriptors' (? #x (a|b) [a-z]
 * [~a] ' %), as POSIX extended regular expressions: parse writes the
 * regex as the token (1 with wildcards, 0 without, -1 for what this
 * doesn't take); match compiles and runs it without case. parses and
 * matches count the calls. */
static long parses, matches;
static int to_regex(const char *p, char *out, unsigned size, int *wild)
{
    unsigned o = 0, depth = 0;
    char stack[32];                         /* per group: a # before it */
    int rep = 0;                            /* the next item repeats */
#define PUT(c) do { if (o + 1 >= size) return -1; out[o++] = (c); } while (0)
    *wild = 0;
    PUT('^');
    for (; *p; p++) {
        int item_end = 1, r = rep;
        rep = 0;
        switch (*p) {
        case '?': *wild = 1; PUT('.'); break;
        case '#': *wild = 1; rep = 1; item_end = 0; break;
        case '%': *wild = 1; PUT('('); PUT(')'); break;
        case '(':
            *wild = 1;
            if (depth >= sizeof stack) return -1;
            stack[depth++] = (char)r;
            PUT('(');
            item_end = 0; r = 0;
            break;
        case '|': *wild = 1; if (!depth) return -1; PUT('|'); item_end = 0; break;
        case ')':
            *wild = 1;
            if (!depth) return -1;
            PUT(')');
            if (stack[--depth]) PUT('*');
            break;
        case '[':
            *wild = 1;
            PUT('[');
            if (p[1] == '~') { PUT('^'); p++; }
            for (p++; *p && *p != ']'; p++) PUT(*p);
            if (!*p) return -1;
            PUT(']');
            break;
        case '~': return -1;
        case '\'':
            if (!*++p) return -1;
            /* fall through */
        default:
            if (strchr(".^$+{}*\\[]()|?", *p)) PUT('\\');
            PUT(*p);
        }
        if (item_end && r) PUT('*');
        else if (!item_end && r && *p != '(') return -1;  /* # before | or # */
    }
    if (depth || rep) return -1;
    PUT('$');
    out[o] = 0;
    return 0;
#undef PUT
}
static int t_parse(void *ctx, const char *pat, char *tok, odt_u32 size)
{
    int wild;
    (void)ctx;
    parses++;
    if (to_regex(pat, tok, size, &wild) < 0) return -1;
    return wild;
}
static int t_match(void *ctx, const char *tok, const char *name)
{
    regex_t re;
    int r;
    (void)ctx;
    matches++;
    if (regcomp(&re, tok, REG_EXTENDED | REG_ICASE | REG_NOSUB)) return 0;
    r = regexec(&re, name, 0, NULL, 0) == 0;
    regfree(&re);
    return r;
}
static int hook_calls, hook_answer, hook_corpus;
static odt_u32 hook_len;
/* the corpus's hooks answer by the data and the type, so either way shows */
static int corpus_hook(const struct odt_entry *e, const struct odt_DTHookContext *d)
{
    return ((d->dthc_BufferLength + e->dth.dth_ID + (unsigned char)d->dthc_Buffer[d->dthc_BufferLength - 1]) & 3) != 0;
}
static int t_hook(void *ctx, struct odt_entry *e, struct odt_DTHookContext *d)
{
    (void)ctx;
    hook_calls++;
    hook_len = d ? d->dthc_BufferLength : 0;
    if (hook_corpus) return corpus_hook(e, d);
    return hook_answer;
}
static const struct odt_env env = { NULL, t_alloc, t_free, t_parse, t_match, t_hook };

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

    /* each pattern is parsed once, the first time it is asked, and "#?"
     * never; the answers stay the same however often they are asked */
    begin(); head("Alt", "alt", "#?.(svg|svgz)", NULL, ODT_GID_SYSTEM, ID("alt "), ODT_DTF_ASCII, 0); add();
    begin(); head("Bad", "bad", "#?.(x", NULL, ODT_GID_SYSTEM, ID("bad "), ODT_DTF_BINARY, 0); add();
    parses = matches = 0;
    for (i = 0; i < 20; i++) {
        CHECK(!strcmp(rec("<svg/>\n", 7, "a.SVGZ"), "Alt"));
        CHECK(!strcmp(rec("<svg/>\n", 7, "a.svgx"), "ascii"));
        /* Bad (fits nothing), Lit (not its own name), High, Low */
        CHECK(!strcmp(rec("\1\2", 2, "odd.bin"), "binary"));
    }
    /* only Alt and Bad: Lit (no wildcards), High and Low were parsed for
     * the checks above, and kept */
    CHECK(parses == 2);
    CHECK(matches == 20 * (2 + 2));         /* Alt twice; High and Low */
    for (n = db.all.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        e = (struct odt_entry *)((char *)n - offsetof(struct odt_DataType, dtn_Node2));
        if (!strcmp(e->dth.dth_Name, "Alt")) CHECK(e->pat_state == ODT_PAT_WILD && e->pat_tok);
        if (!strcmp(e->dth.dth_Name, "Lit")) CHECK(e->pat_state == ODT_PAT_LITERAL && !e->pat_tok);
        if (!strcmp(e->dth.dth_Name, "Bad")) CHECK(e->pat_state == ODT_PAT_BAD && !e->pat_tok);
        if (!strcmp(e->dth.dth_Name, "GIF")) CHECK(e->pat_state == ODT_PAT_ANY && !e->pat_tok);
    }
    /* a token that can't be allocated: no fit, and parsed next time */
    {
        struct odt_entry *alt = odt_db_find_name(&db, "alt");
        if (alt && alt->pat_tok) { t_free(NULL, alt->pat_tok); alt->pat_tok = NULL; alt->pat_state = ODT_PAT_NEW; }
        fail_alloc = 1;
        CHECK(!strcmp(rec("<svg/>\n", 7, "a.svg"), "ascii"));
        fail_alloc = 0;
        CHECK(!strcmp(rec("<svg/>\n", 7, "a.svg"), "Alt") && alt && alt->pat_state == ODT_PAT_WILD);
    }

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

/* ---- text or binary: OS 3.2's rule, and UTF-8 and UTF-16 (ours) ------------- */

static int kind_of(const char *d, unsigned n)
{
    odt_u32 form;
    return odt_classify((const odt_u8 *)d, n, &form);
}
#define KIND(lit) kind_of(lit, sizeof lit - 1)

static void test_text_kinds(void)
{
    static unsigned char r[1084];
    unsigned i, k, texts;
    unsigned long sd = 1;
    odt_u32 form;
    /* UTF-8: bytes 0x80-0x9f inside characters, text when all of it is
     * UTF-8 (OS 3.2 says binary for each of these) */
    CHECK(KIND("na\xc3\xafve caf\xc3\xa9 \xe2\x80\x94 \xe2\x80\x9cquoted\xe2\x80\x9d \xe2\x82\xac" "5\n") == ODT_DTF_ASCII);
    CHECK(KIND("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe3\x83\x86\xe3\x82\xad\xe3\x82\xb9\xe3\x83\x88\n") == ODT_DTF_ASCII);
    CHECK(KIND("\xef\xbb\xbf# Title\n\n\xe2\x80\xa2 item\t\xf0\x9f\x98\x80\r\n") == ODT_DTF_ASCII);     /* BOM, emoji */
    CHECK(KIND("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82") == ODT_DTF_ASCII);                /* Cyrillic */
    CHECK(KIND("ends inside \xe2\x82") == ODT_DTF_ASCII);       /* cut where recognition stops looking */
    CHECK(KIND("ends inside \xf0\x9f\x98") == ODT_DTF_ASCII);
    /* ... and binary when it isn't UTF-8, or has what text hasn't */
    CHECK(KIND("a\x85" "b") == ODT_DTF_BINARY);                 /* a continuation byte alone */
    CHECK(KIND("\xc0\x80 overlong NUL") == ODT_DTF_BINARY);
    CHECK(KIND("\xe0\x80\xaf overlong /") == ODT_DTF_BINARY);
    CHECK(KIND("\xed\xa0\x80 a surrogate") == ODT_DTF_BINARY);
    CHECK(KIND("\xf4\x90\x80\x80 past U+10FFFF") == ODT_DTF_BINARY);
    CHECK(KIND("\xc2\x85 NEL, a C1 control") == ODT_DTF_BINARY);
    CHECK(KIND("\xe2\x82\xac\xe2\x82") == ODT_DTF_ASCII);
    CHECK(KIND("\xe2\x82\xac\xe2\x82x") == ODT_DTF_BINARY);     /* cut short, then more */
    CHECK(KIND("\xe6\x97\xa5\0") == ODT_DTF_BINARY);            /* NUL */
    CHECK(KIND("\xe6\x97\xa5\x01") == ODT_DTF_BINARY);          /* a C0 control text hasn't */
    CHECK(KIND("\xe6\x97\xa5\x1a") == ODT_DTF_BINARY);
    /* OS 3.2's rule where there is no byte 0x80-0x9f, UTF-8 or not */
    CHECK(KIND("caf\xe9 cr\xe8me") == ODT_DTF_ASCII);           /* Latin-1 */
    CHECK(KIND("\xc3\x28 not UTF-8, no C1 byte") == ODT_DTF_ASCII);
    CHECK(KIND("\xff\xfe") == ODT_DTF_ASCII);                   /* a byte order mark alone */
    /* CP1252's 0x80-0x9f (euro, curly quotes, dashes): binary, as on OS
     * 3.2 (not UTF-8; only OS 3.2's rule decides) */
    CHECK(KIND("\x93smart quotes\x94 cost \x80" "5 \x96 or so\n") == ODT_DTF_BINARY);
    CHECK(KIND("it\x92s\n") == ODT_DTF_BINARY);
    CHECK(KIND("na\xefve caf\xe9\n") == ODT_DTF_ASCII);         /* CP1252 above 0x9f only */
    /* UTF-16 with its byte order mark */
    CHECK(KIND("\xff\xfeh\0i\0 \0\xac\x20\n\0") == ODT_DTF_ASCII);             /* LE, a euro */
    CHECK(KIND("\xfe\xff\0h\0i\0\n\x65\xe5\x67\x2c") == ODT_DTF_ASCII);        /* BE, CJK */
    CHECK(KIND("\xff\xfe\x3d\xd8\x00\xde\n\0") == ODT_DTF_ASCII);              /* a surrogate pair */
    CHECK(KIND("\xfe\xff\x65\xe5\x67\x2c\x8a\x9e") == ODT_DTF_BINARY);          /* no space or line end */
    CHECK(KIND("\xff\xfeh\0 \0\x3d\xd8") == ODT_DTF_ASCII);                     /* cut after its first half */
    CHECK(KIND("\xff\xfeh\0\x00\xde") == ODT_DTF_BINARY);                       /* a second half alone */
    CHECK(KIND("\xff\xfe\x3d\xd8h\0") == ODT_DTF_BINARY);                       /* a first half alone */
    CHECK(KIND("\xff\xfeh\0\x00\xe0") == ODT_DTF_BINARY);                       /* private use */
    CHECK(KIND("\xff\xfeh\0\xc0\xdb\x00\xdc") == ODT_DTF_BINARY);               /* plane 16, private use */
    CHECK(KIND("\xff\xfeh\0\0\0") == ODT_DTF_BINARY);                           /* NUL */
    CHECK(KIND("\xff\xfeh\0\x85\0") == ODT_DTF_BINARY);                         /* C1 */
    CHECK(KIND("\xff\xfeh\0\x01\0") == ODT_DTF_BINARY);                         /* C0 */
    CHECK(KIND("\xfe\xff\0h\xff\xff") == ODT_DTF_BINARY);                       /* U+FFFF */
    CHECK(KIND("\xfe\xff\x01\x02\x03\x04\0\0\x10\x11") == ODT_DTF_BINARY);     /* binary after a mark */
    CHECK(KIND("h\0i\0") == ODT_DTF_BINARY);                    /* UTF-16 without its mark: OS 3.2's rule */
    /* binary stays binary: 3,000 random buffers, 1,000 of them with no
     * byte under 0x80 (OS 3.2's rule has only 0x80-0x9f to go on) and
     * 1,000 with no C0 byte text hasn't, 1,000 after a UTF-16 mark; 16
     * bytes and up: none that OS 3.2 calls binary is text now */
    for (texts = 0, i = 0; i < 3000; i++) {
        unsigned n = 16 + i % 1069;
        for (k = 0; k < n; k++) {
            sd = sd * 1103515245UL + 12345UL;
            r[k] = (unsigned char)(sd >> 16);
            if (i < 1000) r[k] |= 0x80;
            else if (i < 2000 && r[k] < 0x20) r[k] = (unsigned char)(r[k] + 0x40);
        }
        if (i >= 2000) { r[0] = 0xff; r[1] = 0xfe; }
        if (odt_classify(r, n, &form) == ODT_DTF_ASCII) {
            int os32_binary = 0;
            for (k = 0; k < n; k++)
                if ((r[k] >= 0x80 && r[k] < 0xa0) ||
                    (r[k] < 0x20 && !((r[k] >= 7 && r[k] <= 13) || r[k] == 0x11 || r[k] == 0x13 || r[k] == 0x1b)))
                    os32_binary = 1;
            texts += os32_binary;
        }
    }
    CHECK(texts == 0);
}

/* ---- a corpus: the cached patterns against recognition from scratch ---------- */

/* Recognition as the core did before it kept tokens: every pattern parsed
 * for every file it is asked about (by its own counter), the rest from
 * the core's lists, odt_classify and odt_mask_match. */
static long ref_parses;
static int ref_name_fits(const char *pat, const char *name)
{
    char tok[512];
    int wild;
    if (!pat) return 1;
    if (!name) name = "";
    ref_parses++;
    if (strlen(pat) * 2 + 2 > sizeof tok || to_regex(pat, tok, 2 * strlen(pat) + 2, &wild) < 0) return 0;
    if (!wild) return strcasecmp(pat, name) != 0;
    {
        regex_t re;
        int r;
        if (regcomp(&re, tok, REG_EXTENDED | REG_ICASE | REG_NOSUB)) return 0;
        r = regexec(&re, name, 0, NULL, 0) == 0;
        regfree(&re);
        return r;
    }
}
static struct odt_entry *ref_recognize(const odt_u8 *buf, odt_u32 len, const char *name, struct odt_DTHookContext *d)
{
    odt_u32 form;
    struct odt_Node *n;
    odt_u16 kind;
    if (!len) return NULL;
    kind = odt_classify(buf, len, &form);
    for (n = db.kind[kind].lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct odt_entry *e = (struct odt_entry *)n;
        const char *pat = e->dth.dth_Pattern;
        if (e->builtin) continue;
        if (!e->dth.dth_MaskLen && pat && !strcmp(pat, "#?")) continue;
        if (!odt_mask_match(&e->dth, buf, len)) continue;
        if (!ref_name_fits(pat, name)) continue;
        if (e->code && !corpus_hook(e, d)) continue;
        return e;
    }
    for (n = db.kind[kind].lh_Head; n->ln_Succ; n = n->ln_Succ)
        if (((struct odt_entry *)n)->builtin) return (struct odt_entry *)n;
    return NULL;
}

static unsigned long seed = 20261107;
static unsigned rnd(unsigned n) { seed = seed * 1103515245UL + 12345UL; return (unsigned)((seed >> 8) % n); }

static const char *const exts[] = { "svg", "svgz", "mod", "txt", "md", "dat", "iff", "gif", "bin", "x", "csv", "ilbm" };
#define NEXTS (sizeof exts / sizeof exts[0])
static const char bin_alpha[] = "ABab01\x89PNG\x1a\xff";
static const char txt_alpha[] = "ABab01<?#! ";

static unsigned char cbuf[1084];
static long corpus_files, corpus_same;

static void corpus_one(const odt_u8 *buf, odt_u32 len, const char *name)
{
    struct odt_DTHookContext d;
    struct odt_entry *a, *b;
    memset(&d, 0, sizeof d);
    d.dthc_Buffer = (char *)buf;
    d.dthc_BufferLength = len;
    a = odt_recognize(&env, &db, buf, len, name, &d);
    b = ref_recognize(buf, len, name, &d);
    corpus_files++;
    if (a == b) corpus_same++;
    else printf("FAIL corpus: %s (%lu bytes) is %s, from scratch %s\n", name ? name : "(no name)", (unsigned long)len,
                a ? a->dth.dth_Name : "(none)", b ? b->dth.dth_Name : "(none)");
}

static void corpus_free(void)
{
    struct odt_entry *e;
    while (db.all.lh_Head->ln_Succ) {
        e = (struct odt_entry *)((char *)db.all.lh_Head - offsetof(struct odt_DataType, dtn_Node2));
        odt_db_remove(e);
        odt_free_entry(&env, e);
    }
}

static void corpus_builtins(void)
{
    odt_db_init(&db);
    odt_db_add(&db, odt_builtin(&env, "binary", "binary", ODT_GID_SYSTEM, ID("bina"), ODT_DTF_BINARY, 0));
    odt_db_add(&db, odt_builtin(&env, "ascii", "ascii", ODT_GID_TEXT, ID("asci"), ODT_DTF_ASCII, 0));
    odt_db_add(&db, odt_builtin(&env, "iff", "iff", ODT_GID_SYSTEM, ID("iff\0"), ODT_DTF_IFF, 0));
}

/* 240 made-up descriptors (masks from a small alphabet so they collide;
 * "#?", extensions, alternatives, literal names (with a mask: without
 * one such a type takes nearly everything), sets, a broken pattern;
 * priorities; DTF_CASE; one in ten with recognition code) and 6,000 files
 * made from their masks or at random, with names from the same
 * extensions: the core's answer must be the from-scratch one for each */
static void test_corpus(void)
{
    char masks[240][16], pats[240][40], names[240][8], fname[32];
    unsigned kinds[240], i, k;
    long p0;

    corpus_builtins();
    for (i = 0; i < 240; i++) {
        unsigned kind = rnd(10) < 5 ? ODT_DTF_BINARY : rnd(10) < 6 ? ODT_DTF_ASCII : ODT_DTF_IFF;
        unsigned ml = rnd(10) < 3 ? 0 : 2 + rnd(7), flags = kind | (rnd(2) ? ODT_DTF_CASE : 0);
        const char *e1 = exts[rnd(NEXTS)], *e2 = exts[rnd(NEXTS)];
        static const unsigned pris[] = { 0, 0, 0, 1, 5, 0xffff };
        kinds[i] = kind;
        if (kind == ODT_DTF_IFF) {
            static const char *const forms[] = { "ILBM", "8SVX", "ANIM", "FTXT", "ACBM" };
            ml = rnd(3) ? 12 : 0;
            sprintf(masks[i], "FORM????%s", forms[rnd(5)]);
            masks[i][ml] = 0;
        } else {
            for (k = 0; k < ml; k++)
                masks[i][k] = rnd(6) == 0 ? '?' : kind == ODT_DTF_ASCII ? txt_alpha[rnd(sizeof txt_alpha - 1)]
                                                                     : bin_alpha[rnd(sizeof bin_alpha - 1)];
            masks[i][ml] = 0;
        }
        switch (rnd(10)) {
        case 0: case 1: case 2: case 3: strcpy(pats[i], "#?"); break;
        case 4: sprintf(pats[i], "#?.%s", e1); break;
        case 5: sprintf(pats[i], "#?.(%s|%s)", e1, e2); break;
        case 6: sprintf(pats[i], "(%s.#?|#?.%s)", e1, e2); break;
        case 7: sprintf(pats[i], rnd(4) || !ml ? "#?.%s#?" : "f%s.dat", e1); break;
        case 8: sprintf(pats[i], rnd(8) ? "#?.[a-m]%s" : "#?.(%s", e1); break;
        default: sprintf(pats[i], "#?%c", e1[0]); break;
        }
        sprintf(names[i], "T%03u", i);
        begin();
        head(names[i], "corpus", pats[i], ml ? masks[i] : NULL, ODT_GID_SYSTEM, i, flags, pris[rnd(6)]);
        if (rnd(10) == 0) chunk("DTCD", 4);
        add();
    }
    hook_corpus = 1;
    p0 = parses;
    ref_parses = 0;
    for (i = 0; i < 6000; i++) {
        unsigned len = 1 + rnd(rnd(4) ? 64 : 1084), from = rnd(240), how = rnd(4);
        const char *alpha = how == 0 ? bin_alpha : txt_alpha;
        unsigned alen = (unsigned)strlen(alpha) + (how == 0);      /* NUL in the binary one */
        for (k = 0; k < len; k++) cbuf[k] = (unsigned char)alpha[rnd(alen)];
        if (how >= 2) {                                             /* from a descriptor's mask */
            const char *m = masks[from];
            for (k = 0; m[k] && k < len; k++) if (m[k] != '?') cbuf[k] = (unsigned char)(rnd(5) ? m[k] : m[k] ^ 0x20);
            if (kinds[from] == ODT_DTF_IFF && len >= 12) { memcpy(cbuf, "FORM", 4); memcpy(cbuf + 8, m + 8, 4); }
        }
        switch (rnd(8)) {
        case 0: corpus_one(cbuf, len, NULL); continue;
        case 1: sprintf(fname, "f%s.dat", exts[rnd(NEXTS)]); break;
        case 2: sprintf(fname, "%s.x%s", exts[rnd(NEXTS)], exts[rnd(NEXTS)]); break;
        default: sprintf(fname, "pic%u.%s", i, exts[rnd(NEXTS)]); break;
        }
        if (rnd(3) == 0) fname[0] ^= 0x20;
        corpus_one(cbuf, len, fname);
    }
    hook_corpus = 0;
    printf("corpus: %ld files, %ld as from scratch; patterns parsed %ld times (from scratch %ld)\n",
           corpus_files, corpus_same, parses - p0, ref_parses);
    CHECK(corpus_same == corpus_files && corpus_files == 6000);
    CHECK(parses - p0 <= 240);
    corpus_free();
}

/* DESCDIR FILE...: the descriptors in DESCDIR (DEVS:DataTypes as a pack
 * has it) and the files given, each recognised as the core does and from
 * scratch; one line a file */
static void test_dir(int argc, char **argv)
{
    char path[1024];
    static unsigned char d[65536];
    FILE *f;
    int i;
    struct dirent *de;
    DIR *dir = opendir(argv[1]);
    long p0;
    if (!dir) { printf("FAIL no %s\n", argv[1]); fails++; return; }
    corpus_builtins();
    while ((de = readdir(dir)) != NULL) {
        struct odt_entry *e;
        size_t n;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", argv[1], de->d_name);
        if (!(f = fopen(path, "rb"))) continue;
        n = fread(d, 1, sizeof d, f);
        fclose(f);
        if (odt_parse(&env, d, (odt_u32)n, &e) == ODT_OK) {
            e = odt_db_add(&db, e);
            if (e) odt_free_entry(&env, e);
        }
    }
    closedir(dir);
    p0 = parses;
    ref_parses = 0;
    hook_corpus = 1;
    for (i = 2; i < argc; i++) {
        const char *base = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
        struct odt_DTHookContext dc;
        struct odt_entry *e;
        size_t n;
        if (!(f = fopen(argv[i], "rb"))) continue;
        n = fread(cbuf, 1, sizeof cbuf, f);
        fclose(f);
        memset(&dc, 0, sizeof dc);
        dc.dthc_Buffer = (char *)cbuf;
        dc.dthc_BufferLength = (odt_u32)n;
        e = odt_recognize(&env, &db, cbuf, (odt_u32)n, base, &dc);
        printf("%s: %s\n", base, e ? e->dth.dth_Name : "(none)");
        corpus_one(cbuf, (odt_u32)n, base);
    }
    hook_corpus = 0;
    printf("dir: %ld files, %ld as from scratch; patterns parsed %ld times (from scratch %ld)\n",
           corpus_files, corpus_same, parses - p0, ref_parses);
    CHECK(corpus_same == corpus_files);
    corpus_free();
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        test_dir(argc, argv);
        CHECK(live == 0);
        printf("%d checks, %d failed\n", checks, fails);
        return fails != 0;
    }
    test_descriptors();
    test_text_kinds();
    test_corpus();
    test_methods();
    test_strings();
    CHECK(live == 0);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
