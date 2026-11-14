/* acmedia.datatype's portable bounded-feed core (amcore.h). */
#include "amcore.h"

struct amd_chunk {
    struct amd_chunk *next;
    ULONG track, parser_epoch, flags, length, sent, control_sent;
    LONG start_hi; ULONG start_lo;
    LONG end_hi; ULONG end_lo;
    UBYTE data[1];
};

static int track_ok(ULONG t) { return t > 0 && t <= AMD_MAX_TRACKS; }
static void copy(UBYTE *d, const UBYTE *s, ULONG n) { while (n--) *d++ = *s++; }

static void low_edge(struct amd_core *c, ULONG track)
{
    UBYTE below = c->bytes < c->low;
    if (below && !c->below_low) {
        c->events |= AMD_EV_NEED;
        c->event_track = track;
    }
    c->below_low = below;
}

void amd_init(struct amd_core *c, struct amd_env *env, ULONG limit, ULONG low, ULONG high)
{
    ULONG i;
    c->env = env; c->head = c->tail = 0; c->bytes = 0;
    c->limit = limit ? limit : 8UL * 1024 * 1024;
    c->high = high && high <= c->limit ? high : c->limit;
    c->low = low < c->high ? low : c->high / 2;
    c->generation = 1;
    for (i = 0; i < AMD_MAX_TRACKS; i++) c->parser_epoch[i] = 1;
    c->ended = c->events = c->event_track = 0;
    c->below_low = 1; c->pad[0] = c->pad[1] = c->pad[2] = 0;
}

static void unlink_free(struct amd_core *c, struct amd_chunk *prev, struct amd_chunk *q)
{
    ULONG left = q->length - q->sent;
    if (prev) prev->next = q->next; else c->head = q->next;
    if (c->tail == q) c->tail = prev;
    c->bytes = left <= c->bytes ? c->bytes - left : 0;
    c->env->free(c->env, q);
}

LONG amd_reset_track(struct amd_core *c, ULONG track)
{
    struct amd_chunk *q, *next, *prev = 0;
    if (!track_ok(track)) return AMD_E_BADARG;
    for (q = c->head; q; q = next) {
        next = q->next;
        if (q->track == track) unlink_free(c, prev, q); else prev = q;
    }
    c->ended &= ~(1UL << (track - 1));
    if (++c->parser_epoch[track - 1] == 0) c->parser_epoch[track - 1] = 1;
    c->events |= AMD_EV_FORMAT; c->event_track = track;
    low_edge(c, track);
    return 0;
}

LONG amd_feed(struct amd_core *c, const struct amd_feed *f)
{
    struct amd_chunk *q;
    ULONG need;
    if (!f || !track_ok(f->track) || (!f->data && f->length)) return AMD_E_BADARG;
    if (f->generation != c->generation) return AMD_E_STALE;
    if (f->flags & AMD_F_RESET_TRACK) {
        LONG e = amd_reset_track(c, f->track);
        if (e) return e;
    }
    if (c->ended & (1UL << (f->track - 1))) return AMD_E_ENDED;
    if ((f->flags & AMD_F_END) && f->length) return AMD_E_BADARG;
    if (!f->length && !(f->flags & (AMD_F_END | AMD_F_INIT | AMD_F_DISCONTINUITY | AMD_F_RESET_TRACK))) return 0;
    if (f->length > c->high || c->bytes > c->high - f->length) return AMD_E_FULL;
    if (f->length > 0xffffffffUL - (ULONG)sizeof(*q) + 1) return AMD_E_MEMORY;
    need = (ULONG)sizeof(*q) - 1 + f->length;
    if (!(q = c->env->alloc(c->env, need))) return AMD_E_MEMORY;
    q->next = 0; q->track = f->track;
    q->parser_epoch = c->parser_epoch[f->track - 1]; q->flags = f->flags;
    q->length = f->length; q->sent = q->control_sent = 0;
    q->start_hi = f->start_hi; q->start_lo = f->start_lo;
    q->end_hi = f->end_hi; q->end_lo = f->end_lo;
    if (f->length) copy(q->data, f->data, f->length);
    if (c->tail) c->tail->next = q; else c->head = q;
    c->tail = q; c->bytes += f->length; c->below_low = c->bytes < c->low;
    if (f->flags & AMD_F_INIT) { c->events |= AMD_EV_FORMAT; c->event_track = f->track; }
    if (f->flags & AMD_F_END) c->ended |= 1UL << (f->track - 1);
    return 0;
}

LONG amd_pump(struct amd_core *c, amd_write_fn write, void *ctx)
{
    struct amd_chunk *q = c->head;
    ULONG left, track, flags;
    LONG n;
    if (!q || !write) return 0;
    left = q->length - q->sent;
    n = write(ctx, q->track, q->parser_epoch, q->control_sent ? 0 : q->flags,
              left ? q->data + q->sent : 0, left);
    if (n < 0 || (ULONG)n > left) return -1;
    q->sent += (ULONG)n; c->bytes -= (ULONG)n;
    if (n > 0 || !left) q->control_sent = 1;
    track = q->track; flags = q->flags;
    if (!left || q->sent == q->length) {
        unlink_free(c, 0, q);
        if (flags & AMD_F_END) { c->events |= AMD_EV_END; c->event_track = track; }
    }
    low_edge(c, track);
    return n;
}

ULONG amd_seek(struct amd_core *c)
{
    struct amd_chunk *q, *next;
    ULONG i;
    for (q = c->head; q; q = next) { next = q->next; c->env->free(c->env, q); }
    c->head = c->tail = 0; c->bytes = c->ended = 0;
    if (++c->generation == 0) c->generation = 1;
    for (i = 0; i < AMD_MAX_TRACKS; i++) if (++c->parser_epoch[i] == 0) c->parser_epoch[i] = 1;
    c->events |= AMD_EV_SEEK | AMD_EV_NEED; c->event_track = 0; c->below_low = 1;
    return c->generation;
}

static int before(LONG ahi, ULONG alo, LONG bhi, ULONG blo)
{
    return ahi != bhi ? ahi < bhi : alo < blo;
}

ULONG amd_remove(struct amd_core *c, ULONG track, LONG shi, ULONG slo, LONG ehi, ULONG elo)
{
    struct amd_chunk *q, *next, *prev = 0;
    ULONG removed = 0;
    if (!track_ok(track) || !before(shi, slo, ehi, elo)) return 0;
    for (q = c->head; q; q = next) {
        int overlap;
        next = q->next;
        overlap = q->track == track && before(q->start_hi, q->start_lo, ehi, elo)
               && before(shi, slo, q->end_hi, q->end_lo);
        if (overlap) { removed += q->length - q->sent; unlink_free(c, prev, q); }
        else prev = q;
    }
    low_edge(c, track);
    return removed;
}

ULONG amd_take_events(struct amd_core *c, ULONG *track)
{
    ULONG e = c->events;
    if (track) *track = c->event_track;
    c->events = 0;
    return e;
}

void amd_free(struct amd_core *c)
{
    struct amd_chunk *q, *next;
    for (q = c->head; q; q = next) { next = q->next; c->env->free(c->env, q); }
    c->head = c->tail = 0; c->bytes = 0;
}
