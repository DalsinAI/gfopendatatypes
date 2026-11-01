/*
 * brotli.datatype's decoder: Brotli (RFC 7932), written from the RFC alone.
 *
 * Streaming: the coded bytes may come in pieces of any size, down to one
 * byte, and the plain bytes go out into whatever room the caller gives.
 * Integer only. No OS calls and no static data but the constant tables
 * (brdict.c, written by mkbrdict.py from the RFC's Appendices A and B, and
 * the context tables in brdecode.c): memory comes from the caller's
 * struct sdt_env, so it can run as a job on another core later.
 *
 * Memory: the decoder's state (about 14 KB), the window (it grows with the
 * output, up to the 2^WBITS the stream asks for: 64 KB to 16 MB) and each
 * meta-block's prefix codes and context maps.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef BRDECODE_H
#define BRDECODE_H

#include <exec/types.h>
#include "sdtcore.h"

/* RFC 7932's static dictionary and transforms (brdict.c) */
extern const UBYTE br_dict[122784];
extern const UBYTE br_xf_pool[];         /* strings, each after its length */
extern const UBYTE br_xf_prefix[121];    /* offsets into the pool */
extern const UBYTE br_xf_kind[121];      /* 0 Identity, 1-9 OmitLast, 10 UppercaseFirst,
                                            11 UppercaseAll, 12-20 OmitFirst1-9 */
extern const UBYTE br_xf_suffix[121];

struct br_state;

/* A new decoder, or NULL when memory runs out. */
struct br_state *br_new(struct sdt_env *env);

/* One SDTM_CODEC run (sdtcore.h): SDT_MORE, SDT_DONE or SDT_ERROR. */
LONG br_run(struct br_state *s, struct sdt_call *c);

void br_free(struct br_state *s);

#endif
