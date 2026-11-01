/*
 * stream.datatype's core with gzip.datatype's and brotli.datatype's
 * decoders on x86 or ARM64 cores (tests/test-stream.sh): decodes a file the
 * way the class does, through sdtcore.c, and writes the plain bytes.
 *
 *   streamtest gzip|brotli [OPTIONS] IN OUT
 *     -w N   gzip's wrapper: 0 auto, 1 gzip, 2 zlib, 3 raw
 *     -f N   feed the coded bytes N at a time (SDTM_FEED), taking after each
 *     -t N   take N plain bytes at a time (SDTM_TAKE)
 *     -p     pull the input through a hook (SDTA_InputHook) instead of feeding
 *     -a     decode the whole buffer at once (NewDTObject's way)
 *
 * Built without WITH_BROTLI (no dictionary to hand), brotli always fails.
 * Prints "ok BYTES" or "error CODE" (the IoErr() code). With -cuts IN it
 * instead decodes IN cut short at many lengths and with bytes changed, a
 * byte at a time, and prints a count: under AddressSanitizer, a read or
 * write outside the buffers stops it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdtcore.h"
#include "gzcore.h"
#ifdef WITH_BROTLI
#include "brdecode.h"
#else
struct br_state;
static struct br_state *br_new(struct sdt_env *e) { (void)e; return NULL; }
static LONG br_run(struct br_state *s, struct sdt_call *c) { (void)s; (void)c; return SDT_ERROR; }
static void br_free(struct br_state *s) { (void)s; }
#endif

static void *e_alloc(struct sdt_env *e, ULONG n) { (void)e; return malloc(n ? n : 1); }
static void e_free(struct sdt_env *e, void *p) { (void)e; free(p); }
static struct sdt_env env = { e_alloc, e_free };

struct codec {
    int brotli;
    struct gz_state gz;
    struct br_state *br;
};

static LONG run(void *ctx, struct sdt_call *c)
{
    struct codec *k = ctx;
    return k->brotli ? br_run(k->br, c) : gz_run(&k->gz, c);
}

struct src {
    const UBYTE *p;
    ULONG len, pos, chunk;
};

static ULONG pull(void *ctx, UBYTE *buf, ULONG len, LONG *err)
{
    struct src *s = ctx;
    ULONG n = s->len - s->pos;
    (void)err;
    if (n > len)
        n = len;
    if (n > s->chunk)
        n = s->chunk;
    memcpy(buf, s->p + s->pos, n);
    s->pos += n;
    return n;
}

static UBYTE *slurp(const char *path, ULONG *len)
{
    FILE *f = fopen(path, "rb");
    UBYTE *buf = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
        && (buf = malloc(n ? (size_t)n : 1)) && fread(buf, 1, (size_t)n, f) == (size_t)n)
        *len = (ULONG)n;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

/* Decodes data; 0 and the plain bytes, or the error code. */
static LONG decode(int brotli, int wrapper, const UBYTE *data, ULONG len, ULONG feed, ULONG take,
                   int usepull, int all, UBYTE **outp, ULONG *outlen)
{
    struct codec k;
    struct sdt_core c;
    struct src src;
    UBYTE *out = NULL, *buf;
    ULONG have = 0, cap = 0, pos = 0, flags = 0;
    LONG err = 0;

    memset(&k, 0, sizeof(k));
    k.brotli = brotli;
    if (brotli) {
        if (!(k.br = br_new(&env)))
            return SDT_ERR_MEMORY;
    } else
        gz_init(&k.gz, NULL, wrapper);
    sdt_init(&c, &env, run, &k);
    src.p = data;
    src.len = len;
    src.pos = 0;
    src.chunk = feed;
    *outp = NULL;
    *outlen = 0;
    if (all) {
        sdt_set_pull(&c, pull, &src);
        err = sdt_decode_all(&c, &out, &have);
        goto done;
    }
    if (usepull)
        sdt_set_pull(&c, pull, &src);
    buf = malloc(take);
    for (;;) {
        ULONG n;
        if (!usepull && !(flags & SDT_F_END) && pos <= len && !c.ended) {
            ULONG m = len - pos < feed ? len - pos : feed;
            if (m) {
                if ((err = sdt_feed(&c, data + pos, m)))
                    break;
                pos += m;
            } else
                sdt_end(&c);
        }
        for (;;) {
            n = sdt_take(&c, buf, take, &flags);
            if (n) {
                if (have + n > cap) {
                    cap = (have + n) * 2;
                    out = realloc(out, cap);
                }
                memcpy(out + have, buf, n);
                have += n;
            }
            if (flags & (SDT_F_END | SDT_F_ERROR | SDT_F_HUNGRY))
                break;
        }
        if (flags & SDT_F_ERROR) {
            err = c.error;
            break;
        }
        if (flags & SDT_F_END)
            break;
        if (usepull && c.ended && (flags & SDT_F_HUNGRY)) {
            err = SDT_ERR_SHORT;
            break;
        }
    }
    free(buf);
done:
    sdt_free(&c);
    if (brotli)
        br_free(k.br);
    else
        gz_free(&k.gz);
    if (err) {
        free(out);
        return err;
    }
    *outp = out;
    *outlen = have;
    return 0;
}

