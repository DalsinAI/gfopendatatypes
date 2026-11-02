/*
 * gfopenimage tests: achelpers/ac_string.h, forced in as build.sh forces
 * it into zlib, libpng and libjpeg (test_achelpers.sh). Their ways of
 * calling memcpy, memmove and memset (zlib's zmemcpy and zmemzero, libjpeg's
 * MEMCOPY and MEMZERO, plain calls as libpng's), a later declaration and a
 * second <string.h>, compiled with -Werror; then each call's result, and
 * where it went: calls of 64 bytes or more to the helpers (counted through
 * the linker's --wrap), smaller ones not.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* zutil.h */
#define zmemcpy memcpy
#define zmemzero(dest, len) memset(dest, 0, len)
/* jinclude.h */
#define MEMCOPY(dest, src, size) memcpy((void *)(dest), (const void *)(src), (size_t)(size))
#define MEMZERO(target, size) memset((void *)(target), 0, (size_t)(size))
/* a C library header declaring them again, after the forced header */
extern void *memcpy(void *, const void *, size_t);
void *memmove(void *dst, const void *src, size_t n);
extern void *memset(void *, int, size_t);

static unsigned long calls[3];
void *__real_ac_memcpy(void *, const void *, size_t);
void *__real_ac_memmove(void *, const void *, size_t);
void *__real_ac_memset(void *, int, size_t);
void *__wrap_ac_memcpy(void *d, const void *s, size_t n) { calls[0]++; return __real_ac_memcpy(d, s, n); }
void *__wrap_ac_memmove(void *d, const void *s, size_t n) { calls[1]++; return __real_ac_memmove(d, s, n); }
void *__wrap_ac_memset(void *d, int c, size_t n) { calls[2]++; return __real_ac_memset(d, c, n); }

#define BIG 40000u
static unsigned char a[BIG + 64], b[BIG + 64], want[BIG + 64];
static unsigned long long rng = 88172645463325252ull;
static unsigned long fails, checks;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 16); }
static void check(int ok, const char *what, unsigned long n)
{
    checks++;
    if (!ok && fails++ < 12)
        printf("FAIL %s (%lu)\n", what, n);
}

int main(void)
{
    int run;
    unsigned i;
    for (i = 0; i < sizeof b; i++)
        b[i] = (unsigned char)(i * 131 + (i >> 7));
    for (run = 0; run < 6000; run++) {
        size_t n = rnd() % 4 ? rnd() % 200 : rnd() % BIG, d0 = rnd() % 4, s0 = rnd() % 4;
        unsigned kind = rnd() % 6, c = 0x100 | rnd() % 256, k;
        unsigned long before[3];
        void *r;
        memcpy(before, calls, sizeof before);
        for (k = 0; k < sizeof a; k++)
            a[k] = want[k] = (unsigned char)(k ^ 0x5a);
        for (k = 0; k < n; k++)                             /* what each kind gives, a byte at a time */
            want[d0 + k] = kind < 3 ? b[s0 + k] : kind == 4 ? (unsigned char)c : 0;
        switch (kind) {
        case 0: r = zmemcpy(a + d0, b + s0, n); break;
        case 1: r = MEMCOPY(a + d0, b + s0, n); break;
        case 2: r = memmove(a + d0, b + s0, n); break;
        case 3: r = zmemzero(a + d0, n); break;
        case 4: r = memset(a + d0, (int)c, n); break;   /* the byte of its value */
        default: r = MEMZERO(a + d0, n); break;
        }
        check(r == a + d0 && !memcmp(a, want, sizeof a), "result", n);
        k = kind < 2 ? 0 : kind == 2 ? 1 : 2;
        check((calls[k] - before[k] == 1) == (n >= 64), "where it went", n);
    }
    /* constant sizes: inline below 64 bytes, the helper from 64 */
    calls[0] = calls[1] = calls[2] = 0;
    memcpy(a, b, 16);
    memset(a, 0, 63);
    memcpy(a, b, 64);
    memset(a, 1, 4096);
    check(calls[0] == 1 && calls[2] == 1, "constant sizes", 0);
    printf("%s %lu of %lu checks\n", fails ? "FAIL" : "ok  ", checks - fails, checks);
    return fails != 0;
}
