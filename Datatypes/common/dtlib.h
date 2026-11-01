/*
 * openamigaimage datatypes: the shared library glue. Each datatype is a
 * library with its own ROMTag (common/dtlib.c) whose fifth function,
 * ObtainEngine(), hands datatypes.library the BOOPSI class.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTLIB_H
#define DTLIB_H

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

#ifndef REG
#define REG(reg, arg) arg __asm(#reg)
/* Runs fn(arg) on a stack of at least bytes (common/dtstack.c). */
ULONG dt_call_with_stack(ULONG bytes, ULONG (*fn)(APTR), APTR arg);

#endif

/* Supplied by each datatype. */
extern const char dt_superclass[];   /* "picture.datatype", "animation.datatype" */
extern const UWORD dt_superversion;  /* oldest superclass version it needs */
extern ULONG dt_instsize;            /* bytes of instance data */
ULONG dt_dispatch(Class *cl, Object *o, Msg msg);
BOOL dt_init(void);                  /* once, when the library loads */
void dt_cleanup(void);

/* Opened by the glue for every datatype. */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
extern struct Library *UtilityBase, *DataTypesBase;

/* Reads a whole DTST_FILE source into memory (AllocVec); NULL on failure,
 * with the reason in IoErr(). */
UBYTE *dt_read_source(Object *o, ULONG *size);

/* Runs fn(arg) on a stack of at least bytes (common/dtstack.c). */
ULONG dt_call_with_stack(ULONG bytes, ULONG (*fn)(APTR), APTR arg);

#endif
