/*
 * For stream.datatype's subclasses: SDTM_CODEC's message to and from the
 * portable cores' struct sdt_call (sdtcore.h).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#ifndef SDTGLUE_H
#define SDTGLUE_H

#include <datatypes/streamclass.h>
#include "sdtcore.h"

static inline void sdt_call_in(struct sdt_call *c, const struct sdtCodec *m)
{
    c->op = m->sdtc_Op;
    c->flags = m->sdtc_Flags;
    c->in = m->sdtc_In;
    c->in_len = m->sdtc_InLen;
    c->out = m->sdtc_Out;
    c->out_len = m->sdtc_OutLen;
    c->in_used = c->out_made = 0;
    c->error = 0;
}

static inline ULONG sdt_call_out(struct sdtCodec *m, const struct sdt_call *c, LONG rc)
{
    m->sdtc_InUsed = c->in_used;
    m->sdtc_OutMade = c->out_made;
    m->sdtc_Error = c->error;
    return (ULONG)rc;
}

#endif
