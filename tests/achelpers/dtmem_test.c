/*
 * gfopenimage tests: Datatypes/common/dtlib.c's calloc and realloc, with
 * the helpers (OAI_AC_HELPERS) and without (test_achelpers.sh): calloc's
 * memory all 0 whatever AllocVec gave, cleared by exec (MEMF_CLEAR) only
 * without the helpers; realloc keeping the contents; and calls of 64 bytes
 * or more going to the helpers (counted through the linker's --wrap).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dtmem_stub.h"

void *dt_malloc(size_t size);
void *dt_calloc(size_t count, size_t size);
void dt_free(void *p);
void *dt_realloc(void *p, size_t size);

static unsigned long cleared, allocs, copies, fills;
void *AllocVec(ULONG size, ULONG flags)
{
    unsigned char *p = malloc(size);
    if (p)
        memset(p, flags & MEMF_CLEAR ? 0 : 0xa5, size);       /* exec's memory is not clean */
    allocs++;
    cleared += (flags & MEMF_CLEAR) != 0;
    return p;
}
void FreeVec(void *block) { free(block); }

#ifdef OAI_AC_HELPERS
void *__real_ac_memcpy(void *, const void *, size_t);
void *__real_ac_memset(void *, int, size_t);
void *__wrap_ac_memcpy(void *d, const void *s, size_t n) { copies++; return __real_ac_memcpy(d, s, n); }
void *__wrap_ac_memset(void *d, int c, size_t n) { fills++; return __real_ac_memset(d, c, n); }
#endif

static unsigned long long rng = 88172645463325252ull;
static unsigned long fails, checks, big;
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
    for (run = 0; run < 3000; run++) {
        size_t count = 1 + rnd() % 16, size = rnd() % 4 ? rnd() % 40 : rnd() % 20000, n2, k;
        size_t total = count * size;
        unsigned long c0 = cleared, f0 = fills, p0 = copies;
        unsigned char *p = dt_calloc(count, size), *q;
        int zero = 1;
        for (k = 0; k < total; k++)
            zero &= p[k] == 0;
        check(p && zero, "calloc", total);
#ifdef OAI_AC_HELPERS
        check(cleared == c0 && fills - f0 == (total + 8 >= 64), "calloc's clearing", total);
#else
        check(cleared == c0 + 1, "calloc's clearing", total);
        (void)f0;
#endif
        for (k = 0; k < total; k++)
            p[k] = (unsigned char)(k * 7 + run);
        n2 = total + 1 + rnd() % 3000;
        q = dt_realloc(p, n2);
        for (k = 0; k < total && q[k] == (unsigned char)(k * 7 + run); k++)
            ;
        check(q && k == total, "realloc", total);
#ifdef OAI_AC_HELPERS
        check(copies - p0 == (total >= 64), "realloc's copy", total);
#else
        (void)p0;
#endif
        big += total >= 64;
        dt_free(q);
    }
    check(dt_calloc(0x10000, 0x10001) == NULL, "calloc's overflow", 0);
    printf("%s %lu of %lu checks (%s; %lu of %lu AllocVecs with MEMF_CLEAR, %lu calls of 64 bytes and more)\n",
           fails ? "FAIL" : "ok  ", checks - fails, checks,
#ifdef OAI_AC_HELPERS
           "with the helpers",
#else
           "without the helpers",
#endif
           cleared, allocs, big);
    return fails != 0;
}
