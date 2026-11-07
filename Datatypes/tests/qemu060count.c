/*
 * qemu060count: reads qemu-m68k's "-d in_asm,exec,nochain" log on stdin
 * and prints how many instructions ran and how many of them a 68060
 * traps to 68060.library (64-bit MULU.L/MULS.L, and DIVU.L/DIVS.L with a
 * 64-bit dividend: qemu writes those with three operands, the 32-bit
 * DIVUL.L as divull). For tests/test-webp060.sh.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HN (1u << 22)
struct tb { unsigned long pc; unsigned n, t; unsigned long long ex; };
static struct tb *H;

static struct tb *get(unsigned long pc)
{
    unsigned h = (unsigned)((pc * 2654435761u) >> 3) & (HN - 1);
    while (H[h].pc && H[h].pc != pc)
        h = (h + 1) & (HN - 1);
    H[h].pc = pc;
    return &H[h];
}

int main(void)
{
    static char line[4096];
    unsigned long long tot = 0, trap = 0;
    unsigned i;
    int have = 0;

    if (!(H = calloc(HN, sizeof *H)))
        return 2;
    have = fgets(line, sizeof line, stdin) != NULL;
    while (have) {
        if (!strncmp(line, "IN:", 3)) {
            struct tb *b = NULL;
            unsigned n = 0, t = 0;
            while ((have = fgets(line, sizeof line, stdin) != NULL) && line[0] == '0') {
                unsigned long pc = strtoul(line, NULL, 16);
                char *p = strchr(line, ':'), m[32] = "", ops[256] = "";
                if (!b)
                    b = get(pc);
                n++;
                if (p && sscanf(p + 1, "%31s %255s", m, ops) >= 1) {
                    int c = 0;
                    char *q;
                    for (q = ops; *q; q++)
                        c += *q == ',';
                    if (c == 2 && (!strcmp(m, "mulul") || !strcmp(m, "mulsl") || !strcmp(m, "divul") || !strcmp(m, "divsl")))
                        t++;
                }
            }
            if (b) {
                b->n = n;
                b->t = t;
            }
            continue;
        }
        if (!strncmp(line, "Trace", 5)) {
            char *p = strchr(line, '/');
            if (p)
                get(strtoul(p + 1, NULL, 16))->ex++;
        }
        have = fgets(line, sizeof line, stdin) != NULL;
    }
    for (i = 0; i < HN; i++)
        if (H[i].pc) {
            tot += H[i].ex * H[i].n;
            trap += H[i].ex * H[i].t;
        }
    printf("%llu %llu\n", tot, trap);
    return 0;
}
