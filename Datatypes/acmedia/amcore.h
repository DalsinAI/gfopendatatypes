/*
 * acmedia.datatype's portable bounded-feed core. It owns copies of public
 * FEED buffers until the device takes them, preserves short writes, enforces
 * object generations and per-track parser epochs, and knows no rung or route.
 * MIT. Copyright (c) 2026 AmiCradle Team. SPDX-License-Identifier: MIT
 */
#ifndef AMCORE_H
#define AMCORE_H
#include <exec/types.h>

#define AMD_MAX_TRACKS 4
#define AMD_F_INIT          (1UL << 0)
#define AMD_F_DISCONTINUITY (1UL << 1)
#define AMD_F_END           (1UL << 2)
#define AMD_F_RESET_TRACK   (1UL << 3)
#define AMD_EV_NEED         (1UL << 0)
#define AMD_EV_FORMAT       (1UL << 1)
#define AMD_EV_SEEK         (1UL << 2)
#define AMD_EV_END          (1UL << 3)
#define AMD_E_MEMORY    103L
#define AMD_E_BADARG    114L
#define AMD_E_FULL      221L
#define AMD_E_STALE     222L
#define AMD_E_ENDED     212L

struct amd_env {
    void *(*alloc)(struct amd_env *, ULONG);
    void (*free)(struct amd_env *, void *);
};
struct amd_chunk;
struct amd_core {
    struct amd_env *env;
    struct amd_chunk *head, *tail;
    ULONG bytes, limit, low, high, generation;
    ULONG parser_epoch[AMD_MAX_TRACKS], ended, events, event_track;
    UBYTE below_low, pad[3];
};
struct amd_feed {
    ULONG track, generation, flags, offset_hi, offset_lo;
    LONG start_hi; ULONG start_lo;
    LONG end_hi; ULONG end_lo;
    LONG timestamp_offset_hi; ULONG timestamp_offset_lo;
    LONG append_start_hi; ULONG append_start_lo;
    LONG append_end_hi; ULONG append_end_lo;
    const UBYTE *data;
    ULONG length;
};
/* Control flags are presented only until the first byte is taken. A
 * zero-length AMD_F_END call is the device feed's EOF marker. */
typedef LONG (*amd_write_fn)(void *, ULONG track, ULONG parser_epoch,
                             ULONG flags, const UBYTE *, ULONG);

void amd_init(struct amd_core *, struct amd_env *, ULONG limit, ULONG low, ULONG high);
LONG amd_feed(struct amd_core *, const struct amd_feed *);
LONG amd_pump(struct amd_core *, amd_write_fn, void *);
ULONG amd_seek(struct amd_core *);
LONG amd_reset_track(struct amd_core *, ULONG track);
ULONG amd_remove(struct amd_core *, ULONG track, LONG shi, ULONG slo, LONG ehi, ULONG elo);
ULONG amd_take_events(struct amd_core *, ULONG *track);
void amd_free(struct amd_core *);
#endif
