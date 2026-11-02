/*
 * gfopenimage datatypes: run a function on a stack of the datatype's own.
 * Programs and datatypes.library's helper processes may call a datatype on
 * a few kilobytes of stack, and decoders such as libvpx need more. The
 * swap keeps everything it needs in registers the called function must
 * preserve (d2, d3, a2, a3), so nothing is read from the old stack while
 * the new one is in use, and several tasks may use it at once.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>

#include "dtlib.h"

/* ULONG dt_swapcall(struct StackSwapStruct *sss (a0), ULONG (*fn)(APTR) (a1), APTR arg (d0)) */
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _dt_swapcall\n"
    "_dt_swapcall:\n"
    "    movem.l d2-d3/a2-a3/a6,-(sp)\n"
    "    move.l  a0,a2\n"
    "    move.l  a1,a3\n"
    "    move.l  d0,d2\n"
    "    move.l  _SysBase,a6\n"
    "    jsr     -732(a6)\n"          /* StackSwap(a2): onto the new stack */
    "    move.l  d2,-(sp)\n"
    "    jsr     (a3)\n"
    "    addq.l  #4,sp\n"
    "    move.l  d0,d3\n"
    "    move.l  a2,a0\n"
    "    move.l  _SysBase,a6\n"
    "    jsr     -732(a6)\n"          /* StackSwap(a2): back to the caller's */
    "    move.l  d3,d0\n"
    "    movem.l (sp)+,d2-d3/a2-a3/a6\n"
    "    rts\n");

ULONG dt_swapcall(REG(a0, struct StackSwapStruct *sss), REG(a1, ULONG (*fn)(APTR)), REG(d0, APTR arg));

ULONG dt_call_with_stack(ULONG bytes, ULONG (*fn)(APTR), APTR arg)
{
    struct Task *me = FindTask(NULL);
    struct StackSwapStruct *sss;
    ULONG result;
    UBYTE *stack;

    ULONG sp;

    /* Enough free stack below the current position already? */
    __asm__ volatile ("move.l sp,%0" : "=r" (sp));
    if (sp > (ULONG)me->tc_SPLower && sp - (ULONG)me->tc_SPLower >= bytes)
        return fn(arg);
    sss = AllocVec(sizeof(*sss) + bytes, MEMF_ANY);
    if (!sss)
        return fn(arg);
    stack = (UBYTE *)(sss + 1);
    sss->stk_Lower = stack;
    sss->stk_Upper = (ULONG)(stack + bytes);
    sss->stk_Pointer = (APTR)sss->stk_Upper;
    result = dt_swapcall(sss, fn, arg);
    FreeVec(sss);
    return result;
}
