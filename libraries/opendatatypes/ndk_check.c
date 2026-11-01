/* GFOpenDatatypes: every number and layout dt_core.h carries, checked against
 * the NDK 3.2 headers at compile time (and dtclass_env.h's, below). Built (never run) by build-amiga.sh
 * on the stove; a mismatch stops the build. Names an older NDK may not carry
 * (V44/V45 additions) are checked when the headers have them.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#include <stddef.h>
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <utility/tagitem.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <exec/semaphores.h>
#include <utility/hooks.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/cghooks.h>
#include <devices/inputevent.h>
#include <devices/printer.h>
#include "dt_core.h"
#include "dtclass_env.h"

#define SAME(a, b) _Static_assert((unsigned long)(a) == (unsigned long)(b), #a " is not " #b)
#define AT(ours, field, theirs, tfield) SAME(offsetof(struct ours, field), offsetof(struct theirs, tfield))
#define SIZE(ours, theirs) SAME(sizeof(struct ours), sizeof(struct theirs))

SIZE(odt_Node, Node);
SIZE(odt_List, List);
SIZE(odt_TagItem, TagItem);
SAME(ODT_TAG_DONE, TAG_DONE); SAME(ODT_TAG_IGNORE, TAG_IGNORE);
SAME(ODT_TAG_MORE, TAG_MORE); SAME(ODT_TAG_SKIP, TAG_SKIP);

SAME(ODT_ID_DTYP, ID_DTYP); SAME(ODT_ID_DTHD, ID_DTHD); SAME(ODT_ID_CODE, ID_CODE);
SAME(ODT_ID_TOOL, ID_TOOL); SAME(ODT_ID_TAGS, ID_TAGS); SAME(ODT_ID_NAME, ID_NAME);

SIZE(odt_DataTypeHeader, DataTypeHeader);
SAME(sizeof(struct odt_DataTypeHeader), DTHSIZE);
AT(odt_DataTypeHeader, dth_Name, DataTypeHeader, dth_Name);
AT(odt_DataTypeHeader, dth_BaseName, DataTypeHeader, dth_BaseName);
AT(odt_DataTypeHeader, dth_Pattern, DataTypeHeader, dth_Pattern);
AT(odt_DataTypeHeader, dth_Mask, DataTypeHeader, dth_Mask);
AT(odt_DataTypeHeader, dth_GroupID, DataTypeHeader, dth_GroupID);
AT(odt_DataTypeHeader, dth_ID, DataTypeHeader, dth_ID);
AT(odt_DataTypeHeader, dth_MaskLen, DataTypeHeader, dth_MaskLen);
AT(odt_DataTypeHeader, dth_Pad, DataTypeHeader, dth_Pad);
AT(odt_DataTypeHeader, dth_Flags, DataTypeHeader, dth_Flags);
AT(odt_DataTypeHeader, dth_Priority, DataTypeHeader, dth_Priority);
SAME(ODT_DTF_TYPE_MASK, DTF_TYPE_MASK); SAME(ODT_DTF_BINARY, DTF_BINARY); SAME(ODT_DTF_ASCII, DTF_ASCII);
SAME(ODT_DTF_IFF, DTF_IFF); SAME(ODT_DTF_MISC, DTF_MISC); SAME(ODT_DTF_CASE, DTF_CASE);
SAME(ODT_DTF_SYSTEM1, DTF_SYSTEM1);
SAME(ODT_GID_SYSTEM, GID_SYSTEM); SAME(ODT_GID_TEXT, GID_TEXT); SAME(ODT_GID_DOCUMENT, GID_DOCUMENT);
SAME(ODT_GID_SOUND, GID_SOUND); SAME(ODT_GID_INSTRUMENT, GID_INSTRUMENT); SAME(ODT_GID_MUSIC, GID_MUSIC);
SAME(ODT_GID_PICTURE, GID_PICTURE); SAME(ODT_GID_ANIMATION, GID_ANIMATION); SAME(ODT_GID_MOVIE, GID_MOVIE);

SIZE(odt_DTHookContext, DTHookContext);
AT(odt_DTHookContext, dthc_Lock, DTHookContext, dthc_Lock);
AT(odt_DTHookContext, dthc_FIB, DTHookContext, dthc_FIB);
AT(odt_DTHookContext, dthc_FileHandle, DTHookContext, dthc_FileHandle);
AT(odt_DTHookContext, dthc_IFF, DTHookContext, dthc_IFF);
AT(odt_DTHookContext, dthc_Buffer, DTHookContext, dthc_Buffer);
AT(odt_DTHookContext, dthc_BufferLength, DTHookContext, dthc_BufferLength);

SIZE(odt_Tool, Tool);
AT(odt_Tool, tn_Which, Tool, tn_Which);
AT(odt_Tool, tn_Flags, Tool, tn_Flags);
AT(odt_Tool, tn_Program, Tool, tn_Program);
SAME(ODT_TW_INFO, TW_INFO); SAME(ODT_TW_BROWSE, TW_BROWSE); SAME(ODT_TW_EDIT, TW_EDIT);
SAME(ODT_TW_PRINT, TW_PRINT); SAME(ODT_TW_MAIL, TW_MAIL);
SAME(ODT_TF_LAUNCH_MASK, TF_LAUNCH_MASK); SAME(ODT_TF_SHELL, TF_SHELL);
SAME(ODT_TF_WORKBENCH, TF_WORKBENCH); SAME(ODT_TF_RX, TF_RX);

SIZE(odt_DataType, DataType);
AT(odt_DataType, dtn_Node1, DataType, dtn_Node1);
AT(odt_DataType, dtn_Node2, DataType, dtn_Node2);
AT(odt_DataType, dtn_Header, DataType, dtn_Header);
AT(odt_DataType, dtn_ToolList, DataType, dtn_ToolList);
AT(odt_DataType, dtn_FunctionName, DataType, dtn_FunctionName);
AT(odt_DataType, dtn_AttrList, DataType, dtn_AttrList);
AT(odt_DataType, dtn_Length, DataType, dtn_Length);

SIZE(odt_ToolNode, ToolNode);
AT(odt_ToolNode, tn_Tool, ToolNode, tn_Tool);
AT(odt_ToolNode, tn_Length, ToolNode, tn_Length);

SAME(ODT_DTST_RAM, DTST_RAM); SAME(ODT_DTST_FILE, DTST_FILE); SAME(ODT_DTST_CLIPBOARD, DTST_CLIPBOARD);
SAME(ODT_DTST_HOTLINK, DTST_HOTLINK);
#ifdef DTST_MEMORY
SAME(ODT_DTST_MEMORY, DTST_MEMORY);
#endif

SAME(ODT_DTERROR_UNKNOWN_DATATYPE, DTERROR_UNKNOWN_DATATYPE);
SAME(ODT_DTERROR_COULDNT_SAVE, DTERROR_COULDNT_SAVE);
SAME(ODT_DTERROR_COULDNT_OPEN, DTERROR_COULDNT_OPEN);
SAME(ODT_DTERROR_COULDNT_SEND_MESSAGE, DTERROR_COULDNT_SEND_MESSAGE);
SAME(ODT_DTERROR_COULDNT_OPEN_CLIPBOARD, DTERROR_COULDNT_OPEN_CLIPBOARD);
SAME(ODT_DTERROR_UNKNOWN_COMPRESSION, DTERROR_UNKNOWN_COMPRESSION);
SAME(ODT_DTERROR_NOT_ENOUGH_DATA, DTERROR_NOT_ENOUGH_DATA);
SAME(ODT_DTERROR_INVALID_DATA, DTERROR_INVALID_DATA);
#ifdef DTERROR_NOT_AVAILABLE
SAME(ODT_DTERROR_NOT_AVAILABLE, DTERROR_NOT_AVAILABLE);
#endif
SAME(ODT_DTMSG_TYPE_OFFSET, DTMSG_TYPE_OFFSET);

SIZE(odt_DTMethod, DTMethod);
AT(odt_DTMethod, dtm_Command, DTMethod, dtm_Command);
AT(odt_DTMethod, dtm_Method, DTMethod, dtm_Method);
SAME(ODT_STM_PAUSE, STM_PAUSE); SAME(ODT_STM_PLAY, STM_PLAY); SAME(ODT_STM_CONTENTS, STM_CONTENTS);
SAME(ODT_STM_INDEX, STM_INDEX); SAME(ODT_STM_RETRACE, STM_RETRACE);
SAME(ODT_STM_BROWSE_PREV, STM_BROWSE_PREV); SAME(ODT_STM_BROWSE_NEXT, STM_BROWSE_NEXT);
SAME(ODT_STM_NEXT_FIELD, STM_NEXT_FIELD); SAME(ODT_STM_PREV_FIELD, STM_PREV_FIELD);
SAME(ODT_STM_ACTIVATE_FIELD, STM_ACTIVATE_FIELD); SAME(ODT_STM_COMMAND, STM_COMMAND);
SAME(ODT_STM_REWIND, STM_REWIND); SAME(ODT_STM_FASTFORWARD, STM_FASTFORWARD);
SAME(ODT_STM_STOP, STM_STOP); SAME(ODT_STM_RESUME, STM_RESUME); SAME(ODT_STM_LOCATE, STM_LOCATE);
#ifdef STM_HELP
SAME(ODT_STM_HELP, STM_HELP);
#endif
#ifdef STM_SEARCH
SAME(ODT_STM_SEARCH, STM_SEARCH);
#endif
#ifdef STM_SEARCH_NEXT
SAME(ODT_STM_SEARCH_NEXT, STM_SEARCH_NEXT);
#endif
#ifdef STM_SEARCH_PREV
SAME(ODT_STM_SEARCH_PREV, STM_SEARCH_PREV);
#endif
#ifdef STM_USER
SAME(ODT_STM_USER, STM_USER);
#endif
#ifdef STMF_METHOD_MASK
SAME(ODT_STMF_METHOD_MASK, STMF_METHOD_MASK);
#endif
#ifdef STMF_DATA_MASK
SAME(ODT_STMF_DATA_MASK, STMF_DATA_MASK);
#endif
#ifdef STMF_RESERVED_MASK
SAME(ODT_STMF_RESERVED_MASK, STMF_RESERVED_MASK);
#endif
#ifdef STMD_VOID
SAME(ODT_STMD_VOID, STMD_VOID);
#endif
#ifdef STMD_ULONG
SAME(ODT_STMD_ULONG, STMD_ULONG);
#endif
#ifdef STMD_STRPTR
SAME(ODT_STMD_STRPTR, STMD_STRPTR);
#endif
#ifdef STMD_TAGLIST
SAME(ODT_STMD_TAGLIST, STMD_TAGLIST);
#endif

#ifdef TOOLA_Program
SAME(ODT_TOOLA_Program, TOOLA_Program);
#endif
#ifdef TOOLA_Which
SAME(ODT_TOOLA_Which, TOOLA_Which);
#endif
#ifdef TOOLA_LaunchType
SAME(ODT_TOOLA_LaunchType, TOOLA_LaunchType);
#endif

/* ---- dtclass_env.h ------------------------------------------------------------ */

