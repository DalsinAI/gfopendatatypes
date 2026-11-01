/*
 * gfopenmodule.datatype: what libxmp needs from a C library, in a datatype
 * that has no start-up code (../common/dtlib.c, built with DT_OWN_MALLOC,
 * has vsnprintf; the allocator is here).
 *
 * Memory: AllocVec(), as dtlib's, except in a task that has an arena (a
 * player that may hand its mixing to a cores board core): then every
 * allocation comes from that one block of Fast RAM, so the whole of
 * libxmp's state is one buffer a core may write (xmpglue.h).
 *
 * The module comes from memory (xmp_load_module_from_memory), so libxmp's
 * file functions are never reached: the few stdio calls it links to fail
 * here rather than pull in libnix's stdio, which needs a program's
 * start-up. (novorbis.c stands in for stb_vorbis.)
 *
 * libnix's soft floating point calls the ROM math libraries through the
 * bases below; each player process opens them for itself
 * (moduleclass.c), so only that process's FPU state is touched.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "xmpglue.h"

struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase, *MathIeeeSingBasBase;

FILE *fopen(const char *path, const char *mode)
{
    (void)path;
    (void)mode;
    return NULL;
}

int fclose(FILE *f)
{
    (void)f;
    return 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f)
{
    (void)buf;
    (void)size;
    (void)n;
    (void)f;
    return 0;
}

int fseek(FILE *f, long offset, int whence)
{
    (void)f;
    (void)offset;
    (void)whence;
    return -1;
}

long ftell(FILE *f)
{
    (void)f;
    return -1;
}

#undef getc
#undef putc
int getc(FILE *f)
{
    (void)f;
    return EOF;
}

int putc(int c, FILE *f)
{
    (void)c;
    (void)f;
    return EOF;
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list ap;
    int n;
    va_start(ap, format);
    n = vsnprintf(buffer, size, format, ap);
    va_end(ap);
    return n;
}

/* libxmp seeds its random numbers (random waveforms) with the time. */
time_t time(time_t *t)
{
    struct DateStamp ds;
    time_t now;
    DateStamp(&ds);
    now = (time_t)ds.ds_Days * 86400 + ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND;
    if (t)
        *t = now;
    return now;
}

/* --- memory ---------------------------------------------------------------- */

/* Each block has eight bytes in front: its size, and ARENA_MAGIC when it is
 * an arena's (else 0, an AllocVec()). */
#define ARENA_MAGIC 0x4F4D4152UL               /* 'OMAR' */

static struct om_arena *taskArena(void)
{
    return (struct om_arena *)FindTask(NULL)->tc_UserData;
}

BOOL om_arena_init(struct om_arena *a, ULONG size)
{
    a->base = AllocVec(size, MEMF_FAST | MEMF_PUBLIC);
    a->size = a->base ? size : 0;
    a->used = a->last = 0;
    a->high = 0;
    return a->base != NULL;
}

void om_arena_free(struct om_arena *a)
{
    if (a->base)
        FreeVec(a->base);
    a->base = NULL;
    a->size = a->used = a->last = 0;
}

void om_arena_use(struct om_arena *a)
{
    FindTask(NULL)->tc_UserData = a;
}

static void *arenaAlloc(struct om_arena *a, size_t size)
{
    ULONG at = (a->used + 7) & ~7UL, need = (size + 8 + 7) & ~7UL;
    ULONG *block;
    if (at + need > a->size || need < size)
        return NULL;
    block = (ULONG *)(a->base + at);
    block[0] = size;
    block[1] = ARENA_MAGIC;
    a->last = at;
    a->used = at + need;
    if (a->used > a->high)
        a->high = a->used;
    return block + 2;
}

/* Not named malloc, so GCC does not turn calloc's malloc and memset back
 * into a call to calloc. */
static void *allocate(size_t size)
{
    struct om_arena *a = taskArena();
    ULONG *block;
    if (a && a->base)
        return arenaAlloc(a, size);
    if (!(block = AllocVec(size + 8, MEMF_ANY)))
        return NULL;
    block[0] = size;
    block[1] = 0;
    return block + 2;
}

void *malloc(size_t size)
{
    return allocate(size);
}

void *calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void *p;
    if (size && total / size != count)
        return NULL;
    if ((p = allocate(total)))
        memset(p, 0, total);
    return p;
}

void free(void *p)
{
    ULONG *block = (ULONG *)p - 2;
    if (!p)
        return;
    if (block[1] == ARENA_MAGIC) {
        /* the arena's last block goes back to it (a loader's scratch
         * buffer, freed at once); others wait for the arena to go */
        struct om_arena *a = taskArena();
        if (a && a->base && (UBYTE *)block == a->base + a->last && a->used > a->last) {
            a->used = a->last;
            block[1] = 0;
        }
        return;
    }
    FreeVec(block);
}

void *realloc(void *p, size_t size)
{
    ULONG *block = (ULONG *)p - 2;
    size_t old;
    void *n;
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    old = block[0];
    if (size <= old)
        return p;
    if (block[1] == ARENA_MAGIC) {
        struct om_arena *a = taskArena();
        if (a && a->base && (UBYTE *)block == a->base + a->last) {
            ULONG need = ((size + 8 + 7) & ~7UL);
            if (a->last + need <= a->size) {     /* the last block grows in place */
                block[0] = size;
                a->used = a->last + need;
                if (a->used > a->high)
                    a->high = a->used;
                return p;
            }
            return NULL;
        }
    }
    if ((n = allocate(size))) {
        memcpy(n, p, old);
        free(p);
    }
    return n;
}
