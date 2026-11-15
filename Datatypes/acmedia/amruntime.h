/*
 * Portable object lifetime and public-message adapter for acmedia.datatype.
 * The device callbacks are deliberately route-opaque: the datatype supplies
 * tracks and bytes; akikoplus.device / ACMedia chooses the implementation
 * rung and output routes.
 * MIT. Copyright (c) 2026 AmiCradle Team. SPDX-License-Identifier: MIT
 */
#ifndef AMRUNTIME_H
#define AMRUNTIME_H

#include <datatypes/acmediaclass.h>
#include "amcore.h"

#define AMR_MIME_MAX 255

struct amr_device_ops {
    LONG (*open)(void *, ULONG generation);
    LONG (*add_track)(void *, ULONG track, CONST_STRPTR mime, ULONG flags);
    amd_write_fn write;
    LONG (*seek)(void *, ULONG generation, struct amdtTime time, ULONG flags);
    LONG (*remove)(void *, ULONG track, struct amdtTime start,
                   struct amdtTime end);
    void (*close)(void *);
};

struct amr_runtime {
    struct amd_core core;
    struct amd_env *env;
    const struct amr_device_ops *ops;
    void *device;
    UBYTE *mime[AMD_MAX_TRACKS];
    ULONG track_flags[AMD_MAX_TRACKS];
    UBYTE opened, pad[3];
};

LONG amr_open(struct amr_runtime *, struct amd_env *,
              const struct amr_device_ops *, void *,
              ULONG limit, ULONG low, ULONG high);
LONG amr_add_track(struct amr_runtime *, const struct amdtAddTrack *);
LONG amr_feed(struct amr_runtime *, const struct amdtFeed *);
LONG amr_pump(struct amr_runtime *);
LONG amr_seek(struct amr_runtime *, const struct amdtSeek *);
LONG amr_remove(struct amr_runtime *, const struct amdtRemove *);
LONG amr_end(struct amr_runtime *, const struct amdtEnd *);
ULONG amr_take_event(struct amr_runtime *, struct amdtEvent *);
ULONG amr_generation(const struct amr_runtime *);
ULONG amr_buffered(const struct amr_runtime *);
void amr_close(struct amr_runtime *);

#endif
