/*
 * gfopenimage datatypes: z.library's calls (zlibrary.h), through its jump
 * table: the Aminet z.library's order and registers (its z_lib.sfd), which
 * GFOpenZlib follows from 53.13 (its z.lvo); 53.12 had InflateInit2,
 * InflateReset and CRC32 elsewhere, so these need z.library 53.13 or the
 * Aminet one. The stream in A0, the other arguments in D0 upwards, the
 * base in A6. D0, D1, A0 and A1 are the library's to
 * change; the result comes back in D0.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include "zlibrary.h"

/* z.library, but not GFOpenZlib 53.12: its jump table was wrong from -102
 * on (InflateInit2, InflateReset and CRC32 among them), so calling it here
 * would jump into the wrong function. NULL then, as when there is none. */
struct Library *zl_Open(void)
{
    struct Library *b = OpenLibrary((CONST_STRPTR)ZL_NAME, ZL_VERSION);
    if (b && b->lib_Version == 53 && b->lib_Revision == 12 && b->lib_IdString) {
        const char *id = (const char *)b->lib_IdString, *w = "GFOpenZlib";
        for (; *id; id++) {
            int i = 0;
            while (w[i] && id[i] == w[i]) i++;
            if (!w[i]) {
                CloseLibrary(b);
                return NULL;
            }
        }
    }
    return b;
}

#define CALL(lvo, ...)                                                            \
    __asm volatile("jsr %%a6@(" #lvo ":W)"                                        \
                   : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1)                       \
                   : "r"(a6), ##__VA_ARGS__                                       \
                   : "cc", "memory")

#define REGS(base)                                                                \
    register struct Library *a6 __asm("a6") = (base);                             \
    register ULONG d0 __asm("d0") = 0;                                            \
    register ULONG d1 __asm("d1") = 0;                                            \
    register ULONG a0 __asm("a0") = 0;                                            \
    register ULONG a1 __asm("a1") = 0

LONG zl_InflateInit2(struct Library *ZBase, struct zl_stream *s, LONG windowBits)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    d0 = (ULONG)windowBits;
    CALL(-102);
    return (LONG)d0;
}

LONG zl_Inflate(struct Library *ZBase, struct zl_stream *s, LONG flush)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    d0 = (ULONG)flush;
    CALL(-60);
    return (LONG)d0;
}

LONG zl_InflateEnd(struct Library *ZBase, struct zl_stream *s)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    CALL(-66);
    return (LONG)d0;
}

LONG zl_InflateReset(struct Library *ZBase, struct zl_stream *s)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    CALL(-114);
    return (LONG)d0;
}

LONG zl_DeflateInit2(struct Library *ZBase, struct zl_stream *s, LONG level, LONG method,
                     LONG windowBits, LONG memLevel, LONG strategy)
{
    REGS(ZBase);
    register LONG d2 __asm("d2") = windowBits;
    register LONG d3 __asm("d3") = memLevel;
    register LONG d4 __asm("d4") = strategy;
    a0 = (ULONG)s;
    d0 = (ULONG)level;
    d1 = (ULONG)method;
    CALL(-72, "r"(d2), "r"(d3), "r"(d4));
    return (LONG)d0;
}

LONG zl_Deflate(struct Library *ZBase, struct zl_stream *s, LONG flush)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    d0 = (ULONG)flush;
    CALL(-42);
    return (LONG)d0;
}

LONG zl_DeflateEnd(struct Library *ZBase, struct zl_stream *s)
{
    REGS(ZBase);
    a0 = (ULONG)s;
    CALL(-48);
    return (LONG)d0;
}

ULONG zl_Crc32(struct Library *ZBase, ULONG crc, const UBYTE *buf, ULONG len)
{
    REGS(ZBase);
    d0 = crc;
    a0 = (ULONG)buf;
    d1 = len;
    CALL(-138);
    return d0;
}
