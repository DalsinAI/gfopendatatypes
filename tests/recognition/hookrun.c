/* The descriptors' recognition code as built (Datatypes/hooks/build.sh: an
 * AmigaOS load file each), run under qemu-m68k: the load file's hunks are
 * loaded and relocated here, as InternalLoadSeg would, and its first code is
 * called with the DTHookContext in A0, as datatypes.library calls it. The
 * library calls docservice makes go to stand-in exec, dos and expansion
 * libraries (a jump table each) that answer as the case says.
 *
 *   hookrun HOOKFILE CASE SAMPLE...
 *
 * CASE (docservice only): three digits, whether gfopenservice.device is in
 * memory, whether DEVS:gfopenservice.device is there, and 0 (no services
 * card or Cradle), 1 (a services card) or 2 (a paired Cradle). One line
 * out for each sample: the hook's answer (the low word of D0), and for
 * docservice whether every Forbid had its Permit and every library opened
 * was closed and every lock unlocked.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "dt_core.h"

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* ---- the load file --------------------------------------------------------- */

#define MAXHUNKS 8
static unsigned char *hunk[MAXHUNKS];

static void *load(const char *path)
{
    static unsigned char file[65536];
    FILE *f = fopen(path, "rb");
    size_t n, at = 0;
    unsigned long first, last, i, h = 0, sizes[MAXHUNKS];
    if (!f) { perror(path); exit(2); }
    n = fread(file, 1, sizeof file, f);
    fclose(f);
#define NEXT() (at + 4 <= n ? (at += 4, be32(file + at - 4)) : (fprintf(stderr, "%s: short\n", path), exit(2), 0UL))
    if (NEXT() != 0x3f3) { fprintf(stderr, "%s: not a load file\n", path); exit(2); }
    while (NEXT()) ;                        /* no resident library names */
    NEXT();
    first = NEXT();
    last = NEXT();
    if (first || last >= MAXHUNKS) { fprintf(stderr, "%s: %lu hunks\n", path, last + 1); exit(2); }
    for (i = 0; i <= last; i++) {
        sizes[i] = NEXT() & 0x3fffffff;
        hunk[i] = mmap(NULL, sizes[i] * 4 + 4, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (hunk[i] == MAP_FAILED) { perror("mmap"); exit(2); }
    }
    while (at < n) {
        unsigned long t = NEXT() & 0x3fffffff, k, cnt, to;
        switch (t) {
        case 0x3e9: case 0x3ea:                 /* HUNK_CODE, HUNK_DATA */
            k = NEXT();
            memcpy(hunk[h], file + at, k * 4);
            at += k * 4;
            break;
        case 0x3eb: NEXT(); break;              /* HUNK_BSS: mmap cleared it */
        case 0x3ec:                             /* HUNK_RELOC32 */
            while ((cnt = NEXT())) {
                to = NEXT();
                while (cnt--) {
                    unsigned char *p = hunk[h] + NEXT();
                    unsigned long v = be32(p) + (unsigned long)hunk[to];
                    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
                }
            }
            break;
        case 0x3f0: case 0x3f1:                 /* HUNK_SYMBOL, HUNK_DEBUG */
            if (t == 0x3f1) { k = NEXT(); at += k * 4; break; }
            while ((k = NEXT() & 0xffffff)) at += k * 4 + 4;
            break;
        case 0x3f2: h++; break;                 /* HUNK_END */
        default: fprintf(stderr, "%s: hunk type %lx\n", path, t); exit(2);
        }
    }
#undef NEXT
    __builtin___clear_cache((char *)hunk[0], (char *)hunk[0] + sizes[0] * 4);
    return hunk[0];
}

/* ---- stand-in libraries ---------------------------------------------------- */

static int in_memory, on_disk, reach;
int forbids, opens, locks, bad;     /* the stubs count in forbids and bad */

__attribute__((used)) unsigned long c_findname(const char *name)
{
    return in_memory && !strcmp(name, "gfopenservice.device") ? 0x1000 : 0;
}
__attribute__((used)) unsigned long c_openlibrary(const char *name, unsigned long version)
{
    if (strcmp(name, "expansion.library") || version > 47) { bad++; return 0; }
    opens++;
    return (unsigned long)hunk[MAXHUNKS - 1];   /* the stand-in expansion.library */
}
__attribute__((used)) void c_closelibrary(void *lib)
{
    if (lib != hunk[MAXHUNKS - 1]) bad++;
    opens--;
}
__attribute__((used)) unsigned long c_findconfigdev(void *old, long manufacturer, long product)
{
    if (old || manufacturer != 0xda15 || product != -1) bad++;
    return reach == 1 ? 0x2000 : 0;
}
__attribute__((used)) unsigned long c_lock(const char *name, long mode)
{
    int yes = (!strcmp(name, "DEVS:gfopenservice.device") && on_disk) ||
              (!strcmp(name, "ENV:GFOpenService/Paired") && reach == 2);
    if (mode != -2) bad++;                      /* ACCESS_READ */
    if (!yes) return 0;
    locks++;
    return 0x3000;
}
__attribute__((used)) void c_unlock(unsigned long lock)
{
    if (lock != 0x3000) bad++;
    locks--;
}

/* Each library call: the arguments from their registers to a C call; D0
 * back. D2-D7/A2-A6 kept, as by any library. */
__asm__(".text\n"
        "s_forbid:  addq.l #1,forbids\n rts\n"
        "s_permit:  subq.l #1,forbids\n rts\n"
        "s_findname: move.l %a1,-(%sp)\n jsr c_findname\n addq.l #4,%sp\n rts\n"
        "s_openlib: move.l %d0,-(%sp)\n move.l %a1,-(%sp)\n jsr c_openlibrary\n addq.l #8,%sp\n rts\n"
        "s_closelib: move.l %a1,-(%sp)\n jsr c_closelibrary\n addq.l #4,%sp\n rts\n"
        "s_findcd:  move.l %d1,-(%sp)\n move.l %d0,-(%sp)\n move.l %a0,-(%sp)\n jsr c_findconfigdev\n"
        "           lea 12(%sp),%sp\n rts\n"
        "s_lock:    move.l %d2,-(%sp)\n move.l %d1,-(%sp)\n jsr c_lock\n addq.l #8,%sp\n rts\n"
        "s_unlock:  move.l %d1,-(%sp)\n jsr c_unlock\n addq.l #4,%sp\n rts\n"
        "s_none:    addq.l #1,bad\n moveq #0,%d0\n rts\n");
extern char s_forbid[], s_permit[], s_findname[], s_openlib[], s_closelib[], s_findcd[], s_lock[], s_unlock[], s_none[];

/* A library base with lvos jump slots below it, each to s_none but those given */
static unsigned char *library(int lvos, int size, const int *offs, char *const *to)
{
    unsigned char *m = mmap(NULL, lvos * 6 + size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char *base = m + lvos * 6;
    int i;
    for (i = 1; i <= lvos; i++) {
        unsigned char *s = base - 6 * i;
        unsigned long a = (unsigned long)s_none;
        int k;
        for (k = 0; offs[k]; k++)
            if (offs[k] == -6 * i) a = (unsigned long)to[k];
        s[0] = 0x4e; s[1] = 0xf9;
        s[2] = a >> 24; s[3] = a >> 16; s[4] = a >> 8; s[5] = a;
    }
    __builtin___clear_cache((char *)m, (char *)base);
    return base;
}

static unsigned long call(void *entry, struct odt_DTHookContext *dthc)
{
    register unsigned long d0 __asm__("d0");
    register void *a0 __asm__("a0") = dthc;
    register void *a1 __asm__("a1") = entry;
    __asm__ volatile("jsr (%2)" : "=d"(d0), "+a"(a0), "+a"(a1) : : "d1", "cc", "memory");
    return d0;
}

int main(int argc, char **argv)
{
    static const int exec_offs[] = { -132, -138, -276, -552, -414, 0 };
    static char *const exec_to[] = { s_forbid, s_permit, s_findname, s_openlib, s_closelib };
    static const int dos_offs[] = { -84, -90, 0 };
    static char *const dos_to[] = { s_lock, s_unlock };
    static const int exp_offs[] = { -72, 0 };
    static char *const exp_to[] = { s_findcd };
    unsigned char *sys, *dos;
    void *entry;
    int i;

    if (argc < 3) { fprintf(stderr, "usage: hookrun HOOKFILE CASE SAMPLE...\n"); return 2; }
    entry = load(argv[1]);
    sys = library(200, 600, exec_offs, exec_to);
    dos = library(200, 64, dos_offs, dos_to);
    hunk[MAXHUNKS - 1] = library(40, 64, exp_offs, exp_to);
    /* exec's DeviceList at 350: an empty list; FindName is the stand-in's */
    in_memory = argv[2][0] == '1';
    on_disk = argv[2][1] == '1';
    reach = argv[2][2] - '0';
    for (i = 3; i < argc; i++) {
        static unsigned char buf[1084];
        struct odt_DTHookContext dthc;
        FILE *f = fopen(argv[i], "rb");
        size_t n;
        unsigned long r;
        if (!f) { perror(argv[i]); return 2; }
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        memset(&dthc, 0, sizeof dthc);
        dthc.dthc_SysBase = sys;
        dthc.dthc_DOSBase = dos;
        dthc.dthc_Buffer = (char *)buf;
        dthc.dthc_BufferLength = n;
        forbids = opens = locks = bad = 0;
        r = call(entry, &dthc);
        printf("%d%s\n", (r & 0xffff) != 0, forbids || opens || locks || bad ? " unbalanced" : "");
    }
    return 0;
}
