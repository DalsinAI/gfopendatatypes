/*
 * libwebp-1.6.0-m68060.patch's sums against libwebp's own (tests/test-webp060.sh).
 *
 *   webp060test check            (built with -DWEBP060_CHECK, patched libwebp)
 *       the patch's helpers (src/dsp/lossless_common.h) against 64-bit
 *       arithmetic, PredictionCostBias (predictor_enc.c, built into this
 *       file) against libwebp 1.6.0's loop, FastSLog2Slow for every value
 *       up to 2^16, VP8LBitsEntropy and the entropy gathering against
 *       1.6.0's, and
 *       HashPix's 32-bit form against its 64-bit one
 *   webp060test encode OUTDIR [small]
 *       a set of pictures encoded at every level, one file each, so that
 *       a build without the patch can be compared byte for byte ("small":
 *       without the largest, for qemu-m68k)
 *   webp060test bench KIND W H LEVEL
 *       one encode, for instruction counts under qemu-m68k
 *
 * The reference functions below are libwebp 1.6.0's (Google, BSD licence,
 * upstream/libwebp/COPYING), with the Team's no-float patch where it
 * applies. MIT, Copyright (c) 2026 AmiCradle Team (the rest).
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/enc/predictor_enc.c"
#include "src/enc/histogram_enc.h"
#include "webpencode.h"

#ifdef WEBP060_CHECK
static int fails;
static uint64_t seed = 88172645463325252ull;
static uint64_t rnd64(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}
static uint32_t rnd32(void) { return (uint32_t)(rnd64() >> 32); }
/* small, middling and large values, and the edges */
static uint32_t pick32(void)
{
    static const uint32_t edge[] = { 0, 1, 2, 49, 50, 99, 100, 255, 256, 65535, 65536, 65537,
                                     0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    switch (rnd32() & 7) {
    case 0: return edge[rnd32() % 16];
    case 1: return rnd32() & 0xff;
    case 2: return rnd32() & 0xffff;
    case 3: return rnd32() >> (rnd32() & 31);
    default: return rnd32();
    }
}

#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf("FAIL " __VA_ARGS__); printf("\n"); } } } while (0)

/* libwebp 1.6.0's PredictionCostBias */
static int64_t RefPredictionCostBias(const uint32_t counts[256], uint64_t weight_0, uint64_t exp_val)
{
    const int significant_symbols = 256 >> 4;
    const uint64_t exp_decay_factor = 6;
    uint64_t bits = (weight_0 * counts[0]) << LOG_2_PRECISION_BITS;
    int i;
    exp_val <<= LOG_2_PRECISION_BITS;
    for (i = 1; i < significant_symbols; ++i) {
        bits += DivRound(exp_val * (counts[i] + counts[256 - i]), 100);
        exp_val = DivRound(exp_decay_factor * exp_val, 10);
    }
    return -DivRound((int64_t)bits, 10);
}

/* libwebp 1.6.0's VP8LBitsEntropy (BitsEntropyRefine in histogram_enc.c) */
static uint64_t RefBitsEntropy(const uint32_t *array, int n)
{
    VP8LBitEntropy entropy;
    uint64_t mix, min_limit;
    VP8LBitsEntropyUnrefined(array, n, &entropy);
    if (entropy.nonzeros < 5) {
        if (entropy.nonzeros <= 1)
            return 0;
        if (entropy.nonzeros == 2)
            return DivRound(99 * ((uint64_t)entropy.sum << LOG_2_PRECISION_BITS) + entropy.entropy, 100);
        mix = entropy.nonzeros == 3 ? 950 : 700;
    } else
        mix = 627;
    min_limit = (uint64_t)(2 * entropy.sum - entropy.max_val) << LOG_2_PRECISION_BITS;
    min_limit = DivRound(mix * min_limit + (1000 - mix) * entropy.entropy, 1000);
    return (entropy.entropy < min_limit) ? min_limit : entropy.entropy;
}

/* libwebp 1.6.0's FastSLog2Slow_C, with the no-float patch past 2^16 */
static uint64_t RefFastSLog2Slow(uint32_t v)
{
    if (v < APPROX_LOG_WITH_CORRECTION_MAX) {
        const uint64_t orig_v = v;
        uint64_t correction;
        const uint64_t log_cnt = BitsLog2Floor(v) - 7;
        const uint32_t y = 1 << log_cnt;
        v >>= log_cnt;
        correction = LOG_2_RECIPROCAL_FIXED * (orig_v & (y - 1));
        return orig_v * (kLog2Table[v] + (log_cnt << LOG_2_PRECISION_BITS)) + correction;
    } else {
        const uint64_t orig_v = v;
        const uint64_t log_cnt = BitsLog2Floor(v) - 7;
        const uint64_t y = (uint64_t)1 << log_cnt;
        const uint64_t correction = LOG_2_RECIPROCAL_FIXED * (orig_v & (y - 1));
        v >>= log_cnt;
        return orig_v * (kLog2Table[v] + (log_cnt << LOG_2_PRECISION_BITS)) + correction;
    }
}

/* libwebp 1.6.0's GetEntropyUnrefined_C */
static void RefHelper(uint32_t val, int i, uint32_t *val_prev, int *i_prev, VP8LBitEntropy *e, VP8LStreaks *st)
{
    const int streak = i - *i_prev;
    if (*val_prev != 0) {
        e->sum += (*val_prev) * streak;
        e->nonzeros += streak;
        e->nonzero_code = *i_prev;
        e->entropy += VP8LFastSLog2(*val_prev) * streak;
        if (e->max_val < *val_prev)
            e->max_val = *val_prev;
    }
    st->counts[*val_prev != 0] += (streak > 3);
    st->streaks[*val_prev != 0][(streak > 3)] += streak;
    *val_prev = val;
    *i_prev = i;
}
static void RefGetEntropyUnrefined(const uint32_t X[], int length, VP8LBitEntropy *e, VP8LStreaks *st)
{
    int i, i_prev = 0;
    uint32_t x_prev = X[0];
    memset(st, 0, sizeof(*st));
    VP8LBitEntropyInit(e);
    for (i = 1; i < length; ++i)
        if (X[i] != x_prev)
            RefHelper(X[i], i, &x_prev, &i_prev, e, st);
    RefHelper(0, i, &x_prev, &i_prev, e, st);
    e->entropy = VP8LFastSLog2(e->sum) - e->entropy;
}

static void check(void)
{
    long n, checks = 0;
    uint32_t v;

    VP8LEncDspInit();
    for (n = 0; n < 4000000; n++) {
        const uint32_t a = pick32(), b = pick32(), a16 = a & 0xffff, s = b & 0xffff;
        const uint64_t x = ((uint64_t)pick32() << 32) | pick32();
        const uint32_t d = 1 + (rnd32() & 0xfffe);
        const uint64_t big = x >> 2;           /* DivRound's a, well inside int64 */
        CHECK(VP8LMulU16x16(a, b) == (uint32_t)(a & 0xffff) * (b & 0xffff), "MulU16x16(%x, %x)", a, b);
        CHECK(VP8LMulU16x32(a16, b) == (uint64_t)a16 * b, "MulU16x32(%x, %x)", a16, b);
        CHECK(VP8LMulU32x32(a, b) == (uint64_t)a * b, "MulU32x32(%x, %x)", a, b);
        CHECK(VP8LMulU64x16(x, s) == x * s, "MulU64x16(%llx, %x)", (unsigned long long)x, s);
        CHECK(VP8LDivRoundU64(big, d) == (uint64_t)DivRound((int64_t)big, d), "DivRoundU64(%llx, %u)",
              (unsigned long long)big, d);
        CHECK(VP8LDivRoundU64(big, 10) == (uint64_t)DivRound((int64_t)big, 10), "DivRoundU64(%llx, 10)",
              (unsigned long long)big);
        CHECK(VP8LDivRoundMulBy100(a, 68) == (uint64_t)DivRound((int64_t)a * 68, 100), "DivRoundMulBy100(%u, 68)", a);
        CHECK(VP8LDivRoundMulBy100(a, 82) == (uint64_t)DivRound((int64_t)a * 82, 100), "DivRoundMulBy100(%u, 82)", a);
        CHECK((uint8_t)(((a + (a >> 19)) * 0x39c5fba7u) >> 24)
                  == (((((uint64_t)a + (a >> 19)) * 0x39c5fba7ull) & 0xffffffffu) >> 24), "HashPix(%x)", a);
        checks += 9;
    }
    for (v = 0; v < 200000; v++) {         /* every remainder the 1311 shortcut meets, and more */
        CHECK(VP8LDivRoundMulBy100(v, 68) == (uint64_t)DivRound((int64_t)v * 68, 100), "DivRoundMulBy100(%u, 68)", v);
        CHECK(VP8LDivRoundMulBy100(v, 82) == (uint64_t)DivRound((int64_t)v * 82, 100), "DivRoundMulBy100(%u, 82)", v);
        checks += 2;
    }
    for (v = LOG_LOOKUP_IDX_MAX; v < APPROX_LOG_WITH_CORRECTION_MAX; v++, checks++)
        CHECK(VP8LFastSLog2Slow(v) == RefFastSLog2Slow(v), "FastSLog2Slow(%u)", v);
    for (n = 0; n < 1000000; n++, checks++) {
        v = pick32() | LOG_LOOKUP_IDX_MAX;
        CHECK(VP8LFastSLog2Slow(v) == RefFastSLog2Slow(v), "FastSLog2Slow(%u)", v);
    }
    for (n = 0; n < 200000; n++) {          /* the bias, at the encoder's two settings */
        uint32_t counts[256];
        const int scale = (int)(rnd32() % 34);
        int i;
        for (i = 0; i < 256; i++) {
            const uint32_t r = rnd32();
            counts[i] = (n & 1) ? (r >> (r & 31)) : (scale < 32 ? (r >> scale) & 0xfffff : r);
            if ((r & 0x300) == 0) counts[i] = 0;
        }
        if ((n & 63) == 0) counts[1 + n % 15] = 0xffffffffu - (rnd32() & 3);   /* past the fast sum */
        CHECK(PredictionCostBias(counts, 1, 94) == RefPredictionCostBias(counts, 1, 94), "PredictionCostBias(1, 94) #%ld", n);
        CHECK(PredictionCostBias(counts, 3, 240) == RefPredictionCostBias(counts, 3, 240), "PredictionCostBias(3, 240) #%ld", n);
        CHECK(PredictionCostBias(counts, 2, 50) == RefPredictionCostBias(counts, 2, 50), "PredictionCostBias(2, 50) #%ld", n);
        checks += 3;
    }
    for (n = 0; n < 300000; n++) {          /* the refined entropy, 2 to 280 symbols */
        uint32_t a[280];
        const int len = 2 + (int)(rnd32() % 279), nz = (n % 7 < 4) ? 2 + (int)(n % 7) : len;
        const int shift = (int)(rnd32() % 32);
        int i;
        memset(a, 0, sizeof a);
        for (i = 0; i < nz; i++)
            a[rnd32() % len] = (n & 1) ? pick32() >> shift : (rnd32() >> shift) + 1;
        CHECK(VP8LBitsEntropy(a, len) == RefBitsEntropy(a, len), "VP8LBitsEntropy #%ld", n);
        checks++;
    }
    for (n = 0; n < 100000; n++) {          /* entropy over runs of equal counts */
        uint32_t X[2328], Y[2328];
        VP8LBitEntropy e1, e2;
        VP8LStreaks s1, s2;
        const int len = 1 + (int)(rnd32() % 2328);
        int i = 0;
        while (i < len) {
            const uint32_t val = (rnd32() & 3) ? 0 : pick32() >> (rnd32() % 20);
            int run = (rnd32() & 1) ? 1 : 1 + (int)(rnd32() % 300);
            while (run-- && i < len) {
                X[i] = val;
                Y[i] = rnd32() & 1;
                i++;
            }
        }
        VP8LGetEntropyUnrefined(X, len, &e1, &s1);
        RefGetEntropyUnrefined(X, len, &e2, &s2);
        CHECK(!memcmp(&e1, &e2, sizeof e1) && !memcmp(&s1, &s2, sizeof s1), "GetEntropyUnrefined #%ld", n);
        for (i = 0; i < len; i++)
            Y[i] += X[i];
        VP8LGetCombinedEntropyUnrefined(X, Y, len, &e1, &s1);
        for (i = 0; i < len; i++)
            Y[i] += X[i];
        RefGetEntropyUnrefined(Y, len, &e2, &s2);
        CHECK(!memcmp(&e1, &e2, sizeof e1) && !memcmp(&s1, &s2, sizeof s1), "GetCombinedEntropyUnrefined #%ld", n);
        checks += 2;
    }
    printf("webp060test: %ld checks, %d failed\n", checks, fails);
}
#endif

struct buf { uint8_t *p; ULONG n, cap; };
static LONG put(void *h, const UBYTE *p, ULONG n)
{
    struct buf *b = h;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 0;
}

static ULONG *picture(const char *kind, ULONG w, ULONG h)
{
    ULONG *px = malloc(w * h * 4), x, y;
    uint32_t s = 4242;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ULONG r, v;
            s = s * 1103515245u + 12345u;
            r = (s >> 8) & 0xffffff;
            if (!strcmp(kind, "photo"))
                v = 0xff000000UL | (((x * 200 / w + y * 40 / h + (r & 15)) & 255) << 16)
                  | (((y * 120 / h + ((r >> 4) & 15)) & 255) << 8) | ((128 + (x ^ y) / 4 + ((r >> 8) & 7)) & 255);
            else if (!strcmp(kind, "palette"))
                v = 0xff000000UL | (((x / 8 + y / 8 + (r & 1)) & 15) * 0x111111UL);
            else
                v = (((x * 13 + y * 29) & 255) << 24) | ((x & 255) << 16) | ((y & 255) << 8) | ((x + y + (r & 3)) & 255);
            px[y * w + x] = v;
        }
    return px;
}

