/*
 * The first code of a DTCD load file: datatypes.library jumps to the first
 * hunk's first byte with the DTHookContext in A0 (the 3.x convention; the
 * Team's library puts it on the stack as well, libraries/opendatatypes/
 * opendatatypes_lib.c call_hook). This passes A0 on to dthook() as a C
 * argument and returns its BOOL in D0. Included first by each hook, which
 * is built with -fno-toplevel-reorder so this stays at the start
 * (hooks/build.sh checks the first word).
 *
 * The tag names the hook in the file, for the recognition test's
 * stand-ins (tests/recognition/) and for anyone looking at a descriptor.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef HOOKENTRY_H
#define HOOKENTRY_H

#include <exec/types.h>
#include <datatypes/datatypes.h>

__asm__("	.text\n"
        "	.globl	_dthook_entry\n"
        "_dthook_entry:\n"
        "	move.l	a0,-(sp)\n"
        "	bsr.w	_dthook\n"
        "	addq.l	#4,sp\n"
        "	rts\n");

ULONG dthook(struct DTHookContext *dthc);

#define DTHOOK_TAG(name) static const char dthook_tag[] __attribute__((used)) = "DTHOOK:" name

#endif
