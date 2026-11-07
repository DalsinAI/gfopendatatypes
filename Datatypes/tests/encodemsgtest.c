/*
 * common/encodemsg.c on a host: media.encode/1's settings and info as
 * bytes, the file a datatype picks to write, and a sound.datatype object's
 * samples in every layout as the 16-bit big-endian PCM the service takes.
 * With an OUTDIR it also writes what three DTM_WRITEs would send (settings
 * and stream bytes) for tests/test-encodemsg.sh to put through the
 * service's own library and FFmpeg. Run on x86 and under qemu-m68k: the
 * files must come out the same.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * usage: encodemsgtest [OUTDIR]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encodemsg.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const unsigned char want_settings[ME_SETTINGS_SIZE] = {
    0, 0, 0, 1,  'H', 'E', 'I', 'C',  0, 0, 1, 0x41,  0, 0, 0, 0xf0,
    0, 0, 0, 0,  0, 1, 0x86, 0xa0,  0, 0, 0x56, 0x22,  0, 0, 0, 2,
    0, 0, 0, 1,  0, 0, 0, 90,  0, 0, 0, 1,  0, 0, 0, 0,
};

static void settings(void)
{
    struct me_settings s = { ME_FORMAT_HEIC, 321, 240, ME_PIX_ARGB, 100000, 22050, 2, ME_SAMPLE_S8, 90, ME_F_ALPHA };
    unsigned char b[ME_SETTINGS_SIZE], info[ME_INFO_SIZE];
    struct me_info i;
    int k;

    me_pack_settings(b, &s);
    CHECK(!memcmp(b, want_settings, sizeof b));
    for (k = 0; k < ME_INFO_SIZE; k++)
        info[k] = (unsigned char)(k * 7 + 1);
    me_parse_info(info, &i);
    CHECK(i.format == 0x01080f16u && i.bytes == 0x1d242b32u && i.width == 0x3940474eu);
    CHECK(i.height == 0x555c636au && i.frames == 0x71787f86u && i.sample_frames == 0x8d949ba2u);
    CHECK(ME_FORMAT_HEIC == 0x48454943u && ME_FORMAT_M4A == ('M' << 24 | '4' << 16 | 'A' << 8 | ' '));
    printf("settings and info: byte for byte\n");
}

static void choices(void)
{
    unsigned char list[6 * 4];
    me_u32 all[6] = { ME_FORMAT_HEIC, ME_FORMAT_M4A, ME_FORMAT_AAC, ME_FORMAT_WMA, ME_FORMAT_HEVC, ME_FORMAT_WMV };
    const me_u32 AVIF = 0x41564946u, FLAC = 0x464c4143u, AV1 = 0x41563120u, M4A_HINT = ME_FORMAT_M4A;
    int k;

    CHECK(me_format_kind(ME_FORMAT_HEIC) == ME_KIND_PICTURE);
    CHECK(me_format_kind(ME_FORMAT_M4A) == ME_KIND_SOUND && me_format_kind(ME_FORMAT_AAC) == ME_KIND_SOUND);
    CHECK(me_format_kind(ME_FORMAT_WMA) == ME_KIND_SOUND);
    CHECK(me_format_kind(ME_FORMAT_HEVC) == ME_KIND_FILM && me_format_kind(ME_FORMAT_WMV) == ME_KIND_FILM);
    CHECK(me_format_kind(AVIF) == 0 && me_format_kind(0) == 0);
    for (k = 0; k < 6; k++)
        me_put32(list + k * 4, all[k]);
    for (k = 0; k < 6; k++)
        CHECK(me_offered(list, 6, all[k]) && !me_offered(list, (me_u32)k, all[k]));
    CHECK(!me_offered(list, 6, AVIF));

    /* asked for: of the kind, or nothing */
    CHECK(me_pick_format(ME_KIND_PICTURE, ME_FORMAT_HEIC, AVIF, 0, 1) == ME_FORMAT_HEIC);
    CHECK(me_pick_format(ME_KIND_PICTURE, ME_FORMAT_WMA, ME_FORMAT_HEIC, 0, 1) == 0);
    CHECK(me_pick_format(ME_KIND_SOUND, ME_FORMAT_WMA, ME_FORMAT_AAC, 0, 1) == ME_FORMAT_WMA);
    CHECK(me_pick_format(ME_KIND_SOUND, ME_FORMAT_AAC, ME_FORMAT_WMA, 0, 1) == ME_FORMAT_AAC);
    CHECK(me_pick_format(ME_KIND_FILM, ME_FORMAT_WMV, ME_FORMAT_HEVC, 0, 1) == ME_FORMAT_WMV);
    CHECK(me_pick_format(ME_KIND_FILM, ME_FORMAT_HEIC, ME_FORMAT_HEVC, 0, 1) == 0);
    /* the object's own */
    CHECK(me_pick_format(ME_KIND_PICTURE, 0, ME_FORMAT_HEIC, 0, 1) == ME_FORMAT_HEIC);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_AAC, M4A_HINT, 1) == ME_FORMAT_M4A);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_AAC, 0, 1) == ME_FORMAT_M4A);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_AAC, ME_FORMAT_AAC, 1) == ME_FORMAT_AAC);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_WMA, 0, 1) == ME_FORMAT_WMA);
    CHECK(me_pick_format(ME_KIND_FILM, 0, ME_FORMAT_HEVC, 0, 1) == ME_FORMAT_HEVC);
    CHECK(me_pick_format(ME_KIND_FILM, 0, ME_FORMAT_WMV, 0, 1) == ME_FORMAT_WMV);
    /* not one the service writes: nothing, unless the object has no source */
    CHECK(me_pick_format(ME_KIND_PICTURE, 0, AVIF, 0, 1) == 0);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, FLAC, 0, 1) == 0);
    CHECK(me_pick_format(ME_KIND_FILM, 0, AV1, 0, 1) == 0);
    CHECK(me_pick_format(ME_KIND_PICTURE, 0, 0, 0, 0) == ME_FORMAT_HEIC);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, 0, 0, 0) == ME_FORMAT_M4A);
    CHECK(me_pick_format(ME_KIND_FILM, 0, 0, 0, 0) == ME_FORMAT_HEVC);
    CHECK(me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_HEVC, 0, 1) == 0);   /* another kind's */

    CHECK(me_quality(-1) == 0 && me_quality(0) == 0 && me_quality(1) == 1 && me_quality(100) == 100);
    CHECK(me_quality(101) == 0 && me_quality(-50) == 0);
    CHECK(me_strip_rows(320, 4, 240, 64 * 1024) == 51 && me_strip_rows(100000, 4, 10, 1024) == 1);
    CHECK(me_strip_rows(16, 4, 10, 1 << 20) == 10 && me_strip_rows(0, 4, 7, 100) == 7);
    printf("the file to write: asked, own, none; offered lists; quality; strips\n");
}

