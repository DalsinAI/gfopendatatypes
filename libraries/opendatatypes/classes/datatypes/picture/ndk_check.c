/* The picture and sound families' numbers and layouts against the NDK 3.2
 * headers: compiled (never run) by build-class.sh on the os32-gcc16 stove;
 * any difference stops the build. Names a 3.2 NDK may not carry (the
 * V43+ picture extras, the sample-type extra) are checked when it does.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <intuition/screens.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <libraries/iffparse.h>

#include "../ilbm/ilbm_core.h"
#include "../8svx/svx_core.h"

#define SAME(a, b) _Static_assert((unsigned long)(a) == (unsigned long)(b), #a " is " #b)
#define SIZE(a, b) _Static_assert(sizeof(struct a) == sizeof(struct b), #a)

/* pictureclass.h */
SAME(PIC_PDTA_ModeID, PDTA_ModeID); SAME(PIC_PDTA_BitMapHeader, PDTA_BitMapHeader);
SAME(PIC_PDTA_BitMap, PDTA_BitMap); SAME(PIC_PDTA_ColorRegisters, PDTA_ColorRegisters);
SAME(PIC_PDTA_CRegs, PDTA_CRegs); SAME(PIC_PDTA_GRegs, PDTA_GRegs); SAME(PIC_PDTA_ColorTable, PDTA_ColorTable);
SAME(PIC_PDTA_ColorTable2, PDTA_ColorTable2); SAME(PIC_PDTA_Allocated, PDTA_Allocated);
SAME(PIC_PDTA_NumColors, PDTA_NumColors); SAME(PIC_PDTA_NumAlloc, PDTA_NumAlloc); SAME(PIC_PDTA_Remap, PDTA_Remap);
SAME(PIC_PDTA_Screen, PDTA_Screen); SAME(PIC_PDTA_FreeSourceBitMap, PDTA_FreeSourceBitMap);
SAME(PIC_PDTA_Grab, PDTA_Grab); SAME(PIC_PDTA_DestBitMap, PDTA_DestBitMap);
SAME(PIC_PDTA_ClassBitMap, PDTA_ClassBitMap); SAME(PIC_PDTA_NumSparse, PDTA_NumSparse);
SAME(PIC_PDTA_SparseTable, PDTA_SparseTable);
#ifdef PDTA_WhichPicture
SAME(PIC_PDTA_WhichPicture, PDTA_WhichPicture); SAME(PIC_PDTA_GetNumPictures, PDTA_GetNumPictures);
SAME(PIC_PDTA_MaxDitherPens, PDTA_MaxDitherPens); SAME(PIC_PDTA_DitherQuality, PDTA_DitherQuality);
SAME(PIC_PDTA_AllocatedPens, PDTA_AllocatedPens);
#endif
#ifdef PDTA_ScaleQuality
SAME(PIC_PDTA_ScaleQuality, PDTA_ScaleQuality);
#endif
#ifdef PDTA_SourceMode
SAME(PIC_PDTA_SourceMode, PDTA_SourceMode); SAME(PIC_PDTA_DestMode, PDTA_DestMode);
SAME(PIC_PMODE_V42, PMODE_V42); SAME(PIC_PMODE_V43, PMODE_V43);
SAME(PIC_PDTM_WRITEPIXELARRAY, PDTM_WRITEPIXELARRAY); SAME(PIC_PDTM_READPIXELARRAY, PDTM_READPIXELARRAY);
SAME(PIC_PBPAFMT_RGB, PBPAFMT_RGB); SAME(PIC_PBPAFMT_RGBA, PBPAFMT_RGBA); SAME(PIC_PBPAFMT_ARGB, PBPAFMT_ARGB);
SAME(PIC_PBPAFMT_LUT8, PBPAFMT_LUT8); SAME(PIC_PBPAFMT_GREY8, PBPAFMT_GREY8);
SIZE(pdtBlitPixelArray, pic_pdtBlitPixelArray);
#endif
#ifdef PDTA_UseFriendBitMap
SAME(PIC_PDTA_UseFriendBitMap, PDTA_UseFriendBitMap);
#endif
#ifdef PDTA_MaskPlane
SAME(PIC_PDTA_MaskPlane, PDTA_MaskPlane);
#endif
#ifdef PDTM_SCALE
SAME(PIC_PDTM_SCALE, PDTM_SCALE); SIZE(pdtScale, pic_pdtScale);
#endif
#ifdef PScale_KeepAspect
SAME(PIC_PScale_KeepAspect, PScale_KeepAspect);
#endif
SAME(PIC_mskNone, mskNone); SAME(PIC_mskHasMask, mskHasMask); SAME(PIC_mskHasTransparentColor, mskHasTransparentColor);
SAME(PIC_mskLasso, mskLasso);
#ifdef mskHasAlpha
SAME(PIC_mskHasAlpha, mskHasAlpha);
#endif
SAME(PIC_cmpNone, cmpNone); SAME(PIC_cmpByteRun1, cmpByteRun1);
#ifdef BMHDF_CMAPOK
SAME(PIC_BMHDF_CMAPOK, BMHDF_CMAPOK);
#endif
SIZE(BitMapHeader, pic_BitMapHeader); SIZE(ColorRegister, pic_ColorRegister);
_Static_assert(__builtin_offsetof(struct BitMapHeader, bmh_Transparent) ==
               __builtin_offsetof(struct pic_BitMapHeader, bmh_Transparent), "bmh_Transparent");

