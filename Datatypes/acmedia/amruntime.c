/* acmedia.datatype portable object runtime (amruntime.h). */
#include "amruntime.h"

static int track_ok(const struct amr_runtime *r, ULONG track)
{
    return r && track > 0 && track <= AMD_MAX_TRACKS && r->mime[track - 1];
}

static int time_before(struct amdtTime a, struct amdtTime b)
{
    return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo;
}

static void clear_runtime(struct amr_runtime *r)
{
    ULONG i;
    r->env = 0; r->ops = 0; r->device = 0;
    for (i = 0; i < AMD_MAX_TRACKS; i++) {
        r->mime[i] = 0; r->track_flags[i] = 0;
    }
    r->opened = 0; r->pad[0] = r->pad[1] = r->pad[2] = 0;
}

LONG amr_open(struct amr_runtime *r, struct amd_env *env,
              const struct amr_device_ops *ops, void *device,
              ULONG limit, ULONG low, ULONG high)
{
    LONG e;
    if (!r || !env || !env->alloc || !env->free || !ops || !ops->write)
        return AMD_E_BADARG;
    clear_runtime(r);
    r->env = env; r->ops = ops; r->device = device;
    amd_init(&r->core, env, limit, low, high);
    e = ops->open ? ops->open(device, r->core.generation) : 0;
    if (e) {
        amd_free(&r->core);
        clear_runtime(r);
        return e;
    }
    r->opened = 1;
    return 0;
}

LONG amr_add_track(struct amr_runtime *r, const struct amdtAddTrack *m)
{
    ULONG i, n;
    UBYTE *copy;
    LONG e;
    if (!r || !r->opened || !m || m->MethodID != AMDTM_ADDTRACK ||
        !m->amat_Mime || !(m->amat_Flags & (AMDT_TRACK_VIDEO | AMDT_TRACK_AUDIO)) ||
        (m->amat_Flags & ~(AMDT_TRACK_VIDEO | AMDT_TRACK_AUDIO)))
        return AMD_E_BADARG;
    for (i = 0; i < AMD_MAX_TRACKS && r->mime[i]; i++) {}
    if (i == AMD_MAX_TRACKS) return AMD_E_FULL;
    for (n = 0; n <= AMR_MIME_MAX && m->amat_Mime[n]; n++) {}
    if (!n || n > AMR_MIME_MAX) return AMD_E_BADARG;
    copy = r->env->alloc(r->env, n + 1);
    if (!copy) return AMD_E_MEMORY;
    for (n = 0; m->amat_Mime[n]; n++) copy[n] = m->amat_Mime[n];
    copy[n] = 0;
    e = r->ops->add_track ?
        r->ops->add_track(r->device, i + 1, (CONST_STRPTR)copy,
                          m->amat_Flags) : 0;
    if (e) { r->env->free(r->env, copy); return e; }
    r->mime[i] = copy; r->track_flags[i] = m->amat_Flags;
    return (LONG)(i + 1);
}

LONG amr_pump(struct amr_runtime *r)
{
    struct amd_chunk *before;
    LONG n;
    if (!r || !r->opened) return AMD_E_BADARG;
    while (r->core.head) {
        before = r->core.head;
        n = amd_pump(&r->core, r->ops->write, r->device);
        if (n < 0) return n;
        if (!n && r->core.head == before) break;
    }
    return 0;
}

LONG amr_feed(struct amr_runtime *r, const struct amdtFeed *m)
{
    struct amd_feed f;
    LONG e;
    if (!r || !r->opened || !m || m->MethodID != AMDTM_FEED ||
        !track_ok(r, m->amf_Track) ||
        (m->amf_Flags & ~(AMDT_FEED_INIT | AMDT_FEED_DISCONTINUITY |
                          AMDT_FEED_END | AMDT_FEED_RESET_TRACK)))
        return AMD_E_BADARG;
    f.track = m->amf_Track; f.generation = m->amf_Generation;
    f.flags = m->amf_Flags; f.offset_hi = m->amf_OffsetHi;
    f.offset_lo = m->amf_OffsetLo;
    f.start_hi = m->amf_Start.hi; f.start_lo = m->amf_Start.lo;
    f.end_hi = m->amf_End.hi; f.end_lo = m->amf_End.lo;
    f.timestamp_offset_hi = m->amf_TimestampOffset.hi;
    f.timestamp_offset_lo = m->amf_TimestampOffset.lo;
    f.append_start_hi = m->amf_AppendStart.hi;
    f.append_start_lo = m->amf_AppendStart.lo;
    f.append_end_hi = m->amf_AppendEnd.hi;
    f.append_end_lo = m->amf_AppendEnd.lo;
    f.data = (const UBYTE *)m->amf_Data; f.length = m->amf_Length;
    e = amd_feed(&r->core, &f);
    return e ? e : amr_pump(r);
}

