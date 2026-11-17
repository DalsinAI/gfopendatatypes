/* gfopensynth.library's proto header. MIT, Copyright (c) 2026 AmiCradle
 * Team. Programs that would rather not open the library themselves link
 * -lgfopensynth and call gs_open() and the rest (<gfopensynth.h>). */
#ifndef PROTO_GFOPENSYNTH_H
#define PROTO_GFOPENSYNTH_H

#include <exec/types.h>
#include <libraries/gfopensynth.h>

#ifndef __NOLIBBASE__
extern struct Library *GFOpenSynthBase;
#endif
#if defined(__GNUC__) && !defined(GFOPENSYNTH_NO_INLINE)
#include <inline/gfopensynth.h>
#else
#include <clib/gfopensynth_protos.h>
#endif

#endif
