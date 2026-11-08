/* Which descriptor gets each sample: datatypes.library's own recognition
 * (libraries/opendatatypes/dt_core.c: the kinds, the order, the masks, the
 * patterns) over descriptor files written by Datatypes/descriptors.sh, with
 * the library's built-in binary, ascii and iff types, and each descriptor's
 * recognition code (DTCD) run as the C it is built from
 * (Datatypes/hooks/dthooks.h). Built for the 68k and run under qemu-m68k by
 * test_recognition.py, which writes the descriptors and the samples and
 * checks the answers.
 *
 *   recognize [-r] DESCRIPTOR... -- SAMPLE NAME SERVICE ...
 *   recognize -h HOOK SAMPLE...
 *
 * The descriptors are added in the order given (-r: the other way round,
 * as a directory may list them). A sample is a file holding the data, NAME
 * the file name recognition sees ("-" for data in memory, no name), and
 * SERVICE 1 or 0: whether the doc.render service can be there, for the
 * docservice hook. One line out for each sample: the descriptor's name.
 * With -h, one hook's answer (1 or 0) for each sample, to hold the built
 * hook (hookrun.c) to.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include "dt_core.h"
#include "dthooks.h"

#define PEEK 1084                       /* opendatatypes_lib.c: what the library reads */

static void *t_alloc(void *ctx, odt_u32 n) { (void)ctx; return calloc(1, n); }
static void t_free(void *ctx, void *p) { (void)ctx; free(p); }

/* An AmigaDOS pattern as an extended glob, written into tok (size bytes):
 * #x is *(x), #? is *, (a|b) is @(a|b), ~x is !(x), ' quotes the next
 * character. 1 when it has wildcards, 0 when it has none (the core then
 * compares the pattern itself, as OS 3.2 does), -1 when the glob does not
 * fit. parse_name of struct odt_env. */
static int t_parse(void *ctx, const char *pat, char *tok, odt_u32 size)
{
    size_t i = 0;
    int wild = 0;
    const char *p;
    (void)ctx;
    if (!size) return -1;
    for (p = pat; *p; p++) {
        char add[8];
        int n = 0;
        switch (*p) {
        case '#':
            wild = 1;
            if (p[1] == '?') { add[n++] = '*'; p++; }
            else {
                add[n++] = '*';
                if (p[1] != '(') { add[n++] = '('; add[n++] = *++p; add[n++] = ')'; }
            }
            break;
        case '(':
            wild = 1;
            if (!i || (tok[i - 1] != '*' && tok[i - 1] != '!')) add[n++] = '@';
            add[n++] = '(';
            break;
        case '~':
            wild = 1;
            add[n++] = '!';
            if (p[1] != '(') { add[n++] = '('; add[n++] = *++p; add[n++] = ')'; }
            break;
        case '?': case '|': case ')': wild = 1; add[n++] = *p; break;
        case '\'': if (p[1]) { add[n++] = '\\'; add[n++] = *++p; } break;
        case '*': case '[': case ']': case '\\': add[n++] = '\\'; add[n++] = *p; break;
        default: add[n++] = *p;
        }
        if (i + (size_t)n + 1 > size) return -1;
        memcpy(tok + i, add, (size_t)n);
        i += (size_t)n;
    }
    tok[i] = 0;
    return wild ? 1 : 0;
}

/* match_name: the glob t_parse wrote, without regard to case. */
static int t_match(void *ctx, const char *tok, const char *name)
{
    (void)ctx;
    return fnmatch(tok, name, FNM_CASEFOLD | FNM_EXTMATCH) == 0;
}

static int service;
static int unknown_hooks;

/* The hook a descriptor's DTCD holds, by the DTHOOK: tag in it
 * (Datatypes/hooks/hookentry.h; the test's stand-in DTCDs are the tag alone) */
