/* (A copy of DalsinAI/gfosautoconfigboards cards/akikoplus/guest/include/inline/akikoplus.h at fb91f9c (main, PR 2 merged), for the datatypes' services: common/dtservice.c.) */
/* Automatically generated header (sfdc 1.12)! Do not edit! */

#ifndef _INLINE_AKIKOPLUS_H
#define _INLINE_AKIKOPLUS_H

#ifndef _SFDC_VARARG_DEFINED
#define _SFDC_VARARG_DEFINED
#ifdef __HAVE_IPTR_ATTR__
typedef APTR _sfdc_vararg __attribute__((iptr));
#else
typedef ULONG _sfdc_vararg;
#endif /* __HAVE_IPTR_ATTR__ */
#endif /* _SFDC_VARARG_DEFINED */

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif /* !__INLINE_MACROS_H */

#ifndef AKIKOPLUS_BASE_NAME
#define AKIKOPLUS_BASE_NAME AkikoPlusBase
#endif /* !AKIKOPLUS_BASE_NAME */

#define AP_Status() \
      LP0(0x1e, ULONG, AP_Status ,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SwapCopy(___dst, ___src, ___count, ___unit) \
      LP4(0x24, LONG, AP_SwapCopy , APTR, ___dst, a0, CONST_APTR, ___src, a1, ULONG, ___count, d0, ULONG, ___unit, d1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SwapInPlace(___data, ___count, ___unit) \
      LP3(0x2a, LONG, AP_SwapInPlace , APTR, ___data, a0, ULONG, ___count, d0, ULONG, ___unit, d1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SwapInterleaved(___data, ___layout, ___count, ___stride) \
      LP4(0x30, LONG, AP_SwapInterleaved , APTR, ___data, a0, CONST_STRPTR, ___layout, a1, ULONG, ___count, d0, ULONG, ___stride, d1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_C2P(___args) \
      LP1(0x36, LONG, AP_C2P , CONST struct APC2P *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Fill(___args) \
      LP1(0x3c, LONG, AP_Fill , CONST struct APFill *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_CopyRect(___args) \
      LP1(0x42, LONG, AP_CopyRect , CONST struct APCopy *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Scale(___args) \
      LP1(0x48, LONG, AP_Scale , CONST struct APScale *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_YUVToARGB(___args) \
      LP1(0x4e, LONG, AP_YUVToARGB , CONST struct APYUV *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Quantise(___args) \
      LP1(0x54, LONG, AP_Quantise , CONST struct APQuant *, ___args, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Compress(___src, ___dst, ___srcLength, ___dstLength, ___level) \
      LP5(0x5a, LONG, AP_Compress , CONST_APTR, ___src, a0, APTR, ___dst, a1, ULONG, ___srcLength, d0, ULONG, ___dstLength, d1, ULONG, ___level, d2,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Decompress(___src, ___dst, ___srcLength, ___dstLength) \
      LP4(0x60, LONG, AP_Decompress , CONST_APTR, ___src, a0, APTR, ___dst, a1, ULONG, ___srcLength, d0, ULONG, ___dstLength, d1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SetThreshold(___bytes) \
      LP1(0x66, ULONG, AP_SetThreshold , ULONG, ___bytes, d0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SetMode(___mode) \
      LP1(0x6c, ULONG, AP_SetMode , ULONG, ___mode, d0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_Query(___what) \
      LP1(0x72, LONG, AP_Query , ULONG, ___what, d0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_OpenService(___name, ___flags, ___error) \
      LP3(0x78, APTR, AP_OpenService , CONST_STRPTR, ___name, a0, ULONG, ___flags, d0, LONG *, ___error, a1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_CloseService(___service) \
      LP1NR(0x7e, AP_CloseService , APTR, ___service, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_CallService(___service, ___call, ___resultAux) \
      LP3(0x84, LONG, AP_CallService , APTR, ___service, a0, CONST struct APServiceCall *, ___call, a1, ULONG *, ___resultAux, a2,\
      , AKIKOPLUS_BASE_NAME)

#define AP_ServiceWhere(___service) \
      LP1(0x8a, LONG, AP_ServiceWhere , APTR, ___service, a0,\
      , AKIKOPLUS_BASE_NAME)

#define AP_SendService(___service, ___call, ___port) \
      LP3(0x90, APTR, AP_SendService , APTR, ___service, a0, CONST struct APServiceCall *, ___call, a1, struct MsgPort *, ___port, a2,\
      , AKIKOPLUS_BASE_NAME)

#define AP_WaitService(___request, ___resultAux) \
      LP2(0x96, LONG, AP_WaitService , APTR, ___request, a0, ULONG *, ___resultAux, a1,\
      , AKIKOPLUS_BASE_NAME)

#define AP_AbortService(___request, ___resultAux) \
      LP2(0x9c, LONG, AP_AbortService , APTR, ___request, a0, ULONG *, ___resultAux, a1,\
      , AKIKOPLUS_BASE_NAME)

#endif /* !_INLINE_AKIKOPLUS_H */
