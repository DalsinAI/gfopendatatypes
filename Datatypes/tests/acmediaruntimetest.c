/* Deterministic host tests for acmedia.datatype's route-opaque runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amruntime.h"

static int cases, fails, allocations, frees;
#define CHECK(x) do { cases++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void *ea(struct amd_env *e, ULONG n)
{
    void *p;
    (void)e; p = malloc(n ? n : 1); if (p) allocations++; return p;
}
static void ef(struct amd_env *e, void *p)
{
    (void)e; if (p) { frees++; free(p); }
}
static struct amd_env env = { ea, ef };

struct device {
    UBYTE out[256];
    char mime[256];
    ULONG n, max, open_calls, add_calls, write_calls, accepted_calls, seek_calls;
    ULONG remove_calls, close_calls, generation, track, epoch;
    ULONG first_flags, later_flags, track_flags;
    struct amdtTime seek_time, remove_start, remove_end;
    int blocked;
};

static LONG dev_open(void *v, ULONG generation)
{
    struct device *d = v;
    d->open_calls++; d->generation = generation; return 0;
}
static LONG dev_add(void *v, ULONG track, CONST_STRPTR mime, ULONG flags)
{
    struct device *d = v;
    d->add_calls++; d->track = track; d->track_flags = flags;
    strcpy(d->mime, mime); return 0;
}
static LONG dev_write(void *v, ULONG track, ULONG epoch, ULONG flags,
                      const UBYTE *p, ULONG n)
{
    struct device *d = v;
    ULONG take = n;
    d->write_calls++; d->track = track; d->epoch = epoch;
    if (d->blocked) return 0;
    if (take > d->max) take = d->max;
    if (take || !n) {
        if (!d->accepted_calls) d->first_flags = flags;
        else d->later_flags |= flags;
        d->accepted_calls++;
    }
    if (take) memcpy(d->out + d->n, p, take);
    d->n += take;
    return (LONG)take;
}
static LONG dev_seek(void *v, ULONG generation, struct amdtTime time, ULONG flags)
{
    struct device *d = v;
    d->seek_calls++; d->generation = generation; d->seek_time = time;
    d->first_flags = flags; return 0;
}
static LONG dev_remove(void *v, ULONG track, struct amdtTime start,
                       struct amdtTime end)
{
    struct device *d = v;
    d->remove_calls++; d->track = track;
    d->remove_start = start; d->remove_end = end; return 0;
}
static void dev_close(void *v) { ((struct device *)v)->close_calls++; }

static const struct amr_device_ops ops = {
    dev_open, dev_add, dev_write, dev_seek, dev_remove, dev_close
};

static struct amdtFeed feed(ULONG track, ULONG generation, ULONG flags,
                            const void *data, ULONG length, ULONG start,
                            ULONG end)
{
    struct amdtFeed f;
    memset(&f, 0, sizeof f); f.MethodID = AMDTM_FEED;
    f.amf_Track = track; f.amf_Generation = generation;
    f.amf_Flags = flags; f.amf_Data = data; f.amf_Length = length;
    f.amf_Start.lo = start; f.amf_End.lo = end; return f;
}

int main(void)
{
    struct amr_runtime r;
    struct device d;
    struct amdtAddTrack add;
    struct amdtFeed f;
    struct amdtRemove rem;
    struct amdtSeek seek;
    struct amdtEnd end;
    struct amdtEvent event;
    char mime[] = "video/webm; codecs=vp9", bytes[] = "abcdef";
    ULONG generation;

    memset(&r, 0xa5, sizeof r); memset(&d, 0, sizeof d); d.max = 2;
    CHECK(amr_open(&r, &env, &ops, &d, 12, 4, 10) == 0);
    CHECK(d.open_calls == 1 && d.generation == 1 && amr_generation(&r) == 1);

    memset(&add, 0, sizeof add); add.MethodID = AMDTM_ADDTRACK;
    add.amat_Mime = mime; add.amat_Flags = AMDT_TRACK_VIDEO;
    CHECK(amr_add_track(&r, &add) == 1);
    strcpy(mime, "changed");
    CHECK(!strcmp((const char *)r.mime[0], "video/webm; codecs=vp9"));
    CHECK(d.add_calls == 1 && d.track == 1 && d.track_flags == AMDT_TRACK_VIDEO);

    /* FEED bytes are copied while the device applies backpressure. */
    d.blocked = 1;
    f = feed(1, 1, AMDT_FEED_INIT, bytes, 6, 0, 1000);
    CHECK(amr_feed(&r, &f) == 0 && amr_buffered(&r) == 6);
    memcpy(bytes, "xxxxxx", 6); d.blocked = 0;
    CHECK(amr_pump(&r) == 0 && amr_buffered(&r) == 0);
    CHECK(d.n == 6 && !memcmp(d.out, "abcdef", 6));
    CHECK((d.first_flags & AMDT_FEED_INIT) && d.later_flags == 0);
    CHECK((amr_take_event(&r, &event) & AMDTE_FORMAT) != 0);
    CHECK(event.ame_Track == 1 && event.ame_Generation == 1);

    /* Tracks must be registered; bounded admission remains transactional. */
    f = feed(2, 1, 0, "x", 1, 0, 1);
    CHECK(amr_feed(&r, &f) == AMD_E_BADARG);
    f = feed(1, 1, 1UL << 31, "x", 1, 0, 1);
    CHECK(amr_feed(&r, &f) == AMD_E_BADARG);
    d.blocked = 1;
    f = feed(1, 1, 0, "12345678", 8, 1000, 2000);
    CHECK(amr_feed(&r, &f) == 0 && amr_buffered(&r) == 8);
    f = feed(1, 1, 0, "3456", 4, 2000, 3000);
    CHECK(amr_feed(&r, &f) == AMD_E_FULL && amr_buffered(&r) == 8);

    /* REMOVE drops private overlap and forwards the same range to device. */
    memset(&rem, 0, sizeof rem); rem.MethodID = AMDTM_REMOVE; rem.amr_Track = 1;
    rem.amr_Start.lo = 500; rem.amr_End.lo = 2500;
    CHECK(amr_remove(&r, &rem) == 0 && amr_buffered(&r) == 0);
    CHECK(d.remove_calls == 1 && d.remove_start.lo == 500 && d.remove_end.lo == 2500);

    /* SEEK invalidates old FEED generations but is not yet SEEKED proof. */
    f = feed(1, 1, 0, "old", 3, 0, 10);
    CHECK(amr_feed(&r, &f) == 0 && amr_buffered(&r) == 3);
    (void)amr_take_event(&r, 0);
    memset(&seek, 0, sizeof seek); seek.MethodID = AMDTM_SEEK;
    seek.ams_Time.hi = -1; seek.ams_Time.lo = 0xffffff00UL;
    seek.ams_Flags = AMDT_SEEK_EXACT;
    CHECK(amr_seek(&r, &seek) == 0);
    generation = amr_generation(&r);
    CHECK(generation == 2 && d.seek_calls == 1 && d.generation == generation);
    CHECK(d.seek_time.hi == -1 && d.seek_time.lo == 0xffffff00UL);
    CHECK(amr_buffered(&r) == 0);
    CHECK(amr_feed(&r, &f) == AMD_E_STALE);
    CHECK((amr_take_event(&r, &event) & AMDTE_SEEKED) == 0);
    CHECK((event.ame_Events & AMDTE_NEED) != 0 && event.ame_Generation == 2);

    /* END is explicit, forwarded once and reported separately from starvation. */
    d.blocked = 0;
    memset(&end, 0, sizeof end); end.MethodID = AMDTM_END;
    end.ame_Track = 1; end.ame_Generation = generation;
    CHECK(amr_end(&r, &end) == 0 && amr_buffered(&r) == 0);
    CHECK((amr_take_event(&r, &event) & AMDTE_END) != 0 && event.ame_Track == 1);
    f = feed(1, generation, 0, "late", 4, 0, 1);
    CHECK(amr_feed(&r, &f) == AMD_E_ENDED);

    amr_close(&r);
    CHECK(d.close_calls == 1 && allocations == frees);
    CHECK(amr_generation(&r) == 0 && amr_pump(&r) == AMD_E_BADARG);
    amr_close(&r);
    CHECK(d.close_calls == 1);

    add.MethodID = 0;
    CHECK(amr_add_track(&r, &add) == AMD_E_BADARG);
    printf("%d/%d acmedia runtime cases pass\n", cases - fails, cases);
    return fails ? 1 : 0;
}
