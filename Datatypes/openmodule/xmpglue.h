/*
 * gfopenmodule.datatype: the arena allocator (xmpglue.c). A player that may
 * hand its mixing to a cores board core loads the module and starts libxmp
 * with an arena in use, so all of libxmp's state lies in one block of Fast
 * RAM: the one buffer the core's job writes (GFOpenMulticore's rule: a job
 * writes only its written buffers). libxmp allocates nothing while it
 * mixes, only when it loads and starts.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OM_XMPGLUE_H
#define OM_XMPGLUE_H

#include <exec/types.h>

struct om_arena {
    UBYTE *base;
    ULONG size;
    ULONG used;      /* bytes given out, from base */
    ULONG last;      /* where the last block starts */
    ULONG high;      /* the most ever used */
};

BOOL om_arena_init(struct om_arena *a, ULONG size);   /* Fast RAM only */
void om_arena_free(struct om_arena *a);
/* This task's allocations come from a (NULL: AllocVec again). */
void om_arena_use(struct om_arena *a);

#endif
