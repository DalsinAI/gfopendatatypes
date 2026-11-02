/* SPDX-License-Identifier: MIT */
/* The 32-bit multiply and divide a 68000 module build needs under
 * -nostdlib: the os32-gcc16 stove's libgcc.a only refers to __mulsi3,
 * __udivsi3, __umodsi3, __divsi3 and __modsi3 (in this toolchain they are
 * libnix's). Linked just before -lgcc by the build scripts that need them.
 * Shift and add, shift and subtract: a fixed 32 steps, no tables, no data.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
unsigned long __mulsi3(unsigned long a, unsigned long b);
unsigned long __udivsi3(unsigned long a, unsigned long b);
unsigned long __umodsi3(unsigned long a, unsigned long b);
long __divsi3(long a, long b);
long __modsi3(long a, long b);

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
    unsigned long r = 0;
    while (b) {
        if (b & 1) r += a;
        a <<= 1;
        b >>= 1;
    }
    return r;
}

static unsigned long udiv(unsigned long a, unsigned long b, unsigned long *rem)
{
    unsigned long q = 0, r = 0;
    int i;
    for (i = 31; i >= 0; i--) {
        r = r << 1 | (a >> i & 1);
        if (r >= b) { r -= b; q |= 1ul << i; }
    }
    *rem = r;
    return q;
}

unsigned long __udivsi3(unsigned long a, unsigned long b) { unsigned long r; return udiv(a, b, &r); }
unsigned long __umodsi3(unsigned long a, unsigned long b) { unsigned long r; udiv(a, b, &r); return r; }

long __divsi3(long a, long b)
{
    unsigned long q = __udivsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b);
    return (a < 0) != (b < 0) ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
    unsigned long r = __umodsi3(a < 0 ? -(unsigned long)a : (unsigned long)a, b < 0 ? -(unsigned long)b : (unsigned long)b);
    return a < 0 ? -(long)r : (long)r;
}
