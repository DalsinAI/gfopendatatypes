/*
 * gfopenimage datatypes: the C library pieces the decoders need (malloc and
 * friends, task-safe; qsort; a small vsnprintf), with no start-up code.
 * Included by common/dtlib.c, and by a plain library built from the same
 * decoders (opensound/snddeclib.c, gfopensound-decoders.library).
 * Needs SysBase, and with OAI_AC_HELPERS ac_helpers.h (as dtlib.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#ifndef DTCLIB_FROM_DTLIB             /* dtlib.c has these already */
#ifdef OAI_AC_HELPERS
#include "ac_helpers.h"
#else
#define ac_memcpy_auto memcpy
#endif
#endif

/* --- task-safe memory for the decoders ------------------------------------ */

/* A datatype with an allocator of its own (gfopenmodule's arenas) builds this
 * file with DT_OWN_MALLOC and supplies malloc, calloc, free and realloc. */
#ifndef DT_OWN_MALLOC

void *malloc(size_t size)
{
    ULONG *block = AllocVec(size + 8, MEMF_ANY);
    if (!block)
        return NULL;
    block[0] = size;
    return block + 2;
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    ULONG *block;
    if (size && total / size != count)
        return NULL;
#ifdef OAI_AC_HELPERS
    /* cleared here rather than by exec (MEMF_CLEAR), whose loop is 68k code */
    block = AllocVec(total + 8, MEMF_ANY);
    if (!block)
        return NULL;
    ac_memset_auto(block, 0, total + 8);
#else
    block = AllocVec(total + 8, MEMF_ANY | MEMF_CLEAR);
    if (!block)
        return NULL;
#endif
    block[0] = total;
    return block + 2;
}

void free(void *p)
{
    if (p)
        FreeVec((ULONG *)p - 2);
}

void *realloc(void *p, size_t size)
{
    void *n;
    size_t old;
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    old = ((ULONG *)p)[-2];
    if (size <= old)
        return p;
    n = malloc(size);
    if (n) {
        ac_memcpy_auto(n, p, old);
        free(p);
    }
    return n;
}

#endif /* DT_OWN_MALLOC */

/* qsort of our own, in place of libnix's: on the Amiga libnix's never
 * returned from libwebp's Huffman tree sort (webp.datatype's lossless save
 * hung at 65%; webpencamiga in the lab, 6 November 2026), while the same
 * encoder with this one writes files that decode back the same. A heap
 * sort: no recursion, no extra memory, n log n at worst. */
static void qs_swap(unsigned char *a, unsigned char *b, size_t size)
{
    while (size--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static void qs_sift(unsigned char *base, size_t root, size_t n, size_t size,
                    int (*cmp)(const void *, const void *))
{
    for (;;) {
        size_t child = 2 * root + 1, big = root;
        if (child < n && cmp(base + child * size, base + big * size) > 0)
            big = child;
        if (child + 1 < n && cmp(base + (child + 1) * size, base + big * size) > 0)
            big = child + 1;
        if (big == root)
            return;
        qs_swap(base + root * size, base + big * size, size);
        root = big;
    }
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    unsigned char *b = base;
    size_t i;
    if (n < 2 || !size)
        return;
    for (i = n / 2; i-- > 0;)
        qs_sift(b, i, n, size, cmp);
    for (i = n - 1; i > 0; i--) {
        qs_swap(b, b + i * size, size);
        qs_sift(b, 0, i, size, cmp);
    }
}

/* A small vsnprintf for decoders' error messages (%s %c %d %u %x %%), so
 * libnix's stdio (which needs a program's start-up) stays out. */
int vsnprintf(char *buffer, size_t size, const char *format, va_list ap)
{
    size_t n = 0;
    char digits[12];
#define PUT(ch) do { if (n + 1 < size) buffer[n] = (ch); n++; } while (0)
    for (; *format; format++) {
        const char *text = NULL;
        unsigned long value;
        int base = 10, negative = 0, i, width = 0;
        char pad = ' ';
        if (*format != '%') {
            PUT(*format);
            continue;
        }
        format++;
        /* a width for numbers ("%02x", "%4d"); other flags are skipped */
        if (*format == '0')
            pad = '0';
        while (*format == '-' || (*format >= '0' && *format <= '9')) {
            if (*format >= '0' && *format <= '9')
                width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == '.')                    /* a precision: skipped */
            for (format++; *format >= '0' && *format <= '9'; format++)
                ;
        while (*format == 'l' || *format == 'z')
            format++;
        switch (*format) {
        case 's':
            text = va_arg(ap, const char *);
            for (text = text ? text : "(null)"; *text; text++)
                PUT(*text);
            continue;
        case 'c':
            PUT((char)va_arg(ap, int));
            continue;
        case 'd':
        case 'i': {
            long v = va_arg(ap, long);
            negative = v < 0;
            value = negative ? -(unsigned long)v : (unsigned long)v;
            break;
        }
        case 'x':
        case 'X':
            base = 16;
            value = va_arg(ap, unsigned long);
            break;
        case 'u':
            value = va_arg(ap, unsigned long);
            break;
        case '\0':
            format--;
            continue;
        default:
            PUT(*format);
            continue;
        }
        i = 0;
        do {
            digits[i++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value && i < (int)sizeof digits);
        if (negative && pad == '0')
            PUT('-');
        for (width -= i + negative; width > 0; width--)
            PUT(pad);
        if (negative && pad != '0')
            PUT('-');
        while (i)
            PUT(digits[--i]);
    }
    if (size)
        buffer[n < size ? n : size - 1] = 0;
#undef PUT
    return (int)n;
}

