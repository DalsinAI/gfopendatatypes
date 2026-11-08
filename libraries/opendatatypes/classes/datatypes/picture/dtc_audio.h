/* GFOpenDatatypes: how a sound's plan goes to audio.device, double
 * buffered, in plain C with no Amiga headers (dtc_shell.h's player gives
 * it exec and audio.device; tests/m68k/opendatatypes_picsound_test.c a
 * simulated device).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the audio.device autodoc: CMD_WRITE requests to a
 * channel are queued and played one after the other, so a request sent
 * while the one before it still plays starts the moment that one ends.
 * The plan's steps (one segment, or a left and right pair) go out two at
 * a time: while one step plays the next waits behind it, and each time a
 * step ends the one after the next is sent. The channels never run dry
 * between segments (sending a segment only after the one before had
 * ended left a gap at every 131,070-byte segment end).
 */
#ifndef OPENDATATYPES_DTC_AUDIO_H
#define OPENDATATYPES_DTC_AUDIO_H

#include "dtc_env.h"

/* What the player is given. A slot is one IOAudio: 0 and 1 a step's (its
 * segment, or its pair), 2 and 3 the next step's. */
struct dtc_audio_io {
    void *ctx;
    /* CMD_WRITE of segment g from slot (BeginIO; the slot's request is free) */
    void (*begin)(void *ctx, odt_u32 slot, const struct dtc_audio_seg *g);
    /* until slot's request is done: 1; or asked to stop first: 0 (the
     * request is still out) */
    int (*wait)(void *ctx, odt_u32 slot);
    /* AbortIO and WaitIO of slot's request */
    void (*abort)(void *ctx, odt_u32 slot);
};

/* Where the plan is: repeat (pass) rep, segment s */
struct dtc_audio_at { odt_u32 rep, s; };

/* The next step: its first segment and how many (1, or 2 for a left and
 * right pair); 0 when the plan is done */
static inline int dtc_audio_next(const struct dtc_audio_plan *plan, struct dtc_audio_at *at, odt_u32 *first,
                                 odt_u32 *n)
{
    if (!plan->nseg) return 0;
    if (at->s >= plan->nseg) {
        at->s = 0;
        at->rep++;
    }
    if (plan->repeat && at->rep >= plan->repeat) return 0;
    *first = at->s;
    *n = (at->s + 1 < plan->nseg && plan->seg[at->s].roles == 1 && plan->seg[at->s + 1].roles == 2) ? 2 : 1;
    at->s += *n;
    return 1;
}

/* Plays the plan: 1 when it ran to its end, 0 when asked to stop (every
 * request then aborted and back) */
static inline int dtc_audio_run(const struct dtc_audio_plan *plan, const struct dtc_audio_io *io)
{
    struct dtc_audio_at at = { 0, 0 };
    odt_u32 first[2], cnt[2], head = 0, out = 0, k, q;
    /* the first two steps: one plays, one waits behind it */
    while (out < 2 && dtc_audio_next(plan, &at, &first[(head + out) & 1], &cnt[(head + out) & 1])) {
        q = (head + out) & 1;
        for (k = 0; k < cnt[q]; k++) io->begin(io->ctx, 2 * q + k, &plan->seg[first[q] + k]);
        out++;
    }
    while (out) {
        q = head;
        for (k = 0; k < cnt[q]; k++)
            if (!io->wait(io->ctx, 2 * q + k)) {
                /* stopped: this step's requests not yet back, and the next's */
                for (; k < cnt[q]; k++) io->abort(io->ctx, 2 * q + k);
                if (out == 2)
                    for (k = 0; k < cnt[q ^ 1]; k++) io->abort(io->ctx, 2 * (q ^ 1) + k);
                return 0;
            }
        head ^= 1;
        out--;
        /* the step after the one now playing, behind it */
        if (dtc_audio_next(plan, &at, &first[q], &cnt[q])) {
            for (k = 0; k < cnt[q]; k++) io->begin(io->ctx, 2 * q + k, &plan->seg[first[q] + k]);
            out++;
        }
    }
    return 1;
}

#endif
