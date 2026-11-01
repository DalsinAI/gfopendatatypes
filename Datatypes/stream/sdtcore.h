/*
 * stream.datatype's core: the coded bytes waiting for the decoder, and the
 * calls that turn them into plain bytes (SDTM_FEED, SDTM_TAKE, SDTM_END,
 * the whole-buffer decode). Plain C with no OS calls and no static data,
 * so it is tested on the host with the decoders (tests/test-stream.sh).
 * The decoder is reached through a function; the class makes it a
 * SDTM_CODEC to the object (streamclass.c).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef SDTCORE_H
#define SDTCORE_H

#include <exec/types.h>

/* The IoErr() codes the cores give (dos/dos.h, datatypes/datatypes.h) */
#define SDT_ERR_MEMORY  103L      /* ERROR_NO_FREE_STORE */
#define SDT_ERR_NOLIB   205L      /* ERROR_OBJECT_NOT_FOUND: no z.library */
#define SDT_ERR_ENDED   212L      /* ERROR_OBJECT_WRONG_TYPE: fed after the end */
#define SDT_ERR_SHORT   2007L     /* DTERROR_NOT_ENOUGH_DATA: cut short */
#define SDT_ERR_DATA    2008L     /* DTERROR_INVALID_DATA: damaged */

/* What SDTM_CODEC carries, with its numbers (datatypes/streamclass.h) */
#define SDT_OP_INIT  0
#define SDT_OP_RUN   1
#define SDT_OP_FREE  2
#define SDT_LAST     1UL
#define SDT_MORE     0
#define SDT_DONE     1
#define SDT_ERROR    2

struct sdt_call {
    ULONG op, flags;
    const UBYTE *in;
    ULONG in_len;
    UBYTE *out;
    ULONG out_len;
    ULONG in_used, out_made;     /* out */
    LONG error;                  /* out */
};

/* Memory: AllocVec and FreeVec on the Amiga, malloc on the host. alloc
 * need not clear. */
struct sdt_env {
    void *(*alloc)(struct sdt_env *env, ULONG size);
    void (*free)(struct sdt_env *env, void *mem);
};

typedef LONG (*sdt_codec_fn)(void *ctx, struct sdt_call *c);
/* Fills up to len bytes at buf: how many, 0 at the end, or (ULONG)-1 with
 * the reason in *err. */
typedef ULONG (*sdt_pull_fn)(void *ctx, UBYTE *buf, ULONG len, LONG *err);

/* Flags sdt_take() gives (SDTF_ in datatypes/streamclass.h) */
#define SDT_F_END     1UL
#define SDT_F_HUNGRY  2UL
#define SDT_F_ERROR   4UL

struct sdt_core {
    struct sdt_env *env;
    sdt_codec_fn codec;
    void *codec_ctx;
    sdt_pull_fn pull;            /* NULL: input only by sdt_feed() */
    void *pull_ctx;
    UBYTE *q;                    /* coded bytes not yet used: q[start..end) */
    ULONG q_start, q_end, q_size;
    UBYTE ended;                 /* no more input will come */
    UBYTE done;                  /* the decoder said the stream ended */
    UBYTE failed;
    UBYTE pad;
    LONG error;
    ULONG total_in, total_out;
};

void sdt_init(struct sdt_core *c, struct sdt_env *env, sdt_codec_fn codec, void *ctx);
void sdt_set_pull(struct sdt_core *c, sdt_pull_fn pull, void *ctx);

/* Queues len coded bytes (copied). 0, or an error code. */
LONG sdt_feed(struct sdt_core *c, const UBYTE *data, ULONG len);

/* No more input. */
void sdt_end(struct sdt_core *c);

/* Up to len plain bytes into buf; the count, and SDT_F_ flags in *flags. */
ULONG sdt_take(struct sdt_core *c, UBYTE *buf, ULONG len, ULONG *flags);

/* Everything to the end, in one buffer from env (*data, *size; a stream of
 * no bytes gives a buffer of none). 0, or an error code. */
LONG sdt_decode_all(struct sdt_core *c, UBYTE **data, ULONG *size);

/* Frees the queue (not the decoder's state). */
void sdt_free(struct sdt_core *c);

#endif
