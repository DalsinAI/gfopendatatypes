/* Counts the guest instructions a qemu user-mode run executed, from its
 * log of translated blocks and their executions on stdin:
 *
 *     qemu-m68k -d in_asm,exec,nochain -D /dev/stdout PROGRAM ... | qemu_count
 *
 * Each "IN:" block lists a translated block's instructions; each "Trace"
 * line is one execution of the block at that address. The count is the
 * same as -one-insn-per-tb's number of "Trace" lines, at a fraction of the
 * time and log. Built on the host by test_opendatatypes_mp4write.sh (COUNT=1).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS (1u << 20)

static unsigned long long key[SLOTS];
static unsigned len[SLOTS];
static unsigned char used[SLOTS];

static unsigned *slot(unsigned long long k)
{
    unsigned h = (unsigned)((k * 0x9E3779B97F4A7C15ull) >> 44) & (SLOTS - 1);
    while (used[h] && key[h] != k) h = (h + 1) & (SLOTS - 1);
    if (!used[h]) {
        used[h] = 1;
        key[h] = k;
        len[h] = 0;
    }
    return &len[h];
}

int main(void)
{
    static char l[4096];
    unsigned long long total = 0, pc = 0, unknown = 0;
    int in_block = 0, have_pc = 0;
    unsigned n = 0;
    while (fgets(l, sizeof l, stdin)) {
        if (in_block) {
            if (l[0] == '0' && l[1] == 'x') {
                if (!have_pc) {
                    pc = strtoull(l + 2, 0, 16);
                    have_pc = 1;
                }
                n++;
                continue;
            }
            if (have_pc) *slot(pc) = n;
            in_block = 0;
        }
        if (!strncmp(l, "IN:", 3)) {
            in_block = 1;
            have_pc = 0;
            n = 0;
        } else if (!strncmp(l, "Trace ", 6)) {
            /* Trace 0: 0xHOST [CSBASE/PC/FLAGS/CFLAGS] */
            char *p = strchr(l, '[');
            unsigned *c;
            if (!p || !(p = strchr(p, '/'))) continue;
            c = slot(strtoull(p + 1, 0, 16) & 0xffffffffull);
            if (!*c) unknown++;
            total += *c;
        }
    }
    printf("%llu\n", total);
    if (unknown) fprintf(stderr, "qemu_count: %llu runs of blocks it never saw translated\n", unknown);
    return 0;
}
