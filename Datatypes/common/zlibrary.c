/*
 * openamigaimage datatypes: z.library's calls (zlibrary.h), through its jump
 * table as GFOpenZlib's z.lvo lists it: the stream in A0, the other arguments
 * in D0 upwards, the base in A6. D0, D1, A0 and A1 are the library's to
 * change; the result comes back in D0.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
#include <exec/types.h>

#include "zlibrary.h"

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
    CALL(-132);
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
    CALL(-162);
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
    CALL(-234);
    return d0;
}
