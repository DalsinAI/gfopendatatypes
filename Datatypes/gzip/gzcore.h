/*
 * gzip.datatype's decoder: gzip (RFC 1952, one member or several one after
 * another), zlib-wrapped deflate (RFC 1950, HTTP's "deflate") and raw
 * deflate (RFC 1951, which some servers send for "deflate"), inflated by
 * z.library. Streaming: input and room may come a byte at a time.
 *
 * Plain C with no OS calls and no static data: the calls to z.library go
 * through common/zlibrary.h with the base the caller hands in.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef GZCORE_H
#define GZCORE_H

#include <exec/types.h>
#include "zlibrary.h"
#include "sdtcore.h"

/* Wrappers (SDTW_ in datatypes/streamclass.h) */
#define GZ_AUTO  0
#define GZ_GZIP  1
#define GZ_ZLIB  2
#define GZ_RAW   3

struct gz_state {
    struct zl_stream z;
    struct Library *zbase;
    UBYTE want;                  /* GZ_AUTO or a wrapper */
    UBYTE kind;                  /* the wrapper found (GZ_AUTO until known) */
    UBYTE phase;                 /* see gzcore.c */
    UBYTE live;                  /* z is initialised */
    UBYTE head[2];               /* first bytes held back while telling the wrapper */
    UBYTE nhead, usedhead;
};

/* Sets up s (no z.library call yet). want: GZ_AUTO or a wrapper. */
void gz_init(struct gz_state *s, struct Library *zbase, LONG want);

/* One SDTM_CODEC run (sdtcore.h): SDT_MORE, SDT_DONE or SDT_ERROR. */
LONG gz_run(struct gz_state *s, struct sdt_call *c);

/* Ends the inflate, if any. */
void gz_free(struct gz_state *s);

/* "gzip", "zlib", "deflate", or NULL until known. */
const char *gz_format(const struct gz_state *s);

#endif