static int cuts(int brotli, int wrapper, const UBYTE *data, ULONG len)
{
    ULONG n, i, count = 0, step = len > 4000 ? len / 2000 : 1;
    UBYTE *out;
    ULONG outlen;
    for (n = 0; n <= len; n += step) {
        UBYTE *cut = malloc(n ? n : 1);
        memcpy(cut, data, n);
        if (!decode(brotli, wrapper, cut, n, 7, 13, 0, 0, &out, &outlen))
            free(out);
        free(cut);
        count++;
    }
    for (i = 0; i < len && i < 600; i++) {
        static const UBYTE vals[] = { 0x00, 0xff, 0x01, 0x80, 0x55 };
        ULONG v;
        for (v = 0; v < sizeof vals; v++) {
            UBYTE *bad = malloc(len);
            memcpy(bad, data, len);
            bad[i] ^= vals[v] ? vals[v] : 0x10;
            if (!decode(brotli, wrapper, bad, len, len, 4096, 0, 0, &out, &outlen))
                free(out);
            if (!decode(brotli, wrapper, bad, len, 1, 1, 0, 0, &out, &outlen))
                free(out);
            free(bad);
            count += 2;
        }
    }
    printf("cuts %lu\n", (unsigned long)count);
    return 0;
}

int main(int argc, char **argv)
{
    int brotli, wrapper = 0, usepull = 0, all = 0, i;
    ULONG feed = 0x7fffffff, take = 65536, len, outlen;
    UBYTE *data, *out;
    LONG err;
    FILE *f;

    if (argc < 3)
        return 2;
    brotli = !strcmp(argv[1], "brotli");
    for (i = 2; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc)
            wrapper = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f") && i + 1 < argc)
            feed = (ULONG)atol(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            take = (ULONG)atol(argv[++i]);
        else if (!strcmp(argv[i], "-p"))
            usepull = 1;
        else if (!strcmp(argv[i], "-a"))
            all = 1;
        else if (!strcmp(argv[i], "-cuts") && i + 1 < argc) {
            if (!(data = slurp(argv[i + 1], &len)))
                return 2;
            cuts(brotli, wrapper, data, len);
            free(data);
            return 0;
        }
    }
    if (i + 2 != argc || !(data = slurp(argv[i], &len)))
        return 2;
    if (!feed)
        feed = 1;
    if (!take)
        take = 1;
    err = decode(brotli, wrapper, data, len, feed, take, usepull, all, &out, &outlen);
    free(data);
    if (err) {
        printf("error %ld\n", (long)err);
        return 1;
    }
    if (!(f = fopen(argv[i + 1], "wb")) || (outlen && fwrite(out, 1, outlen, f) != outlen)) {
        free(out);
        return 2;
    }
    fclose(f);
    free(out);
    printf("ok %lu\n", (unsigned long)outlen);
    return 0;
}