static int encode1(const char *kind, ULONG w, ULONG h, LONG level, const char *dir)
{
    ULONG *px = picture(kind, w, h);
    struct buf b = { 0, 0, 0 };
    LONG rc = webp_encode_lossless(px, w, h, w, level, put, &b);
    if (dir && !rc) {
        char path[600];
        FILE *f;
        snprintf(path, sizeof path, "%s/%s-%lux%lu-L%ld.webp", dir, kind, (unsigned long)w, (unsigned long)h, (long)level);
        if ((f = fopen(path, "wb"))) {
            fwrite(b.p, 1, b.n, f);
            fclose(f);
        }
    }
    if (!dir)
        printf("%s %lux%lu L%ld: %lu bytes\n", kind, (unsigned long)w, (unsigned long)h, (long)level, (unsigned long)b.n);
    free(b.p);
    free(px);
    return rc != 0;
}

int main(int argc, char **argv)
{
#ifdef WEBP060_CHECK
    if (argc == 2 && !strcmp(argv[1], "check")) {
        check();
        return fails != 0;
    }
#endif
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "encode")) {
        static const char *kinds[] = { "photo", "palette", "alpha" };
        static const ULONG sizes[][2] = { { 64, 48 }, { 257, 31 }, { 160, 120 } };
        const unsigned nsizes = argc == 4 ? 2 : 3;   /* "small": not the largest */
        unsigned k, s;
        LONG l;
        int bad = 0;
        for (k = 0; k < 3; k++)
            for (s = 0; s < nsizes; s++)
                for (l = -1; l <= 9; l++)
                    bad += encode1(kinds[k], sizes[s][0], sizes[s][1], l, argv[2]);
        return bad != 0;
    }
    if (argc == 6 && !strcmp(argv[1], "bench"))
        return encode1(argv[2], atol(argv[3]), atol(argv[4]), atol(argv[5]), NULL);
    fprintf(stderr, "usage: webp060test check | encode OUTDIR [small] | bench KIND W H LEVEL\n");
    return 2;
}
