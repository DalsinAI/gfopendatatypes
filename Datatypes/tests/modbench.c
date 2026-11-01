/*
 * modbench: how long libxmp takes to mix a module, as gfopenmodule.datatype
 * asks it to (xmp_play_buffer, 8-bit stereo, in chunks of an eighth of a
 * second), and optionally the sound as a WAV file. Built for the PC (to
 * check the test modules) and for the Amiga (to measure the CPU it costs).
 *
 *   modbench FILE [SECONDS [RATE [INTERP [OUT.wav [BITS]]]]]
 *
 *   SECONDS  how much to mix (default 10)
 *   RATE     mixing rate (default 28000)
 *   INTERP   0 nearest, 1 linear, 2 spline (default 1)
 *   OUT.wav  write what was mixed (default: none; "-" for none)
 *   BITS     8 (as the datatype) or 16 (default 8)
 *
 * Prints the module's name, type, channels and length, then the time the
 * mixing took and that as a share of the sound's own length: the CPU load
 * playing it would cost.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmp.h>

#ifdef __amigaos__
#include <exec/execbase.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
extern struct ExecBase *SysBase;
struct Device *TimerBase;
static struct timerequest treq;
static ULONG efreq;

static void clock_open(void)
{
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0) == 0)
        TimerBase = treq.tr_node.io_Device;
}

static double now(void)
{
    struct EClockVal ev;
    if (!TimerBase)
        return 0;
    efreq = ReadEClock(&ev);
    return ((double)ev.ev_hi * 4294967296.0 + ev.ev_lo) / efreq;
}

static void clock_close(void)
{
    if (TimerBase)
        CloseDevice((struct IORequest *)&treq);
}

static const char *cpu_name(void)
{
    UWORD f = SysBase->AttnFlags;
    if (f & (1 << 7)) return "68060";
    if (f & AFF_68040) return "68040";
    if (f & AFF_68030) return "68030";
    if (f & AFF_68020) return "68020";
    return "68000";
}
#else
#include <time.h>
static void clock_open(void) {}
static void clock_close(void) {}
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static const char *cpu_name(void) { return "host"; }
#endif

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, unsigned long v) { put16(f, v & 0xffff); put16(f, v >> 16 & 0xffff); }

int main(int argc, char **argv)
{
    struct xmp_module_info mi;
    xmp_context ctx;
    double seconds = argc > 2 ? atof(argv[2]) : 10;
    int rate = argc > 3 ? atoi(argv[3]) : 28000;
    int interp = argc > 4 ? atoi(argv[4]) : 1;
    const char *out = argc > 5 && strcmp(argv[5], "-") ? argv[5] : NULL;
    int bits = argc > 6 ? atoi(argv[6]) : 8;
    int chunk = rate / 8, bpf = bits == 8 ? 2 : 4;
    long frames = (long)(seconds * rate), done = 0, data = 0;
    unsigned char *buf;
    FILE *wav = NULL;
    double t0, t;
    int ret, ended = 0;
    long peak = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: modbench FILE [SECONDS [RATE [INTERP [OUT.wav [BITS]]]]]\n");
        return 20;
    }
    clock_open();
    ctx = xmp_create_context();
    t0 = now();
    if ((ret = xmp_load_module(ctx, argv[1])) < 0) {
        printf("%s: libxmp can't load it (%d)\n", argv[1], ret);
        return 10;
    }
    t = now() - t0;
    xmp_get_module_info(ctx, &mi);
    printf("%s: \"%s\" %s, %d channels, %d patterns, %d instruments, %d.%03d s; loaded in %d ms\n",
           argv[1], mi.mod->name, mi.mod->type, mi.mod->chn, mi.mod->pat, mi.mod->ins,
           mi.seq_data[0].duration / 1000, mi.seq_data[0].duration % 1000, (int)(t * 1000));
    if (xmp_start_player(ctx, rate, bits == 8 ? XMP_FORMAT_8BIT : 0) < 0) {
        printf("can't start the player\n");
        return 10;
    }
    xmp_set_player(ctx, XMP_PLAYER_INTERP, interp);
    buf = malloc(chunk * bpf);
    if (out && (wav = fopen(out, "wb"))) {
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav);
        put32(wav, 16); put16(wav, 1); put16(wav, 2); put32(wav, rate);
        put32(wav, (unsigned long)rate * bpf); put16(wav, bpf); put16(wav, bits);
        fwrite("data\0\0\0\0", 1, 8, wav);
    }
    t0 = now();
    while (done < frames && !ended) {
        int n = frames - done < chunk ? (int)(frames - done) : chunk;
        int i;
        if (xmp_play_buffer(ctx, buf, n * bpf, 1) < 0)
            ended = 1;
        if (wav) {
            if (bits == 8) {           /* WAV's 8-bit is unsigned */
                for (i = 0; i < n * 2; i++) {
                    int v = (signed char)buf[i];
                    if (v < 0 ? -v > peak : v > peak) peak = v < 0 ? -v : v;
                    fputc((v + 128) & 255, wav);
                }
            } else {
                for (i = 0; i < n * 2; i++) {
                    int v = (short)(buf[2 * i] | buf[2 * i + 1] << 8);
                    if (v < 0 ? -v > peak : v > peak) peak = v < 0 ? -v : v;
                }
                fwrite(buf, bpf, n, wav);
            }
            data += n * bpf;
        }
        done += n;
    }
    t = now() - t0;
    printf("%s %d Hz %d-bit interp %d: mixed %ld.%02ld s in %ld ms, CPU %ld.%ld%%%s\n",
           cpu_name(), rate, bits, interp, done / rate, done % rate * 100 / rate, (long)(t * 1000),
           (long)(t * 100 * rate / done), (long)(t * 1000 * rate / done) % 10, ended ? " (the song ended)" : "");
    if (wav) {
        fseek(wav, 4, SEEK_SET); put32(wav, 36 + data);
        fseek(wav, 40, SEEK_SET); put32(wav, data);
        fclose(wav);
        printf("wrote %s, peak %ld\n", out, peak);
    }
    xmp_end_player(ctx);
    xmp_release_module(ctx);
    xmp_free_context(ctx);
    free(buf);
    clock_close();
    return 0;
}
