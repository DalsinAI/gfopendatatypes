/*
 * webm.datatype's reader through its read cache (webm/webm_readcache.h),
 * on the host: every file is read twice, straight from the source (as
 * before the cache) and through the cache, and the two must give the same
 * answer, the same picture size, codec, audio track count and frames. The
 * same goes for every file cut short and with bytes changed. The source
 * counts its calls: through the cache, the reader may call it at most once
 * for each WEBM_CACHE_SIZE bytes of the file plus once per frame bigger
 * than the cache (a frame it skips can move it past the cache).
 *
 *   webmreadtest [-time N] [-audio N] file.webm...
 *
 * -audio N: the next file must have N audio tracks. -time N: read each
 * file N times from a real file descriptor (lseek and read, as Seek and
 * Read on AmigaDOS) both ways and print the time and the calls.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "webm_demux.h"
#include "webm_readcache.h"

typedef struct {
    const unsigned char *data;
    uint32_t size;
    unsigned long calls;
} Mem;

static long memRead(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    Mem *m = handle;
    m->calls++;
    if (offset > m->size)
        return 0;
    if (length > m->size - offset)
        length = m->size - offset;
    memcpy(buffer, m->data + offset, length);
    return (long)length;
}

typedef struct {
    int fd;
    unsigned long calls;
} Fd;

static long fdRead(void *handle, uint32_t offset, void *buffer, uint32_t length)
{
    Fd *f = handle;
    f->calls++;
    if (lseek(f->fd, offset, SEEK_SET) < 0)
        return -1;
    return (long)read(f->fd, buffer, length);
}

static int failures;

static int same(const char *what, int rcA, const WebMInfo *a, int rcB, const WebMInfo *b)
{
    if (rcA != rcB || (!rcA && (a->codec != b->codec || a->width != b->width || a->height != b->height
        || a->frameDurationNs != b->frameDurationNs || a->durationMs != b->durationMs
        || a->frameCount != b->frameCount || a->audioTracks != b->audioTracks
        || memcmp(a->frames, b->frames, a->frameCount * sizeof(WebMFrame))))) {
        printf("FAIL %s: straight %d, cached %d\n", what, rcA, rcB);
        failures++;
        return 0;
    }
    return 1;
}

/* Both ways on bytes in memory; the calls the cached way made, or -1 */
static long both(const char *what, const unsigned char *data, uint32_t size, WebMInfo *out, int *rcOut)
{
    Mem a = { data, size, 0 }, b = { data, size, 0 };
    static WebMReadCache cache;
    WebMInfo ia, ib;
    int ra, rb;
    ra = webm_parse(memRead, &a, size, &ia);
    webm_cache_init(&cache, memRead, &b);
    rb = webm_parse(webm_cache_read, &cache, size, &ib);
    if (!same(what, ra, &ia, rb, &ib)) {
        if (!ra) webm_free(&ia);
        if (!rb) webm_free(&ib);
        return -1;
    }
    if (!rb && out) {
        uint32_t i, big = 0;
        for (i = 0; i < ib.frameCount; i++)
            if (ib.frames[i].size > WEBM_CACHE_SIZE)
                big++;
        /* every block of every track may jump past the cache; the video
         * track's big frames are a floor for that, so allow twice as many */
        if (b.calls > size / WEBM_CACHE_SIZE + 2 * big + 16) {
            printf("FAIL %s: %lu calls through the cache for %lu bytes (%lu big frames)\n", what, b.calls,
                (unsigned long)size, (unsigned long)big);
            failures++;
        }
        printf("     %s: %lu bytes, %lu frames, %lu audio track(s), source calls %lu straight, %lu cached\n", what,
            (unsigned long)size, (unsigned long)ib.frameCount, (unsigned long)ib.audioTracks, a.calls, b.calls);
        *out = ib;
    } else if (!rb)
        webm_free(&ib);
    if (!ra)
        webm_free(&ia);
    if (rcOut)
        *rcOut = rb;
    return (long)b.calls;
}

static unsigned long rng = 12345;
static unsigned long next(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 8) & 0xffffff;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int timeRuns = 0, i;
    long wantAudio = -1;
    for (i = 1; i < argc; i++) {
        FILE *f;
        unsigned char *data, *copy;
        long size;
        WebMInfo info;
        int rc = -99, k;
        char what[64];
        if (!strcmp(argv[i], "-time") && i + 1 < argc) {
            timeRuns = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "-audio") && i + 1 < argc) {
            wantAudio = atol(argv[++i]);
            continue;
        }
        if (!(f = fopen(argv[i], "rb"))) {
            printf("FAIL cannot open %s\n", argv[i]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc(size ? size : 1);
        copy = malloc(size ? size : 1);
        if (!data || !copy || fread(data, 1, size, f) != (size_t)size)
            return 1;
        fclose(f);
        memset(&info, 0, sizeof info);
        if (both(argv[i], data, (uint32_t)size, &info, &rc) < 0 || rc) {
            printf("FAIL %s: not read (%d)\n", argv[i], rc);
            failures++;
        } else {
            if (wantAudio >= 0 && info.audioTracks != (uint32_t)wantAudio) {
                printf("FAIL %s: %lu audio tracks, not %ld\n", argv[i], (unsigned long)info.audioTracks, wantAudio);
                failures++;
            }
            webm_free(&info);
        }
        wantAudio = -1;
        /* cut short, and with bytes changed */
        for (k = 0; k < 400; k++) {
            uint32_t n = (uint32_t)(next() % (unsigned long)(size + 1)), j, flips;
            memcpy(copy, data, size);
            snprintf(what, sizeof what, "%s cut at %lu", argv[i], (unsigned long)n);
            both(what, copy, n, NULL, NULL);
            flips = 1 + next() % 8;
            for (j = 0; j < flips && size; j++)
                copy[next() % (unsigned long)size] = (unsigned char)next();
            snprintf(what, sizeof what, "%s changed (%d)", argv[i], k);
            both(what, copy, (uint32_t)size, NULL, NULL);
        }
        if (timeRuns) {
            Fd a = { open(argv[i], O_RDONLY), 0 }, b = { open(argv[i], O_RDONLY), 0 };
            static WebMReadCache cache;
            double t0, t1, t2;
            int r;
            if (a.fd < 0 || b.fd < 0)
                return 1;
            t0 = now();
            for (r = 0; r < timeRuns; r++)
                if (!webm_parse(fdRead, &a, (uint32_t)size, &info))
                    webm_free(&info);
            t1 = now();
            for (r = 0; r < timeRuns; r++) {
                webm_cache_init(&cache, fdRead, &b);
                if (!webm_parse(webm_cache_read, &cache, (uint32_t)size, &info))
                    webm_free(&info);
            }
            t2 = now();
            printf("TIME %s x%d: straight %.1f ms (%lu lseek+read), cached %.1f ms (%lu lseek+read)\n", argv[i],
                timeRuns, (t1 - t0) * 1000, a.calls, (t2 - t1) * 1000, b.calls);
            close(a.fd);
            close(b.fd);
        }
        free(data);
        free(copy);
    }
    if (failures) {
        printf("FAIL: %d\n", failures);
        return 1;
    }
    printf("ok   webm reader: the cache gives the straight reads' answers\n");
    return 0;
}
