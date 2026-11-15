/* Minimal Amiga utility/tagitem.h for ACMedia's public ABI host tests. */
#ifndef UTILITY_TAGITEM_H
#define UTILITY_TAGITEM_H

#include <exec/types.h>

#define TAG_USER (1UL << 31)
struct TagItem { ULONG ti_Tag; ULONG ti_Data; };

#endif