/* Sample frame i, channel c of a test sound: distinct, signed, both bytes used. */
static int value(me_u32 i, me_u32 c)
{
    return (int)((i * 2654435761u + c * 40503u) >> 16) - 32768;
}

static void sound(void)
{
    enum { N = 37 };
    signed char m8[N], s8[N * 2], l8[N], r8[N], m16[N * 2], s16[N * 4], l16[N * 2], r16[N * 2];
    unsigned char out[N * 4 + 8];
    struct {
        struct me_sound s;
        me_u32 channels;
        int wide;
        const char *what;
    } t[] = {
        { { m8, NULL, N, 0 }, 1, 0, "8-bit mono" },
        { { s8, NULL, N, 1 }, 2, 0, "8-bit stereo, interleaved" },
        { { l8, r8, N, 0 }, 2, 0, "8-bit stereo, two buffers" },
        { { m16, NULL, N, 2 }, 1, 1, "16-bit mono" },
        { { s16, NULL, N, 3 }, 2, 1, "16-bit stereo, interleaved" },
        { { l16, r16, N, 2 }, 2, 1, "16-bit stereo, two buffers" },
        { { m8, m8, N, 1 }, 1, 0, "left and right the same buffer" },
    };
    me_u32 i, c, k;

    for (i = 0; i < N; i++) {
        m8[i] = (signed char)(value(i, 0) >> 8);
        l8[i] = (signed char)(value(i, 0) >> 8);
        r8[i] = (signed char)(value(i, 1) >> 8);
        s8[i * 2] = l8[i];
        s8[i * 2 + 1] = r8[i];
        for (c = 0; c < 2; c++) {
            int v = value(i, c);
            signed char hi = (signed char)(v >> 8), lo = (signed char)(v & 0xff);
            if (!c) {
                m16[i * 2] = l16[i * 2] = hi;
                m16[i * 2 + 1] = l16[i * 2 + 1] = lo;
            } else {
                r16[i * 2] = hi;
                r16[i * 2 + 1] = lo;
            }
            s16[i * 4 + c * 2] = hi;
            s16[i * 4 + c * 2 + 1] = lo;
        }
    }
    for (k = 0; k < sizeof t / sizeof t[0]; k++) {
        me_u32 first, count, ch = me_sound_channels(&t[k].s);
        int ok = ch == t[k].channels;
        for (first = 0; first < N + 2 && ok; first += 5)
            for (count = 0; count < 12 && ok; count += 4) {
                memset(out, 0xee, sizeof out);
                me_sound_s16(&t[k].s, first, count, out);
                for (i = 0; i < count && ok; i++)
                    for (c = 0; c < ch; c++) {
                        int v = first + i >= N ? 0 : t[k].wide ? value(first + i, c)
                                                    : (value(first + i, c) >> 8) * 256;
                        int got = (signed char)out[(i * ch + c) * 2] * 256 + out[(i * ch + c) * 2 + 1];
                        if (got != v)
                            ok = 0;
                    }
                if (out[count * ch * 2] != 0xee)     /* nothing past the end */
                    ok = 0;
            }
        if (!ok) {
            printf("FAIL sound %s\n", t[k].what);
            fails++;
        }
    }
    printf("sound: %u layouts read as sound.datatype reads them, 16-bit big-endian, nothing past the end\n",
           (unsigned)(sizeof t / sizeof t[0]));
}

