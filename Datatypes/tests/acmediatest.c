/* Host tests for acmedia.datatype's route-opaque bounded-feed core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amcore.h"

static int cases, fails;
#define CHECK(x) do { cases++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static void *ea(struct amd_env *e, ULONG n) { (void)e; return malloc(n ? n : 1); }
static void ef(struct amd_env *e, void *p) { (void)e; free(p); }
static struct amd_env env = { ea, ef };

struct sink { UBYTE out[256]; ULONG n, max, calls, track, epoch, flags; int fail; };
static LONG wr(void *v, ULONG track, ULONG epoch, ULONG flags, const UBYTE *p, ULONG n)
{
    struct sink *s = v;
    ULONG take = n;
    s->calls++; s->track = track; s->epoch = epoch; s->flags = flags;
    if (s->fail) return -1;
    if (take > s->max) take = s->max;
    if (take) memcpy(s->out + s->n, p, take);
    s->n += take;
    return (LONG)take;
}

static struct amd_feed mkfeed(ULONG track, ULONG gen, ULONG flags, const char *p,
                              ULONG n, ULONG start, ULONG end)
{
    struct amd_feed f;
    memset(&f, 0, sizeof f);
    f.track = track; f.generation = gen; f.flags = flags;
    f.data = (const UBYTE *)p; f.length = n; f.start_lo = start; f.end_lo = end;
    return f;
}

int main(void)
{
    struct amd_core c;
    struct amd_feed f;
    struct sink s;
    ULONG ev, track, gen, ep;
    char a[] = "abcdef", b[] = "GHIJ", z[] = "zzzz";

    memset(&s, 0, sizeof s); s.max = 2;
    amd_init(&c, &env, 12, 4, 10);
    CHECK(c.generation == 1 && c.low == 4 && c.high == 10);

    /* FEED owns a copy; changing caller bytes cannot change output. */
    f = mkfeed(1, 1, AMD_F_INIT, a, 6, 0, 1000);
    CHECK(amd_feed(&c, &f) == 0 && c.bytes == 6);
    memcpy(a, "xxxxxx", 6);
    ev = amd_take_events(&c, &track);
    CHECK((ev & AMD_EV_FORMAT) && track == 1);
    CHECK(amd_pump(&c, wr, &s) == 2 && c.bytes == 4);
    CHECK(s.flags & AMD_F_INIT);
    CHECK(amd_pump(&c, wr, &s) == 2 && c.bytes == 2);
    CHECK(s.flags == 0);
    ev = amd_take_events(&c, &track);
    CHECK((ev & AMD_EV_NEED) && track == 1);
    CHECK(amd_take_events(&c, NULL) == 0); /* one low-water edge */
    CHECK(amd_pump(&c, wr, &s) == 2 && c.bytes == 0);
    CHECK(s.n == 6 && !memcmp(s.out, "abcdef", 6));

    /* High water is transactional. */
    f = mkfeed(1, 1, 0, b, 4, 1000, 2000);
    CHECK(amd_feed(&c, &f) == 0 && c.bytes == 4);
    f = mkfeed(2, 1, 0, z, 4, 0, 100);
    CHECK(amd_feed(&c, &f) == 0 && c.bytes == 8);
    f = mkfeed(2, 1, 0, z, 4, 100, 200);
    CHECK(amd_feed(&c, &f) == AMD_E_FULL && c.bytes == 8);

    /* REMOVE drops overlapping track 1 data and preserves track 2. */
    CHECK(amd_remove(&c, 1, 0, 1500, 0, 2500) == 4 && c.bytes == 4);
    memset(&s, 0, sizeof s); s.max = 4;
    CHECK(amd_pump(&c, wr, &s) == 4 && s.track == 2 && !memcmp(s.out, "zzzz", 4));

    /* Per-track reset changes only that parser epoch. */
    f = mkfeed(2, 1, 0, z, 4, 0, 100);
    CHECK(amd_feed(&c, &f) == 0);
    ep = c.parser_epoch[1];
    CHECK(amd_reset_track(&c, 2) == 0 && c.bytes == 0);
    CHECK(c.parser_epoch[1] != ep && c.parser_epoch[0] == 1);
    ev = amd_take_events(&c, &track);
    CHECK((ev & AMD_EV_FORMAT) && track == 2);

    /* Seek invalidates every old generation without closing the object. */
    f = mkfeed(1, 1, 0, z, 4, 0, 10);
    CHECK(amd_feed(&c, &f) == 0);
    gen = amd_seek(&c);
    CHECK(gen == 2 && c.bytes == 0 && !c.head);
    CHECK(amd_feed(&c, &f) == AMD_E_STALE);
    f.generation = gen;
    CHECK(amd_feed(&c, &f) == 0);
    ev = amd_take_events(&c, &track);
    CHECK((ev & AMD_EV_SEEK) && (ev & AMD_EV_NEED) && track == 0);

    /* Short writes resume exactly where they stopped. */
    memset(&s, 0, sizeof s); s.max = 1;
    while (c.bytes) CHECK(amd_pump(&c, wr, &s) == 1);
    CHECK(s.n == 4 && !memcmp(s.out, "zzzz", 4));

    /* EOF is explicit and terminal, not starvation. */
    f = mkfeed(1, gen, AMD_F_END, NULL, 0, 0, 0);
    CHECK(amd_feed(&c, &f) == 0);
    CHECK(amd_pump(&c, wr, &s) == 0);
    ev = amd_take_events(&c, &track);
    CHECK((ev & AMD_EV_END) && track == 1);
    f = mkfeed(1, gen, 0, z, 1, 0, 1);
    CHECK(amd_feed(&c, &f) == AMD_E_ENDED);

    f = mkfeed(2, gen, AMD_F_END, z, 1, 0, 1);
    CHECK(amd_feed(&c, &f) == AMD_E_BADARG);

    /* A transport failure leaves the private copy queued for policy/retry. */
    f = mkfeed(2, gen, 0, z, 4, 0, 10);
    CHECK(amd_feed(&c, &f) == 0);
    s.fail = 1;
    CHECK(amd_pump(&c, wr, &s) == -1 && c.bytes == 4);
    s.fail = 0; s.max = 4;
    CHECK(amd_pump(&c, wr, &s) == 4 && c.bytes == 0);

    CHECK(amd_feed(&c, NULL) == AMD_E_BADARG);
    f = mkfeed(0, gen, 0, z, 1, 0, 1);
    CHECK(amd_feed(&c, &f) == AMD_E_BADARG);
    amd_free(&c);
    CHECK(!c.head && c.bytes == 0);
    printf("%d/%d acmedia feed cases pass\n", cases - fails, cases);
    return fails ? 1 : 0;
}