/* graphics */
SAME(PIC_INVALID_ID, INVALID_ID); SAME(PIC_MODE_LACE, LACE); SAME(PIC_MODE_EHB, EXTRA_HALFBRITE);
SAME(PIC_MODE_HAM, HAM); SAME(PIC_MODE_HIRES, HIRES); SAME(PIC_PRECISION_IMAGE, PRECISION_IMAGE);
SAME(PIC_PRECISION_EXACT, PRECISION_EXACT); SAME(PIC_OBP_Precision, OBP_Precision);
SAME(ILBM_GENLOCK_VIDEO, GENLOCK_VIDEO); SAME(ILBM_GENLOCK_AUDIO, GENLOCK_AUDIO); SAME(ILBM_VP_HIDE, VP_HIDE);
SAME(ILBM_SPRITES, SPRITES);
SIZE(BitMap, odt_BitMap);
SAME(DTC_DETAILPEN, DETAILPEN); SAME(DTC_BLOCKPEN, BLOCKPEN); SAME(DTC_TEXTPEN, TEXTPEN);
SAME(DTC_SHINEPEN, SHINEPEN); SAME(DTC_SHADOWPEN, SHADOWPEN); SAME(DTC_FILLPEN, FILLPEN);
SAME(DTC_BACKGROUNDPEN, BACKGROUNDPEN);

/* iffparse, dos */
SAME(ILBM_IFFPARSE_SCAN, IFFPARSE_SCAN); SAME(ILBM_IFFERR_EOF, IFFERR_EOF);
SAME(DTC_ERROR_NO_FREE_STORE, ERROR_NO_FREE_STORE); SAME(DTC_ERROR_REQUIRED_ARG_MISSING, ERROR_REQUIRED_ARG_MISSING);
SAME(DTC_ERROR_OBJECT_WRONG_TYPE, ERROR_OBJECT_WRONG_TYPE); SAME(DTC_ERROR_NOT_IMPLEMENTED, ERROR_NOT_IMPLEMENTED);

/* soundclass.h */
SAME(SND_SDTA_VoiceHeader, SDTA_VoiceHeader); SAME(SND_SDTA_Sample, SDTA_Sample);
SAME(SND_SDTA_SampleLength, SDTA_SampleLength); SAME(SND_SDTA_Period, SDTA_Period);
SAME(SND_SDTA_Volume, SDTA_Volume); SAME(SND_SDTA_Cycles, SDTA_Cycles); SAME(SND_SDTA_SignalTask, SDTA_SignalTask);
SAME(SND_SDTA_SignalBit, SDTA_SignalBit); SAME(SND_SDTA_Continuous, SDTA_Continuous);
#ifdef SDTA_SignalBitNumber
SAME(SND_SDTA_SignalBitNumber, SDTA_SignalBitNumber); SAME(SND_SDTA_SamplesPerSec, SDTA_SamplesPerSec);
SAME(SND_SDTA_ReplayPeriod, SDTA_ReplayPeriod); SAME(SND_SDTA_LeftSample, SDTA_LeftSample);
SAME(SND_SDTA_RightSample, SDTA_RightSample); SAME(SND_SDTA_Pan, SDTA_Pan);
SAME(SND_SDTA_FreeSampleData, SDTA_FreeSampleData); SAME(SND_SDTA_SyncSampleChange, SDTA_SyncSampleChange);
#endif
#ifdef SDTA_SampleType
SAME(SND_SDTA_SampleType, SDTA_SampleType);
#endif
#ifdef SDTST_M8S
SAME(SND_SDTST_M8S, SDTST_M8S); SAME(SND_SDTST_S8S, SDTST_S8S); SAME(SND_SDTST_M16S, SDTST_M16S);
SAME(SND_SDTST_S16S, SDTST_S16S);
#endif
SIZE(VoiceHeader, snd_VoiceHeader);
SAME(SND_CMP_NONE, CMP_NONE); SAME(SND_CMP_FIBDELTA, CMP_FIBDELTA);