SAME(ODT_DTA_Dummy, DTA_Dummy);
SAME(ODT_DTA_TextAttr, DTA_TextAttr);
SAME(ODT_DTA_TopVert, DTA_TopVert);
SAME(ODT_DTA_VisibleVert, DTA_VisibleVert);
SAME(ODT_DTA_TotalVert, DTA_TotalVert);
SAME(ODT_DTA_VertUnit, DTA_VertUnit);
SAME(ODT_DTA_TopHoriz, DTA_TopHoriz);
SAME(ODT_DTA_VisibleHoriz, DTA_VisibleHoriz);
SAME(ODT_DTA_TotalHoriz, DTA_TotalHoriz);
SAME(ODT_DTA_HorizUnit, DTA_HorizUnit);
SAME(ODT_DTA_NodeName, DTA_NodeName);
SAME(ODT_DTA_Title, DTA_Title);
SAME(ODT_DTA_TriggerMethods, DTA_TriggerMethods);
SAME(ODT_DTA_Data, DTA_Data);
SAME(ODT_DTA_TextFont, DTA_TextFont);
SAME(ODT_DTA_Methods, DTA_Methods);
SAME(ODT_DTA_PrinterStatus, DTA_PrinterStatus);
SAME(ODT_DTA_PrinterProc, DTA_PrinterProc);
SAME(ODT_DTA_LayoutProc, DTA_LayoutProc);
SAME(ODT_DTA_Busy, DTA_Busy);
SAME(ODT_DTA_Sync, DTA_Sync);
SAME(ODT_DTA_BaseName, DTA_BaseName);
SAME(ODT_DTA_GroupID, DTA_GroupID);
SAME(ODT_DTA_ErrorLevel, DTA_ErrorLevel);
SAME(ODT_DTA_ErrorNumber, DTA_ErrorNumber);
SAME(ODT_DTA_ErrorString, DTA_ErrorString);
SAME(ODT_DTA_Conductor, DTA_Conductor);
SAME(ODT_DTA_ControlPanel, DTA_ControlPanel);
SAME(ODT_DTA_Immediate, DTA_Immediate);
SAME(ODT_DTA_Repeat, DTA_Repeat);
SAME(ODT_DTA_Name, DTA_Name);
SAME(ODT_DTA_SourceType, DTA_SourceType);
SAME(ODT_DTA_Handle, DTA_Handle);
SAME(ODT_DTA_DataType, DTA_DataType);
SAME(ODT_DTA_Domain, DTA_Domain);
SAME(ODT_DTA_Left, DTA_Left);
SAME(ODT_DTA_Top, DTA_Top);
SAME(ODT_DTA_Width, DTA_Width);
SAME(ODT_DTA_Height, DTA_Height);
SAME(ODT_DTA_ObjName, DTA_ObjName);
SAME(ODT_DTA_ObjAuthor, DTA_ObjAuthor);
SAME(ODT_DTA_ObjAnnotation, DTA_ObjAnnotation);
SAME(ODT_DTA_ObjCopyright, DTA_ObjCopyright);
SAME(ODT_DTA_ObjVersion, DTA_ObjVersion);
SAME(ODT_DTA_ObjectID, DTA_ObjectID);
SAME(ODT_DTA_UserData, DTA_UserData);
SAME(ODT_DTA_FrameInfo, DTA_FrameInfo);
SAME(ODT_DTA_RelRight, DTA_RelRight);
SAME(ODT_DTA_RelBottom, DTA_RelBottom);
SAME(ODT_DTA_RelWidth, DTA_RelWidth);
SAME(ODT_DTA_RelHeight, DTA_RelHeight);
SAME(ODT_DTA_SelectDomain, DTA_SelectDomain);
SAME(ODT_DTA_TotalPVert, DTA_TotalPVert);
SAME(ODT_DTA_TotalPHoriz, DTA_TotalPHoriz);
SAME(ODT_DTA_NominalVert, DTA_NominalVert);
SAME(ODT_DTA_NominalHoriz, DTA_NominalHoriz);
SAME(ODT_DTA_DestCols, DTA_DestCols);
SAME(ODT_DTA_DestRows, DTA_DestRows);
SAME(ODT_DTA_Special, DTA_Special);
SAME(ODT_DTA_RastPort, DTA_RastPort);
SAME(ODT_DTA_ARexxPortName, DTA_ARexxPortName);
#ifdef DTA_SourceAddress
SAME(ODT_DTA_SourceAddress, DTA_SourceAddress);
#endif
#ifdef DTA_SourceSize
SAME(ODT_DTA_SourceSize, DTA_SourceSize);
#endif
SAME(ODT_DTM_FRAMEBOX, DTM_FRAMEBOX);
SAME(ODT_DTM_PROCLAYOUT, DTM_PROCLAYOUT);
SAME(ODT_DTM_ASYNCLAYOUT, DTM_ASYNCLAYOUT);
SAME(ODT_DTM_REMOVEDTOBJECT, DTM_REMOVEDTOBJECT);
SAME(ODT_DTM_SELECT, DTM_SELECT);
SAME(ODT_DTM_CLEARSELECTED, DTM_CLEARSELECTED);
SAME(ODT_DTM_COPY, DTM_COPY);
SAME(ODT_DTM_PRINT, DTM_PRINT);
SAME(ODT_DTM_ABORTPRINT, DTM_ABORTPRINT);
SAME(ODT_DTM_NEWMEMBER, DTM_NEWMEMBER);
SAME(ODT_DTM_DISPOSEMEMBER, DTM_DISPOSEMEMBER);
SAME(ODT_DTM_GOTO, DTM_GOTO);
SAME(ODT_DTM_TRIGGER, DTM_TRIGGER);
SAME(ODT_DTM_OBTAINDRAWINFO, DTM_OBTAINDRAWINFO);
SAME(ODT_DTM_DRAW, DTM_DRAW);
SAME(ODT_DTM_RELEASEDRAWINFO, DTM_RELEASEDRAWINFO);
SAME(ODT_DTM_WRITE, DTM_WRITE);
SAME(ODT_DTSIF_LAYOUT, DTSIF_LAYOUT);
SAME(ODT_DTSIF_NEWSIZE, DTSIF_NEWSIZE);
SAME(ODT_DTSIF_DRAGGING, DTSIF_DRAGGING);
SAME(ODT_DTSIF_DRAGSELECT, DTSIF_DRAGSELECT);
SAME(ODT_DTSIF_HIGHLIGHT, DTSIF_HIGHLIGHT);
SAME(ODT_DTSIF_PRINTING, DTSIF_PRINTING);
SAME(ODT_DTSIF_LAYOUTPROC, DTSIF_LAYOUTPROC);
SAME(ODT_FIF_SCALABLE, FIF_SCALABLE);
SAME(ODT_FIF_SCROLLABLE, FIF_SCROLLABLE);
SAME(ODT_FIF_REMAPPABLE, FIF_REMAPPABLE);
SAME(ODT_FRAMEF_SPECIFY, FRAMEF_SPECIFY); SAME(ODT_DTWM_IFF, DTWM_IFF); SAME(ODT_DTWM_RAW, DTWM_RAW);
SAME(ODT_PDERR_NOERR, PDERR_NOERR); SAME(ODT_PDERR_CANCEL, PDERR_CANCEL);
SIZE(odt_SignalSemaphore, SignalSemaphore);
SIZE(odt_Hook, Hook);
SIZE(odt_DTSpecialInfo, DTSpecialInfo);
AT(odt_DTSpecialInfo, si_Lock, DTSpecialInfo, si_Lock);
AT(odt_DTSpecialInfo, si_Flags, DTSpecialInfo, si_Flags);
AT(odt_DTSpecialInfo, si_TopVert, DTSpecialInfo, si_TopVert);
AT(odt_DTSpecialInfo, si_VisVert, DTSpecialInfo, si_VisVert);
AT(odt_DTSpecialInfo, si_TotVert, DTSpecialInfo, si_TotVert);
AT(odt_DTSpecialInfo, si_OTopVert, DTSpecialInfo, si_OTopVert);
AT(odt_DTSpecialInfo, si_VertUnit, DTSpecialInfo, si_VertUnit);
AT(odt_DTSpecialInfo, si_TopHoriz, DTSpecialInfo, si_TopHoriz);
AT(odt_DTSpecialInfo, si_VisHoriz, DTSpecialInfo, si_VisHoriz);
AT(odt_DTSpecialInfo, si_TotHoriz, DTSpecialInfo, si_TotHoriz);
AT(odt_DTSpecialInfo, si_OTopHoriz, DTSpecialInfo, si_OTopHoriz);
AT(odt_DTSpecialInfo, si_HorizUnit, DTSpecialInfo, si_HorizUnit);
SIZE(odt_FrameInfo, FrameInfo);
AT(odt_FrameInfo, fri_PropertyFlags, FrameInfo, fri_PropertyFlags);
AT(odt_FrameInfo, fri_Resolution, FrameInfo, fri_Resolution);
AT(odt_FrameInfo, fri_RedBits, FrameInfo, fri_RedBits);
AT(odt_FrameInfo, fri_GreenBits, FrameInfo, fri_GreenBits);
AT(odt_FrameInfo, fri_BlueBits, FrameInfo, fri_BlueBits);
AT(odt_FrameInfo, fri_Dimensions, FrameInfo, fri_Dimensions);
AT(odt_FrameInfo, fri_Screen, FrameInfo, fri_Screen);
AT(odt_FrameInfo, fri_ColorMap, FrameInfo, fri_ColorMap);
AT(odt_FrameInfo, fri_Flags, FrameInfo, fri_Flags);
SIZE(odt_dtGeneral, dtGeneral);
AT(odt_dtGeneral, dtg_GInfo, dtGeneral, dtg_GInfo);
SIZE(odt_dtSelect, dtSelect);
AT(odt_dtSelect, dts_GInfo, dtSelect, dts_GInfo);
AT(odt_dtSelect, dts_Select, dtSelect, dts_Select);
SIZE(odt_dtFrameBox, dtFrameBox);
AT(odt_dtFrameBox, dtf_GInfo, dtFrameBox, dtf_GInfo);
AT(odt_dtFrameBox, dtf_ContentsInfo, dtFrameBox, dtf_ContentsInfo);
AT(odt_dtFrameBox, dtf_FrameInfo, dtFrameBox, dtf_FrameInfo);
AT(odt_dtFrameBox, dtf_SizeFrameInfo, dtFrameBox, dtf_SizeFrameInfo);
AT(odt_dtFrameBox, dtf_FrameFlags, dtFrameBox, dtf_FrameFlags);
SIZE(odt_dtGoto, dtGoto);
AT(odt_dtGoto, dtg_GInfo, dtGoto, dtg_GInfo);
AT(odt_dtGoto, dtg_NodeName, dtGoto, dtg_NodeName);
AT(odt_dtGoto, dtg_AttrList, dtGoto, dtg_AttrList);
SIZE(odt_dtTrigger, dtTrigger);
AT(odt_dtTrigger, dtt_GInfo, dtTrigger, dtt_GInfo);
AT(odt_dtTrigger, dtt_Function, dtTrigger, dtt_Function);
AT(odt_dtTrigger, dtt_Data, dtTrigger, dtt_Data);
SIZE(odt_dtPrint, dtPrint);
AT(odt_dtPrint, dtp_GInfo, dtPrint, dtp_GInfo);
AT(odt_dtPrint, dtp_PIO, dtPrint, dtp_PIO);
AT(odt_dtPrint, dtp_AttrList, dtPrint, dtp_AttrList);
SIZE(odt_dtDraw, dtDraw);
AT(odt_dtDraw, dtd_RPort, dtDraw, dtd_RPort);
AT(odt_dtDraw, dtd_Left, dtDraw, dtd_Left);
AT(odt_dtDraw, dtd_Top, dtDraw, dtd_Top);
AT(odt_dtDraw, dtd_Width, dtDraw, dtd_Width);
AT(odt_dtDraw, dtd_Height, dtDraw, dtd_Height);
AT(odt_dtDraw, dtd_TopHoriz, dtDraw, dtd_TopHoriz);
AT(odt_dtDraw, dtd_TopVert, dtDraw, dtd_TopVert);
AT(odt_dtDraw, dtd_AttrList, dtDraw, dtd_AttrList);
/* NDK 3.2's datatypesclass.h has DTM_RELEASEDRAWINFO but no struct for it:
 * the autodoc's MethodID and dtr_Handle */