LONG amr_seek(struct amr_runtime *r, const struct amdtSeek *m)
{
    ULONG generation;
    if (!r || !r->opened || !m || m->MethodID != AMDTM_SEEK ||
        (m->ams_Flags & ~AMDT_SEEK_EXACT)) return AMD_E_BADARG;
    generation = amd_seek(&r->core);
    return r->ops->seek ?
        r->ops->seek(r->device, generation, m->ams_Time, m->ams_Flags) : 0;
}

LONG amr_remove(struct amr_runtime *r, const struct amdtRemove *m)
{
    if (!r || !r->opened || !m || m->MethodID != AMDTM_REMOVE ||
        !track_ok(r, m->amr_Track) || !time_before(m->amr_Start, m->amr_End))
        return AMD_E_BADARG;
    (void)amd_remove(&r->core, m->amr_Track, m->amr_Start.hi,
                     m->amr_Start.lo, m->amr_End.hi, m->amr_End.lo);
    return r->ops->remove ?
        r->ops->remove(r->device, m->amr_Track, m->amr_Start, m->amr_End) : 0;
}

LONG amr_end(struct amr_runtime *r, const struct amdtEnd *m)
{
    struct amdtFeed f;
    if (!r || !r->opened || !m || m->MethodID != AMDTM_END ||
        !track_ok(r, m->ame_Track)) return AMD_E_BADARG;
    f.MethodID = AMDTM_FEED; f.amf_GInfo = m->ame_GInfo;
    f.amf_Track = m->ame_Track; f.amf_Generation = m->ame_Generation;
    f.amf_Data = 0; f.amf_Length = 0; f.amf_Flags = AMDT_FEED_END;
    f.amf_OffsetHi = f.amf_OffsetLo = 0;
    f.amf_Start.hi = f.amf_End.hi = f.amf_TimestampOffset.hi = 0;
    f.amf_AppendStart.hi = f.amf_AppendEnd.hi = 0;
    f.amf_Start.lo = f.amf_End.lo = f.amf_TimestampOffset.lo = 0;
    f.amf_AppendStart.lo = f.amf_AppendEnd.lo = 0;
    return amr_feed(r, &f);
}

ULONG amr_take_event(struct amr_runtime *r, struct amdtEvent *event)
{
    ULONG core_events, track, public_events = 0;
    if (!r || !r->opened) return 0;
    core_events = amd_take_events(&r->core, &track);
    if (core_events & AMD_EV_NEED) public_events |= AMDTE_NEED;
    if (core_events & AMD_EV_FORMAT) public_events |= AMDTE_FORMAT;
    if (core_events & AMD_EV_END) public_events |= AMDTE_END;
    /* AMD_EV_SEEK is cancellation, not proof of a decodable post-seek item. */
    if (event) {
        event->MethodID = 0; event->ame_Events = public_events;
        event->ame_Track = track; event->ame_Generation = r->core.generation;
        event->ame_Error = 0;
    }
    return public_events;
}

ULONG amr_generation(const struct amr_runtime *r)
{
    return r && r->opened ? r->core.generation : 0;
}

ULONG amr_buffered(const struct amr_runtime *r)
{
    return r && r->opened ? r->core.bytes : 0;
}

void amr_close(struct amr_runtime *r)
{
    ULONG i;
    if (!r || !r->opened) return;
    if (r->ops->close) r->ops->close(r->device);
    amd_free(&r->core);
    for (i = 0; i < AMD_MAX_TRACKS; i++)
        if (r->mime[i]) r->env->free(r->env, r->mime[i]);
    clear_runtime(r);
}