static void put(const char *dir, const char *name, const void *d, size_t n)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "wb")) || fwrite(d, 1, n, f) != n || fclose(f)) {
        printf("FAIL writing %s\n", path);
        fails++;
    }
}

/* What three DTM_WRITEs send: settings and streams, for the round trip. */
static void requests(const char *dir)
{
    enum { W = 97, H = 61, RATE = 28000, FRAMES = RATE };   /* odd sizes; an Amiga's rate; one second */
    struct me_settings pic = { 0 }, snd = { 0 };
    unsigned char set[ME_SETTINGS_SIZE];
    unsigned char *px = malloc(W * H * 4), *pcm = malloc(FRAMES * 4);
    signed char *left = malloc(FRAMES), *right = malloc(FRAMES);
    struct me_sound s;
    me_u32 x, y, i;

    if (!px || !pcm || !left || !right)
        exit(2);
    /* a picture: gradients, and alpha across it, as PDTM_READPIXELARRAY gives ARGB */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = px + (y * W + x) * 4;
            p[0] = (unsigned char)(x * 255 / (W - 1));
            p[1] = (unsigned char)(y * 255 / (H - 1));
            p[2] = (unsigned char)((x + y) * 255 / (W + H - 2));
            p[3] = (unsigned char)(255 - x * 255 / (W - 1));
        }
    pic.format = me_pick_format(ME_KIND_PICTURE, 0, 0, 0, 0);
    pic.width = W;
    pic.height = H;
    pic.pixels = ME_PIX_ARGB;
    pic.flags = ME_F_ALPHA;
    me_pack_settings(set, &pic);
    put(dir, "picture.settings", set, sizeof set);
    put(dir, "picture.stream0", px, W * H * 4);
    /* a sound: 8-bit stereo in two buffers, 440 Hz left and 880 Hz right, from a
     * quarter-wave table (no floating point) */
    for (i = 0; i < FRAMES; i++) {
        static const signed char q[] = { 0, 9, 18, 26, 35, 43, 51, 58, 65, 72, 78, 84, 89, 94, 98, 102, 105, 107,
                                         109, 110, 110 };
        me_u32 ph[2] = { (me_u32)((unsigned long long)i * 440 * 80 / RATE % 80),
                         (me_u32)((unsigned long long)i * 880 * 80 / RATE % 80) };
        signed char v[2];
        for (x = 0; x < 2; x++) {
            me_u32 p = ph[x];
            v[x] = p < 20 ? q[p] : p < 40 ? q[40 - p] : p < 60 ? (signed char)-q[p - 40] : (signed char)-q[80 - p];
        }
        left[i] = v[0];
        right[i] = v[1];
    }
    s.left = left;
    s.right = right;
    s.length = FRAMES;
    s.type = 0;
    snd.format = me_pick_format(ME_KIND_SOUND, 0, ME_FORMAT_AAC, 0, 1);    /* an AAC read from an .m4a */
    snd.rate = RATE;
    snd.channels = me_sound_channels(&s);
    snd.samples = ME_SAMPLE_S16;
    me_pack_settings(set, &snd);
    put(dir, "sound.settings", set, sizeof set);
    for (i = 0; i < FRAMES; i += 1000)                    /* in strips, as the datatype sends it */
        me_sound_s16(&s, i, FRAMES - i < 1000 ? FRAMES - i : 1000, pcm + i * 4);
    put(dir, "sound.stream1", pcm, FRAMES * 4);
    snd.format = me_pick_format(ME_KIND_SOUND, ME_FORMAT_WMA, ME_FORMAT_AAC, 0, 1);
    me_pack_settings(set, &snd);
    put(dir, "wma.settings", set, sizeof set);
    put(dir, "wma.stream1", pcm, FRAMES * 4);
    free(px);
    free(pcm);
    free(left);
    free(right);
    printf("requests written: a HEIC with alpha, an M4A and a WMA\n");
}

int main(int argc, char **argv)
{
    settings();
    choices();
    sound();
    if (argc > 1)
        requests(argv[1]);
    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("encodemsg: all passed\n");
    return 0;
}