static int t_hook(void *ctx, struct odt_entry *e, struct odt_DTHookContext *d)
{
    const unsigned char *b = (const unsigned char *)d->dthc_Buffer;
    unsigned long n = d->dthc_BufferLength;
    odt_u32 i;
    (void)ctx;
    for (i = 0; i + 7 < e->code_len; i++)
        if (!memcmp(e->code + i, "DTHOOK:", 7)) {
            const char *h = (const char *)e->code + i + 7;
            if (!strcmp(h, "matroska")) return dth_matroska(b, n);
            if (!strcmp(h, "mpegaudio")) return dth_mpegaudio(b, n);
            if (!strcmp(h, "oggtheora")) return dth_oggtheora(b, n);
            if (!strcmp(h, "docservice")) return dth_docservice(service, service, 0);
            break;
        }
    printf("FAIL %s: recognition code with no hook the test knows\n", e->dth.dth_Name);
    unknown_hooks++;
    return 0;
}

static const struct odt_env env = { NULL, t_alloc, t_free, t_parse, t_match, t_hook };

static unsigned char *slurp(const char *path, unsigned long max, unsigned long *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b = malloc(max ? max : 1);
    if (!f || !b) { fprintf(stderr, "recognize: can't read %s\n", path); exit(2); }
    *len = fread(b, 1, max, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    struct odt_db db;
    struct odt_entry *e;
    int i, first = 1, last, reverse = 0, sep;

    if (argc > 2 && !strcmp(argv[1], "-h")) {
        for (i = 3; i < argc; i++) {
            unsigned long len;
            unsigned char *buf = slurp(argv[i], PEEK, &len);
            int r = -1;
            if (!strcmp(argv[2], "matroska")) r = dth_matroska(buf, len);
            else if (!strcmp(argv[2], "mpegaudio")) r = dth_mpegaudio(buf, len);
            else if (!strcmp(argv[2], "oggtheora")) r = dth_oggtheora(buf, len);
            if (r < 0) { fprintf(stderr, "recognize: no hook %s\n", argv[2]); return 2; }
            printf("%d\n", r);
            free(buf);
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-r")) { reverse = 1; first = 2; }
    for (sep = first; sep < argc && strcmp(argv[sep], "--"); sep++)
        ;
    if (sep >= argc) { fprintf(stderr, "usage: recognize [-r] DESCRIPTOR... -- SAMPLE NAME SERVICE ...\n"); return 2; }
    odt_db_init(&db);
    /* the library's own types first, as load() adds them */
    odt_db_add(&db, odt_builtin(&env, "binary", "binary", 0x73797374UL, 0x62696e61UL, ODT_DTF_BINARY, 0));
    odt_db_add(&db, odt_builtin(&env, "ascii", "ascii", 0x74657874UL, 0x61736369UL, ODT_DTF_ASCII, 0));
    odt_db_add(&db, odt_builtin(&env, "iff", "iff", 0x73797374UL, 0x69666600UL, ODT_DTF_IFF, 0));
    last = sep - 1;
    for (i = 0; i <= last - first; i++) {
        const char *path = argv[reverse ? last - i : first + i];
        unsigned long len;
        unsigned char *form = slurp(path, 65536, &len);
        int r = odt_parse(&env, form, len, &e);
        if (r != ODT_OK) { printf("FAIL %s: not a descriptor (%d)\n", path, r); return 1; }
        if (odt_db_add(&db, e)) { printf("FAIL %s: a second descriptor named %s\n", path, e->dth.dth_Name); return 1; }
        free(form);
    }
    for (i = sep + 1; i + 2 < argc; i += 3) {
        struct odt_DTHookContext dthc;
        unsigned long len;
        unsigned char *buf = slurp(argv[i], PEEK, &len);
        memset(&dthc, 0, sizeof dthc);
        dthc.dthc_Buffer = (char *)buf;
        dthc.dthc_BufferLength = len;
        service = argv[i + 2][0] == '1';
        e = odt_recognize(&env, &db, buf, len, strcmp(argv[i + 1], "-") ? argv[i + 1] : NULL, &dthc);
        printf("%s\n", e ? e->dth.dth_Name : "-");
        free(buf);
    }
    return unknown_hooks ? 1 : 0;
}
