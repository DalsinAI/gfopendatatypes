/*
 * gfopenimage datatypes: the calls they make to z.library (GFOpenZlib, ours:
 * DalsinAI amicradle-guest libraries/openzlib, API and ABI those of the
 * Aminet z.library). Every inflate and deflate in the datatypes goes
 * through it: no zlib is linked in.
 *
 * The library base is passed to each call, so a datatype keeps it in its
 * object, not in a global. The init calls take no version or stream size
 * (the library is the version), as z.library's own.
 *
 * On the Amiga the calls are common/zlibrary.c, through z.library's jump
 * table (openzlib's z.lvo). On x86 or ARM64 cores the tests give their own
 * (tests/zlhost.c, over GFOpenZlib's core).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef ZLIBRARY_H
#define ZLIBRARY_H

#include <exec/types.h>

struct Library;

#define ZL_NAME    "z.library"
#define ZL_VERSION 53              /* GFOpenZlib is 53.12; the Aminet one 53.x */

/* zlib.h's z_stream, as z.library lays it out on the 68k (56 bytes) */
struct zl_stream {
    const UBYTE *next_in;
    ULONG avail_in;
    ULONG total_in;
    UBYTE *next_out;
    ULONG avail_out;
    ULONG total_out;
    const char *msg;
    APTR state;                    /* the library's own */
    APTR zalloc, zfree, opaque;    /* NULL: z.library allocates for itself */
    LONG data_type;
    ULONG adler;
    ULONG reserved;
};

/* zlib.h's numbers */
#define ZL_NO_FLUSH     0
#define ZL_SYNC_FLUSH   2
#define ZL_FULL_FLUSH   3
#define ZL_FINISH       4
#define ZL_OK           0
#define ZL_STREAM_END   1
#define ZL_NEED_DICT    2
#define ZL_STREAM_ERROR (-2)
#define ZL_DATA_ERROR   (-3)
#define ZL_MEM_ERROR    (-4)
#define ZL_BUF_ERROR    (-5)
#define ZL_DEFLATED     8
#define ZL_DEFAULT_STRATEGY 0
#define ZL_FILTERED     1

LONG zl_InflateInit2(struct Library *ZBase, struct zl_stream *s, LONG windowBits);
LONG zl_Inflate(struct Library *ZBase, struct zl_stream *s, LONG flush);
LONG zl_InflateEnd(struct Library *ZBase, struct zl_stream *s);
LONG zl_InflateReset(struct Library *ZBase, struct zl_stream *s);
LONG zl_DeflateInit2(struct Library *ZBase, struct zl_stream *s, LONG level, LONG method,
                     LONG windowBits, LONG memLevel, LONG strategy);
LONG zl_Deflate(struct Library *ZBase, struct zl_stream *s, LONG flush);
LONG zl_DeflateEnd(struct Library *ZBase, struct zl_stream *s);
ULONG zl_Crc32(struct Library *ZBase, ULONG crc, const UBYTE *buf, ULONG len);

#endif