SAME(sizeof(struct odt_dtReleaseDrawInfo), 8);
SAME(offsetof(struct odt_dtReleaseDrawInfo, dtr_Handle), 4);
SIZE(odt_dtWrite, dtWrite);
AT(odt_dtWrite, dtw_GInfo, dtWrite, dtw_GInfo);
AT(odt_dtWrite, dtw_FileHandle, dtWrite, dtw_FileHandle);
AT(odt_dtWrite, dtw_Mode, dtWrite, dtw_Mode);
AT(odt_dtWrite, dtw_AttrList, dtWrite, dtw_AttrList);
SIZE(odt_IClass, IClass);
AT(odt_IClass, cl_InstOffset, IClass, cl_InstOffset);
AT(odt_IClass, cl_UserData, IClass, cl_UserData);
SIZE(odt_Object, _Object);
SIZE(odt_opSet, opSet); SIZE(odt_opUpdate, opUpdate); SIZE(odt_opGet, opGet);
SAME(ODT_OM_NEW, OM_NEW); SAME(ODT_OM_DISPOSE, OM_DISPOSE); SAME(ODT_OM_SET, OM_SET); SAME(ODT_OM_GET, OM_GET);
SAME(ODT_OM_NOTIFY, OM_NOTIFY); SAME(ODT_OM_UPDATE, OM_UPDATE); SAME(ODT_OPUF_INTERIM, OPUF_INTERIM);
SAME(ODT_ICA_TARGET, ICA_TARGET); SAME(ODT_ICA_MAP, ICA_MAP); SAME(ODT_ICTARGET_IDCMP, ICTARGET_IDCMP);
SIZE(odt_IBox, IBox); SIZE(odt_Rectangle, Rectangle);
SIZE(odt_Gadget, Gadget); SIZE(odt_ExtGadget, ExtGadget);
AT(odt_Gadget, SpecialInfo, Gadget, SpecialInfo); AT(odt_Gadget, GadgetID, Gadget, GadgetID);
SIZE(odt_GadgetInfo, GadgetInfo); AT(odt_GadgetInfo, gi_Domain, GadgetInfo, gi_Domain); AT(odt_GadgetInfo, gi_DrInfo, GadgetInfo, gi_DrInfo);
SIZE(odt_InputEvent, InputEvent); AT(odt_InputEvent, ie_Code, InputEvent, ie_Code); AT(odt_InputEvent, ie_PosX, InputEvent, ie_position);
SIZE(odt_gpInput, gpInput); SIZE(odt_gpLayout, gpLayout); SIZE(odt_gpRender, gpRender); SIZE(odt_gpHitTest, gpHitTest); SIZE(odt_gpGoInactive, gpGoInactive);
AT(odt_gpInput, gpi_Mouse, gpInput, gpi_Mouse);
SAME(ODT_GM_HITTEST, GM_HITTEST); SAME(ODT_GM_RENDER, GM_RENDER); SAME(ODT_GM_GOACTIVE, GM_GOACTIVE);
SAME(ODT_GM_HANDLEINPUT, GM_HANDLEINPUT); SAME(ODT_GM_GOINACTIVE, GM_GOINACTIVE); SAME(ODT_GM_LAYOUT, GM_LAYOUT);
SAME(ODT_GMR_GADGETHIT, GMR_GADGETHIT); SAME(ODT_GMR_MEACTIVE, GMR_MEACTIVE); SAME(ODT_GMR_NOREUSE, GMR_NOREUSE);
SAME(ODT_GMR_REUSE, GMR_REUSE); SAME(ODT_GMR_VERIFY, GMR_VERIFY); SAME(ODT_GREDRAW_UPDATE, GREDRAW_UPDATE); SAME(ODT_GREDRAW_REDRAW, GREDRAW_REDRAW);
SAME(ODT_GA_Left, GA_Left); SAME(ODT_GA_Top, GA_Top); SAME(ODT_GA_Width, GA_Width); SAME(ODT_GA_Height, GA_Height);
SAME(ODT_GA_RelRight, GA_RelRight); SAME(ODT_GA_RelBottom, GA_RelBottom); SAME(ODT_GA_RelWidth, GA_RelWidth); SAME(ODT_GA_RelHeight, GA_RelHeight);
SAME(ODT_GA_ID, GA_ID); SAME(ODT_GA_UserData, GA_UserData); SAME(ODT_GA_SpecialInfo, GA_SpecialInfo); SAME(ODT_GA_Disabled, GA_Disabled);
SAME(ODT_GA_Selected, GA_Selected); SAME(ODT_GA_Immediate, GA_Immediate); SAME(ODT_GA_RelVerify, GA_RelVerify);
SAME(ODT_GA_FollowMouse, GA_FollowMouse); SAME(ODT_GA_GZZGadget, GA_GZZGadget); SAME(ODT_GA_DrawInfo, GA_DrawInfo); SAME(ODT_GA_RelSpecial, GA_RelSpecial);
SAME(ODT_GFLG_RELBOTTOM, GFLG_RELBOTTOM); SAME(ODT_GFLG_RELRIGHT, GFLG_RELRIGHT); SAME(ODT_GFLG_RELWIDTH, GFLG_RELWIDTH);
SAME(ODT_GFLG_RELHEIGHT, GFLG_RELHEIGHT); SAME(ODT_GFLG_RELSPECIAL, GFLG_RELSPECIAL); SAME(ODT_GFLG_EXTENDED, GFLG_EXTENDED);
SAME(ODT_IECLASS_RAWMOUSE, IECLASS_RAWMOUSE); SAME(ODT_IECODE_LBUTTON, IECODE_LBUTTON); SAME(ODT_IECODE_RBUTTON, IECODE_RBUTTON);
SAME(ODT_IECODE_NOBUTTON, IECODE_NOBUTTON); SAME(ODT_IECODE_UP_PREFIX, IECODE_UP_PREFIX);
SAME(ODT_DTST_RAM, DTST_RAM); SAME(ODT_DTST_FILE, DTST_FILE); SAME(ODT_DTST_CLIPBOARD, DTST_CLIPBOARD); SAME(ODT_DTST_HOTLINK, DTST_HOTLINK);
#ifdef DTST_MEMORY
SAME(ODT_DTST_MEMORY, DTST_MEMORY);
#endif
