/* GFOpenDatatypes: datatypesclass, the numbers, messages and layouts every
 * datatype class shares, and struct odt_cenv, what a class core asks of
 * the system. Plain C with no Amiga headers, so the C shells, the qemu
 * tests and lean C++ class cores all include it.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Clean-room: the numbers and layouts are those the NDK 3.2 headers
 * publish (datatypes/datatypesclass.h, datatypes/datatypes.h,
 * intuition/classes.h, classusr.h, gadgetclass.h, cghooks.h,
 * graphics/gfx.h, devices/inputevent.h), written from what those headers
 * publish, never from Kickstart's, Hyperion's or AROS's code. ndk_check.c
 * checks each against the NDK on the stove (build-amiga.sh).
 *
 * The layouts are the 68k's: 32-bit pointers, big-endian, 2-byte
 * alignment; each published one has a static assert of its size here.
 *
 * Who uses what:
 *   - The datatype classes (picture, sound, text, animation and their
 *     subclasses: classes/datatypes/<name>/) use the numbers, the messages
 *     and struct odt_cenv. A class must not read struct odt_dtinst: on OS
 *     3.2 its superclass is OS 3.2's datatypesclass, whose instance data
 *     is private. Everything the superclass knows is an attribute
 *     (GetDTAttrs: DTA_Handle, DTA_SourceType, DTA_Domain, DTA_TopVert...).
 *   - datatypesclass itself (dtclass_core.c) and our library keep their
 *     instance in struct odt_dtinst, laid out below. It is published so
 *     the qemu tests and the library shell can read it; programs never
 *     see it.
 */
#ifndef OPENDATATYPES_DTCLASS_ENV_H
#define OPENDATATYPES_DTCLASS_ENV_H

#ifdef __cplusplus
extern "C" {
#define ODT_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define ODT_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

#include "dt_core.h"

ODT_STATIC_ASSERT(sizeof(void *) == 4 && sizeof(odt_u32) == 4, "the 68k's 32-bit pointers and longs");
ODT_STATIC_ASSERT(sizeof(struct odt_Node) == 14, "Node");
ODT_STATIC_ASSERT(sizeof(struct odt_List) == 14, "List");
ODT_STATIC_ASSERT(sizeof(struct odt_DataTypeHeader) == 32, "DataTypeHeader");
ODT_STATIC_ASSERT(sizeof(struct odt_DataType) == 58, "DataType");
ODT_STATIC_ASSERT(sizeof(struct odt_DTMethod) == 12, "DTMethod");
ODT_STATIC_ASSERT(sizeof(struct odt_Tool) == 8, "Tool");
ODT_STATIC_ASSERT(sizeof(struct odt_ToolNode) == 26, "ToolNode");
ODT_STATIC_ASSERT(sizeof(struct odt_DTHookContext) == 40, "DTHookContext");

#define ODT_TAG_USER 0x80000000UL

/* ---- exec and utility ------------------------------------------------------ */

struct odt_MinNode { struct odt_MinNode *mln_Succ, *mln_Pred; };
struct odt_MinList { struct odt_MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
struct odt_Hook {
    struct odt_MinNode h_MinNode;
    odt_u32 (*h_Entry)(void);
    odt_u32 (*h_SubEntry)(void);
    void *h_Data;
};
ODT_STATIC_ASSERT(sizeof(struct odt_Hook) == 20, "Hook");

/* exec/semaphores.h's SignalSemaphore, opaque: the env's sem_ calls work it */
struct odt_SignalSemaphore { odt_u8 opaque[46]; };
ODT_STATIC_ASSERT(sizeof(struct odt_SignalSemaphore) == 46, "SignalSemaphore");

/* ---- BOOPSI: intuition/classes.h and classusr.h ------------------------------ */

struct odt_IClass {
    struct odt_Hook cl_Dispatcher;
    odt_u32 cl_Reserved;
    struct odt_IClass *cl_Super;
    char *cl_ID;
    odt_u16 cl_InstOffset, cl_InstSize;
    odt_u32 cl_UserData, cl_SubclassCount, cl_ObjectCount, cl_Flags;
};
ODT_STATIC_ASSERT(sizeof(struct odt_IClass) == 52, "IClass");
struct odt_Object { struct odt_MinNode o_Node; struct odt_IClass *o_Class; };
#define ODT_OCLASS(o)        (((struct odt_Object *)(o) - 1)->o_Class)
#define ODT_INST_DATA(cl, o) ((void *)((odt_u8 *)(o) + (cl)->cl_InstOffset))

#define ODT_OM_NEW       0x101UL
#define ODT_OM_DISPOSE   0x102UL
#define ODT_OM_SET       0x103UL
#define ODT_OM_GET       0x104UL
#define ODT_OM_ADDTAIL   0x105UL
#define ODT_OM_REMOVE    0x106UL
#define ODT_OM_NOTIFY    0x107UL
#define ODT_OM_UPDATE    0x108UL
#define ODT_OM_ADDMEMBER 0x109UL
#define ODT_OM_REMMEMBER 0x10AUL
#define ODT_OPUF_INTERIM 1UL

struct odt_GadgetInfo;
struct odt_opSet    { odt_u32 MethodID; struct odt_TagItem *ops_AttrList; struct odt_GadgetInfo *ops_GInfo; };
struct odt_opUpdate { odt_u32 MethodID; struct odt_TagItem *opu_AttrList; struct odt_GadgetInfo *opu_GInfo; odt_u32 opu_Flags; };
struct odt_opGet    { odt_u32 MethodID; odt_u32 opg_AttrID; odt_u32 *opg_Storage; };
struct odt_opMember { odt_u32 MethodID; void *opam_Object; };

#define ODT_ICA_TARGET     (ODT_TAG_USER + 0x40001UL)
#define ODT_ICA_MAP        (ODT_TAG_USER + 0x40002UL)
#define ODT_ICSPECIAL_CODE (ODT_TAG_USER + 0x40003UL)
#define ODT_ICTARGET_IDCMP (~0UL)

/* ---- intuition: boxes, gadgets, GadgetInfo, input ------------------------------ */

struct odt_IBox { odt_s16 Left, Top, Width, Height; };
struct odt_Rectangle { odt_s16 MinX, MinY, MaxX, MaxY; };
struct odt_Point { odt_s16 x, y; };

struct odt_Gadget {
    struct odt_Gadget *NextGadget;
    odt_s16 LeftEdge, TopEdge, Width, Height;
    odt_u16 Flags, Activation, GadgetType;
    void *GadgetRender, *SelectRender;
    void *GadgetText;
    odt_s32 MutualExclude;
    void *SpecialInfo;                  /* a datatype object's: its struct odt_DTSpecialInfo */
    odt_u16 GadgetID;
    void *UserData;
};
ODT_STATIC_ASSERT(sizeof(struct odt_Gadget) == 44, "Gadget");
struct odt_ExtGadget {
    struct odt_Gadget g;
    odt_u32 MoreFlags;
    odt_s16 BoundsLeftEdge, BoundsTopEdge, BoundsWidth, BoundsHeight;
};
ODT_STATIC_ASSERT(sizeof(struct odt_ExtGadget) == 56, "ExtGadget");

#define ODT_GFLG_RELBOTTOM  0x0008
#define ODT_GFLG_RELRIGHT   0x0010
#define ODT_GFLG_RELWIDTH   0x0020
#define ODT_GFLG_RELHEIGHT  0x0040
#define ODT_GFLG_SELECTED   0x0080
#define ODT_GFLG_DISABLED   0x0100
#define ODT_GFLG_RELSPECIAL 0x4000
#define ODT_GFLG_EXTENDED   0x8000
#define ODT_GACT_RELVERIFY  0x0001
#define ODT_GACT_IMMEDIATE  0x0002
#define ODT_GACT_FOLLOWMOUSE 0x0008
#define ODT_GTYP_GZZGADGET  0x2000
#define ODT_GTYP_CUSTOMGADGET 0x0005

/* intuition/cghooks.h */
struct odt_GadgetInfo {
    void *gi_Screen, *gi_Window, *gi_Requester, *gi_RastPort, *gi_Layer;
    struct odt_IBox gi_Domain;
    odt_u8 gi_DetailPen, gi_BlockPen;
    void *gi_DrInfo;
    odt_u32 gi_Reserved[6];
};
ODT_STATIC_ASSERT(sizeof(struct odt_GadgetInfo) == 58, "GadgetInfo");

/* devices/inputevent.h, the mouse's form */
struct odt_InputEvent {
    struct odt_InputEvent *ie_NextEvent;
    odt_u8 ie_Class, ie_SubClass;
    odt_u16 ie_Code, ie_Qualifier;
    odt_s16 ie_PosX, ie_PosY;          /* ie_X, ie_Y: devices/inputevent.h makes those macros */
    odt_u32 ie_Secs, ie_Micro;
};
ODT_STATIC_ASSERT(sizeof(struct odt_InputEvent) == 22, "InputEvent");
#define ODT_IECLASS_RAWKEY   0x01
#define ODT_IECLASS_RAWMOUSE 0x02
#define ODT_IECLASS_TIMER    0x06
#define ODT_IECODE_UP_PREFIX 0x80
#define ODT_IECODE_LBUTTON   0x68
#define ODT_IECODE_RBUTTON   0x69
#define ODT_IECODE_NOBUTTON  0xFF
#define ODT_SELECTDOWN       ODT_IECODE_LBUTTON
#define ODT_SELECTUP         (ODT_IECODE_LBUTTON | ODT_IECODE_UP_PREFIX)
#define ODT_MENUDOWN         ODT_IECODE_RBUTTON
#define ODT_MENUUP           (ODT_IECODE_RBUTTON | ODT_IECODE_UP_PREFIX)

/* intuition/gadgetclass.h */
#define ODT_GA_Dummy        (ODT_TAG_USER + 0x30000UL)
#define ODT_GA_Left         (ODT_GA_Dummy + 0x01)
#define ODT_GA_RelRight     (ODT_GA_Dummy + 0x02)
#define ODT_GA_Top          (ODT_GA_Dummy + 0x03)
#define ODT_GA_RelBottom    (ODT_GA_Dummy + 0x04)
#define ODT_GA_Width        (ODT_GA_Dummy + 0x05)
#define ODT_GA_RelWidth     (ODT_GA_Dummy + 0x06)
#define ODT_GA_Height       (ODT_GA_Dummy + 0x07)
#define ODT_GA_RelHeight    (ODT_GA_Dummy + 0x08)
#define ODT_GA_Disabled     (ODT_GA_Dummy + 0x0E)
#define ODT_GA_ID           (ODT_GA_Dummy + 0x10)
#define ODT_GA_UserData     (ODT_GA_Dummy + 0x11)
#define ODT_GA_SpecialInfo  (ODT_GA_Dummy + 0x12)
#define ODT_GA_Selected     (ODT_GA_Dummy + 0x13)
#define ODT_GA_Immediate    (ODT_GA_Dummy + 0x15)
#define ODT_GA_RelVerify    (ODT_GA_Dummy + 0x16)
#define ODT_GA_FollowMouse  (ODT_GA_Dummy + 0x17)
#define ODT_GA_GZZGadget    (ODT_GA_Dummy + 0x0F)
#define ODT_GA_DrawInfo     (ODT_GA_Dummy + 0x21)
#define ODT_GA_RelSpecial   (ODT_GA_Dummy + 0x27)

#define ODT_GM_HITTEST     0UL
#define ODT_GM_RENDER      1UL
#define ODT_GM_GOACTIVE    2UL
#define ODT_GM_HANDLEINPUT 3UL
#define ODT_GM_GOINACTIVE  4UL
#define ODT_GM_HELPTEST    5UL
#define ODT_GM_LAYOUT      6UL
#define ODT_GM_DOMAIN      7UL

#define ODT_GMR_GADGETHIT  0x00000004UL
#define ODT_GMR_MEACTIVE   0UL
#define ODT_GMR_NOREUSE    (1UL << 1)
#define ODT_GMR_REUSE      (1UL << 2)
#define ODT_GMR_VERIFY     (1UL << 3)
#define ODT_GREDRAW_TOGGLE 0UL
#define ODT_GREDRAW_REDRAW 1UL
#define ODT_GREDRAW_UPDATE 2UL

struct odt_gpHitTest  { odt_u32 MethodID; struct odt_GadgetInfo *gpht_GInfo; struct odt_Point gpht_Mouse; };
struct odt_gpRender   { odt_u32 MethodID; struct odt_GadgetInfo *gpr_GInfo; void *gpr_RPort; odt_s32 gpr_Redraw; };
struct odt_gpInput {
    odt_u32 MethodID;
    struct odt_GadgetInfo *gpi_GInfo;
    struct odt_InputEvent *gpi_IEvent;
    odt_s32 *gpi_Termination;
    struct odt_Point gpi_Mouse;         /* relative to the gadget's box */
    void *gpi_TabletData;
};
struct odt_gpGoInactive { odt_u32 MethodID; struct odt_GadgetInfo *gpgi_GInfo; odt_u32 gpgi_Abort; };
struct odt_gpLayout     { odt_u32 MethodID; struct odt_GadgetInfo *gpl_GInfo; odt_u32 gpl_Initial; };
ODT_STATIC_ASSERT(sizeof(struct odt_gpInput) == 24, "gpInput");
ODT_STATIC_ASSERT(sizeof(struct odt_gpLayout) == 12, "gpLayout");

/* ---- datatypes/datatypesclass.h ------------------------------------------------ */

#define ODT_DATATYPESCLASS "datatypesclass"

#define ODT_DTA_Dummy          (ODT_TAG_USER + 0x1000UL)
#define ODT_DTA_TextAttr       (ODT_DTA_Dummy + 10)  /* struct TextAttr * */
#define ODT_DTA_TopVert        (ODT_DTA_Dummy + 11)
#define ODT_DTA_VisibleVert    (ODT_DTA_Dummy + 12)
#define ODT_DTA_TotalVert      (ODT_DTA_Dummy + 13)
#define ODT_DTA_VertUnit       (ODT_DTA_Dummy + 14)
#define ODT_DTA_TopHoriz       (ODT_DTA_Dummy + 15)
#define ODT_DTA_VisibleHoriz   (ODT_DTA_Dummy + 16)
#define ODT_DTA_TotalHoriz     (ODT_DTA_Dummy + 17)
#define ODT_DTA_HorizUnit      (ODT_DTA_Dummy + 18)
#define ODT_DTA_NodeName       (ODT_DTA_Dummy + 19)
#define ODT_DTA_Title          (ODT_DTA_Dummy + 20)
#define ODT_DTA_TriggerMethods (ODT_DTA_Dummy + 21)
#define ODT_DTA_Data           (ODT_DTA_Dummy + 22)
#define ODT_DTA_TextFont       (ODT_DTA_Dummy + 23)
#define ODT_DTA_Methods        (ODT_DTA_Dummy + 24)
#define ODT_DTA_PrinterStatus  (ODT_DTA_Dummy + 25)
#define ODT_DTA_PrinterProc    (ODT_DTA_Dummy + 26)
#define ODT_DTA_LayoutProc     (ODT_DTA_Dummy + 27)
#define ODT_DTA_Busy           (ODT_DTA_Dummy + 28)
#define ODT_DTA_Sync           (ODT_DTA_Dummy + 29)
#define ODT_DTA_BaseName       (ODT_DTA_Dummy + 30)
#define ODT_DTA_GroupID        (ODT_DTA_Dummy + 31)
#define ODT_DTA_ErrorLevel     (ODT_DTA_Dummy + 32)
#define ODT_DTA_ErrorNumber    (ODT_DTA_Dummy + 33)
#define ODT_DTA_ErrorString    (ODT_DTA_Dummy + 34)
#define ODT_DTA_Conductor      (ODT_DTA_Dummy + 35)
#define ODT_DTA_ControlPanel   (ODT_DTA_Dummy + 36)
#define ODT_DTA_Immediate      (ODT_DTA_Dummy + 37)
#define ODT_DTA_Repeat         (ODT_DTA_Dummy + 38)
#define ODT_DTA_SourceAddress  (ODT_DTA_Dummy + 39)  /* V44 */
#define ODT_DTA_SourceSize     (ODT_DTA_Dummy + 40)  /* V44 */
#define ODT_DTA_Reserved       (ODT_DTA_Dummy + 41)
#define ODT_DTA_Name           (ODT_DTA_Dummy + 100)
#define ODT_DTA_SourceType     (ODT_DTA_Dummy + 101)
#define ODT_DTA_Handle         (ODT_DTA_Dummy + 102)
#define ODT_DTA_DataType       (ODT_DTA_Dummy + 103)
#define ODT_DTA_Domain         (ODT_DTA_Dummy + 104)
#define ODT_DTA_Left           (ODT_DTA_Dummy + 105) /* the NDK: use GA_Left and the rest */
#define ODT_DTA_Top            (ODT_DTA_Dummy + 106)
#define ODT_DTA_Width          (ODT_DTA_Dummy + 107)
#define ODT_DTA_Height         (ODT_DTA_Dummy + 108)
#define ODT_DTA_ObjName        (ODT_DTA_Dummy + 109)
#define ODT_DTA_ObjAuthor      (ODT_DTA_Dummy + 110)
#define ODT_DTA_ObjAnnotation  (ODT_DTA_Dummy + 111)
#define ODT_DTA_ObjCopyright   (ODT_DTA_Dummy + 112)
#define ODT_DTA_ObjVersion     (ODT_DTA_Dummy + 113)
#define ODT_DTA_ObjectID       (ODT_DTA_Dummy + 114)
#define ODT_DTA_UserData       (ODT_DTA_Dummy + 115)
#define ODT_DTA_FrameInfo      (ODT_DTA_Dummy + 116)
#define ODT_DTA_RelRight       (ODT_DTA_Dummy + 117) /* the NDK: use GA_RelRight and the rest */
#define ODT_DTA_RelBottom      (ODT_DTA_Dummy + 118)
#define ODT_DTA_RelWidth       (ODT_DTA_Dummy + 119)
#define ODT_DTA_RelHeight      (ODT_DTA_Dummy + 120)
#define ODT_DTA_SelectDomain   (ODT_DTA_Dummy + 121)
#define ODT_DTA_TotalPVert     (ODT_DTA_Dummy + 122)
#define ODT_DTA_TotalPHoriz    (ODT_DTA_Dummy + 123)
#define ODT_DTA_NominalVert    (ODT_DTA_Dummy + 124)
#define ODT_DTA_NominalHoriz   (ODT_DTA_Dummy + 125)
#define ODT_DTA_DestCols       (ODT_DTA_Dummy + 400) /* printing */
#define ODT_DTA_DestRows       (ODT_DTA_Dummy + 401)
#define ODT_DTA_Special        (ODT_DTA_Dummy + 402)
#define ODT_DTA_RastPort       (ODT_DTA_Dummy + 403)
#define ODT_DTA_ARexxPortName  (ODT_DTA_Dummy + 404)

/* DTA_ErrorLevel's levels (the NDK has DTERROR_ numbers for DTA_ErrorNumber) */

/* Hung on the gadget's SpecialInfo: the scroll numbers, and the lock a
 * layout holds. */
struct odt_DTSpecialInfo {
    struct odt_SignalSemaphore si_Lock;
    odt_u32 si_Flags;
    odt_s32 si_TopVert, si_VisVert, si_TotVert, si_OTopVert, si_VertUnit;
    odt_s32 si_TopHoriz, si_VisHoriz, si_TotHoriz, si_OTopHoriz, si_HorizUnit;
};
ODT_STATIC_ASSERT(sizeof(struct odt_DTSpecialInfo) == 90, "DTSpecialInfo");
ODT_STATIC_ASSERT(__builtin_offsetof(struct odt_DTSpecialInfo, si_Flags) == 46, "DTSpecialInfo.si_Flags");
#define ODT_DTSIF_LAYOUT     (1UL << 0)
#define ODT_DTSIF_NEWSIZE    (1UL << 1)
#define ODT_DTSIF_DRAGGING   (1UL << 2)
#define ODT_DTSIF_DRAGSELECT (1UL << 3)
#define ODT_DTSIF_HIGHLIGHT  (1UL << 4)
#define ODT_DTSIF_PRINTING   (1UL << 5)
#define ODT_DTSIF_LAYOUTPROC (1UL << 6)

#define ODT_DTM_Dummy           0x600UL
#define ODT_DTM_FRAMEBOX        0x601UL
#define ODT_DTM_PROCLAYOUT      0x602UL
#define ODT_DTM_ASYNCLAYOUT     0x603UL
#define ODT_DTM_REMOVEDTOBJECT  0x604UL
#define ODT_DTM_SELECT          0x605UL
#define ODT_DTM_CLEARSELECTED   0x606UL
#define ODT_DTM_COPY            0x607UL
#define ODT_DTM_PRINT           0x608UL
#define ODT_DTM_ABORTPRINT      0x609UL
#define ODT_DTM_NEWMEMBER       0x610UL
#define ODT_DTM_DISPOSEMEMBER   0x611UL
#define ODT_DTM_GOTO            0x630UL
#define ODT_DTM_TRIGGER         0x631UL
#define ODT_DTM_OBTAINDRAWINFO  0x640UL
#define ODT_DTM_DRAW            0x641UL
#define ODT_DTM_RELEASEDRAWINFO 0x642UL
#define ODT_DTM_WRITE           0x650UL

struct odt_FrameInfo {
    odt_u32 fri_PropertyFlags;          /* graphics/displayinfo.h DIPF_ */
    struct odt_Point fri_Resolution;
    odt_u8 fri_RedBits, fri_GreenBits, fri_BlueBits;
    struct { odt_u32 Width, Height, Depth; } fri_Dimensions;
    void *fri_Screen;                   /* struct Screen * */
    void *fri_ColorMap;                 /* struct ColorMap * */
    odt_u32 fri_Flags;                  /* FIF_ */
};
ODT_STATIC_ASSERT(sizeof(struct odt_FrameInfo) == 36, "FrameInfo");
ODT_STATIC_ASSERT(__builtin_offsetof(struct odt_FrameInfo, fri_Dimensions) == 12, "FrameInfo.fri_Dimensions");
#define ODT_FIF_SCALABLE   0x1UL
#define ODT_FIF_SCROLLABLE 0x2UL
#define ODT_FIF_REMAPPABLE 0x4UL

struct odt_dtGeneral { odt_u32 MethodID; struct odt_GadgetInfo *dtg_GInfo; };
struct odt_dtSelect  { odt_u32 MethodID; struct odt_GadgetInfo *dts_GInfo; struct odt_Rectangle dts_Select; };
struct odt_dtFrameBox {
    odt_u32 MethodID;
    struct odt_GadgetInfo *dtf_GInfo;
    struct odt_FrameInfo *dtf_ContentsInfo;   /* the object's */
    struct odt_FrameInfo *dtf_FrameInfo;      /* to fill in */
    odt_u32 dtf_SizeFrameInfo;
    odt_u32 dtf_FrameFlags;
};
#define ODT_FRAMEF_SPECIFY (1UL << 0)
struct odt_dtGoto    { odt_u32 MethodID; struct odt_GadgetInfo *dtg_GInfo; char *dtg_NodeName; struct odt_TagItem *dtg_AttrList; };
struct odt_dtTrigger { odt_u32 MethodID; struct odt_GadgetInfo *dtt_GInfo; odt_u32 dtt_Function; void *dtt_Data; };
struct odt_dtPrint   { odt_u32 MethodID; struct odt_GadgetInfo *dtp_GInfo; void *dtp_PIO; struct odt_TagItem *dtp_AttrList; };
struct odt_dtDraw {
    odt_u32 MethodID;
    void *dtd_RPort;
    odt_s32 dtd_Left, dtd_Top, dtd_Width, dtd_Height;
    odt_s32 dtd_TopHoriz, dtd_TopVert;
    struct odt_TagItem *dtd_AttrList;
};
struct odt_dtReleaseDrawInfo { odt_u32 MethodID; void *dtr_Handle; };
struct odt_dtWrite {
    odt_u32 MethodID;
    struct odt_GadgetInfo *dtw_GInfo;
    odt_u32 dtw_FileHandle;             /* BPTR */
    odt_u32 dtw_Mode;                   /* DTWM_ */
    struct odt_TagItem *dtw_AttrList;
};
#define ODT_DTWM_IFF 0UL
#define ODT_DTWM_RAW 1UL
ODT_STATIC_ASSERT(sizeof(struct odt_dtSelect) == 16, "dtSelect");
ODT_STATIC_ASSERT(sizeof(struct odt_dtFrameBox) == 24, "dtFrameBox");
ODT_STATIC_ASSERT(sizeof(struct odt_dtDraw) == 36, "dtDraw");
ODT_STATIC_ASSERT(sizeof(struct odt_dtWrite) == 20, "dtWrite");
ODT_STATIC_ASSERT(sizeof(struct odt_dtPrint) == 16, "dtPrint");

/* DTA_PrinterStatus: the printer.device's errors (devices/printer.h),
 * PDERR_NOERR 0 when done, PDERR_CANCEL 1 when aborted */
#define ODT_PDERR_NOERR  0UL
#define ODT_PDERR_CANCEL 1UL

/* ---- what a class core asks of the system ------------------------------------- */

/* One env serves datatypesclass and every datatype class. The library and
 * class shells fill it from the real libraries; a qemu test fills it with
 * a mock. Every call takes the env first; ctx is the filler's own. A NULL
 * call means the service isn't there: the core treats it as a call that
 * failed (or, for drawing, drew nothing).
 *
 * Handles: a "fh" is a DOS file handle (BPTR as a number), an "iff" a
 * struct IFFHandle *, an "rp" a struct RastPort *; the core never looks
 * inside them. */
struct odt_cenv;
typedef odt_u32 (*odt_dispatch_fn)(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);

struct odt_cenv {
    void *ctx;
    /* the class datatypesclass is; ODT_DTINST finds its instance */
    struct odt_IClass *dtclass;

    /* BOOPSI: amiga.lib's DoMethodA, DoSuperMethodA, CoerceMethodA;
     * NewObjectA (a class pointer, or NULL and a public name) and
     * DisposeObject */
    odt_u32 (*do_method)(const struct odt_cenv *env, void *obj, void *msg);
    odt_u32 (*do_super)(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
    odt_u32 (*coerce)(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
    void *(*new_object)(const struct odt_cenv *env, struct odt_IClass *cl, const char *id, struct odt_TagItem *tags);
    void (*dispose_object)(const struct odt_cenv *env, void *obj);

    /* memory: AllocVec(n, MEMF_ANY | MEMF_CLEAR) and FreeVec (NULL is fine) */
    void *(*alloc)(const struct odt_cenv *env, odt_u32 n);
    void (*free)(const struct odt_cenv *env, void *p);

    /* exec's semaphores, on a SignalSemaphore (si_Lock) */
    void (*sem_init)(const struct odt_cenv *env, struct odt_SignalSemaphore *s);
    void (*sem_obtain)(const struct odt_cenv *env, struct odt_SignalSemaphore *s);
    void (*sem_obtain_shared)(const struct odt_cenv *env, struct odt_SignalSemaphore *s);
    int (*sem_attempt)(const struct odt_cenv *env, struct odt_SignalSemaphore *s);   /* 1 got it */
    void (*sem_release)(const struct odt_cenv *env, struct odt_SignalSemaphore *s);

    /* DOS: IoErr and SetIoErr; Open (MODE_OLDFILE 1005, MODE_NEWFILE 1006),
     * Close, Read, Write, Seek (OFFSET_BEGINNING -1, CURRENT 0, END 1),
     * each answering as dos.library does */
    odt_s32 (*io_err)(const struct odt_cenv *env);
    void (*set_io_err)(const struct odt_cenv *env, odt_s32 code);
    odt_u32 (*open)(const struct odt_cenv *env, const char *name, odt_s32 mode);
    void (*close)(const struct odt_cenv *env, odt_u32 fh);
    odt_s32 (*read)(const struct odt_cenv *env, odt_u32 fh, void *buf, odt_s32 n);
    odt_s32 (*write)(const struct odt_cenv *env, odt_u32 fh, const void *buf, odt_s32 n);
    odt_s32 (*seek)(const struct odt_cenv *env, odt_u32 fh, odt_s32 off, odt_s32 mode);

    /* iffparse.library on an IFFHandle the env made (iff_open_*), each as
     * iffparse.library answers (0 or IFFERR_). iff_close closes and frees
     * it and the stream under it. */
    void *(*iff_open_dos)(const struct odt_cenv *env, odt_u32 fh, int write);  /* InitIFFasDOS */
    void *(*iff_open_clip)(const struct odt_cenv *env, odt_u32 unit, int write); /* InitIFFasClip */
    void *(*iff_open_mem)(const struct odt_cenv *env, const void *at, odt_u32 size); /* read only */
    void (*iff_close)(const struct odt_cenv *env, void *iff);
    odt_s32 (*iff_parse)(const struct odt_cenv *env, void *iff, odt_s32 mode);  /* IFFPARSE_ */
    odt_s32 (*iff_prop)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id);
    odt_s32 (*iff_stop)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id);
    odt_s32 (*iff_stop_on_exit)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id);
    odt_s32 (*iff_collection)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id);
    /* FindProp: the stored chunk's data and size, NULL if none */
    const void *(*iff_find_prop)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id, odt_s32 *size);
    /* CurrentChunk: its ID, type and size (0 when there is none) */
    odt_u32 (*iff_current)(const struct odt_cenv *env, void *iff, odt_u32 *type, odt_s32 *size);
    odt_s32 (*iff_read_chunk)(const struct odt_cenv *env, void *iff, void *buf, odt_s32 n);
    odt_s32 (*iff_push)(const struct odt_cenv *env, void *iff, odt_u32 type, odt_u32 id, odt_s32 size);
    odt_s32 (*iff_write_chunk)(const struct odt_cenv *env, void *iff, const void *buf, odt_s32 n);
    odt_s32 (*iff_pop)(const struct odt_cenv *env, void *iff);

    /* Drawing, the little datatypesclass does itself: ObtainGIRPort and
     * ReleaseGIRPort; a complemented box outline (drag select's band,
     * both corners inside) */
    void *(*obtain_rp)(const struct odt_cenv *env, struct odt_GadgetInfo *gi);
    void (*release_rp)(const struct odt_cenv *env, void *rp);
    void (*complement_box)(const struct odt_cenv *env, void *rp, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);

    /* The layout and printing processes (DoAsyncLayout, PrintDTObjectA):
     * start one that runs odt_layout_run (or odt_print_run) on obj with a
     * copy of msg, answering the process or NULL; ask one to stop
     * (SIGBREAKF_CTRL_C); and, from inside a class's DTM_ASYNCLAYOUT or
     * DTM_PRINT, whether the running process has been asked to stop.
     * start_process answers before the new process does anything else
     * (the core records it in DTA_LayoutProc or DTA_PrinterProc first),
     * but the process runs odt_proc_hold first and start_process answers
     * only once it has (OM_DISPOSE then waits for the process). */
    void *(*start_process)(const struct odt_cenv *env, int printing, void *obj, void *win, void *req, void *msg);
    void (*signal_stop)(const struct odt_cenv *env, void *proc);
    int (*check_stop)(const struct odt_cenv *env);

    /* GetDTString, through sys/datatypes.catalog */
    const char *(*dt_string)(const struct odt_cenv *env, odt_u32 id);

    /* A class's own services (picture.datatype's graphics, sound's audio,
     * text's fonts): each class family defines the struct it points at */
    void *class_ext;

    /* UnLock, for DTA_Handle's file lock the object takes over; optional */
    void (*unlock)(const struct odt_cenv *env, odt_u32 lock);

    /* FindTask(NULL): which task runs now (a dispose from inside a
     * process's completion notification waits for it to unwind); optional */
    void *(*this_task)(const struct odt_cenv *env);
};

/* ---- datatypesclass's instance (private to datatypesclass and the library) ---- */

/* The source datatypesclass opened at OM_NEW, and what DTA_Handle gives */
#define ODT_HANDLE_NONE 0               /* DTST_RAM, or nothing opened */
#define ODT_HANDLE_FILE 1               /* a DOS file handle (a non-IFF type's file) */
#define ODT_HANDLE_IFF  2               /* an IFFHandle on a file, the clipboard or memory */
#define ODT_HANDLE_GIVEN 3              /* the program's own IFFHandle (DTA_Handle, DTST_CLIPBOARD): not ours to close */

struct odt_dtinst {
    struct odt_DTSpecialInfo si;        /* G(o)->SpecialInfo */
    odt_u16 handle_kind;                /* ODT_HANDLE_ */
    odt_u32 source_type;                /* DTA_SourceType: DTST_ */
    void *handle;                       /* DTA_Handle: fh as a pointer, or the IFFHandle */
    odt_u32 fh;                         /* the file under an IFF handle we opened, or 0 */
    char *name;                         /* DTA_Name, a copy */
    struct odt_DataType *datatype;      /* DTA_DataType: the library's, held by the object */
    const void *source_address;         /* DTA_SourceAddress (DTST_MEMORY) */
    odt_u32 source_size;                /* DTA_SourceSize */
    struct odt_IBox domain;             /* DTA_Domain: the box at the last layout */
    struct odt_IBox select_domain;      /* DTA_SelectDomain */
    struct odt_Rectangle select;        /* the selected rectangle, in the object's own units */
    struct odt_FrameInfo frame;         /* DTA_FrameInfo (DTM_FRAMEBOX's answer) */
    char *obj_name, *obj_author, *obj_annotation, *obj_copyright, *obj_version; /* copies */
    odt_u32 object_id;                  /* DTA_ObjectID */
    odt_u32 user_data;                  /* DTA_UserData */
    void *text_attr;                    /* DTA_TextAttr */
    char *title;                        /* DTA_Title */
    char *node_name;                    /* DTA_NodeName */
    odt_s32 total_pvert, total_phoriz;  /* DTA_TotalPVert, DTA_TotalPHoriz */
    odt_s32 nominal_vert, nominal_horiz;/* DTA_NominalVert, DTA_NominalHoriz */
    odt_u32 error_level, error_number;  /* the last load error, as notified */
    void *layout_proc;                  /* DTA_LayoutProc while one runs */
    void *printer_proc;                 /* DTA_PrinterProc while one runs */
    odt_u32 printer_status;             /* DTA_PrinterStatus */
    odt_u32 dest_cols, dest_rows;       /* DTA_DestCols, DTA_DestRows (printing) */
    odt_u32 special;                    /* DTA_Special (printing) */
    void *rastport;                     /* DTA_RastPort (printing) */
    char *arexx_port;                   /* DTA_ARexxPortName, a copy */
    odt_u16 flags;                      /* ODT_DTF_ below */
    /* drag scrolling and drag selecting, between GM_GOACTIVE and GM_GOINACTIVE */
    odt_s16 drag_x, drag_y;             /* where the button went down (gadget-relative) */
    odt_s16 drag_lx, drag_ly;           /* the last mouse position */
    odt_s32 drag_top_vert, drag_top_horiz; /* si_Top* when it went down */
    /* what the library keeps with an object */
    void *window, *requester;           /* AddDTObject's */
    void *class_library;                /* the class library NewDTObjectA opened for it */
    struct odt_SignalSemaphore proc_lock; /* held shared by the layout and print processes */
    odt_u32 lock;                       /* DTA_Handle's file lock (DTST_FILE): the object's, UnLocked with it */
    void *notifying;                    /* the processes inside their completion notification */
    odt_u16 procs;                      /* layout and print processes started and not yet ended */
    odt_u16 dispose_pending;            /* a dispose from inside a completion notification (dtclass_core.c) */
    struct odt_IClass *dispose_cl;      /* its OM_DISPOSE's class */
    void *dispose_owner;                /* the task disposing it, once one is */
};
#define ODT_DTF_BUSY      0x0001        /* DTA_Busy */
#define ODT_DTF_SYNC      0x0002        /* DTA_Sync: laid out and current */
#define ODT_DTF_IMMEDIATE 0x0004        /* DTA_Immediate */
#define ODT_DTF_REPEAT    0x0008        /* DTA_Repeat */
#define ODT_DTF_PANEL     0x0010        /* DTA_ControlPanel */
#define ODT_DTF_SELECTED  0x0020        /* something is selected (select is valid) */
#define ODT_DTF_BAND      0x0040        /* the drag select band is drawn */
#define ODT_DTF_ADDED     0x0080        /* in a window (AddDTObject) */

#define ODT_DTINST(env, obj) ((struct odt_dtinst *)ODT_INST_DATA((env)->dtclass, (obj)))

/* ---- datatypesclass's core (dtclass_core.c) ------------------------------------- */

/* datatypesclass's dispatcher, the class made under gadgetclass with
 * odt_dtclass_inst_size() bytes of instance */
odt_u32 odt_dtclass_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
odt_u32 odt_dtclass_inst_size(void);

/* What a layout process runs (DoAsyncLayout): the object's DTM_ASYNCLAYOUT
 * under si_Lock, then the notification; and a printing process's
 * DTM_PRINT (PrintDTObjectA). Each answers the method's result. */
/* the process's work; *dispose (may be NULL) is set when DisposeDTObject
 * came from inside a completion notification (odt_dispose_later) and this
 * is the object's last process, for the caller to do now: all of it
 * (ODT_DISPOSE_ALL), or only the library's record when OM_DISPOSE came
 * too and the object has gone already (ODT_DISPOSE_RECORD) */
#define ODT_DISPOSE_ALL    1
#define ODT_DISPOSE_RECORD 2
odt_u32 odt_layout_run(const struct odt_cenv *env, void *obj, struct odt_gpLayout *gpl, int *dispose);
void odt_proc_hold(const struct odt_cenv *env, void *obj);
odt_u32 odt_print_run(const struct odt_cenv *env, void *obj, struct odt_dtPrint *msg, int *dispose);
/* DisposeDTObject's first step (dtclass_core.c): 0, the caller disposes
 * it all now; ODT_DISPOSE_RECORD, only the library's record; 1, nothing */
int odt_dispose_later(const struct odt_cenv *env, void *obj);

/* The object half of datatypes.library's calls (the library shell adds
 * Intuition's half: GadgetInfo, AddGList, RemoveGList...) */
odt_u32 odt_do_async_layout(const struct odt_cenv *env, void *obj, struct odt_gpLayout *gpl);  /* DoAsyncLayout */
odt_u32 odt_print_object(const struct odt_cenv *env, void *obj, void *win, void *req,
                         struct odt_dtPrint *msg);                                             /* PrintDTObjectA */
odt_u32 odt_start_drag_select(const struct odt_cenv *env, void *obj);                         /* StartDragSelect */
odt_u32 odt_get_dt_attrs(const struct odt_cenv *env, void *obj, struct odt_TagItem *tags);    /* GetDTAttrsA */
odt_u32 odt_remove_object(const struct odt_cenv *env, void *obj);                             /* RemoveDTObject's DTM_REMOVEDTOBJECT */
/* DTST_RAM: the class a group's objects are made of, NULL for none */
const char *odt_group_base(odt_u32 gid);
/* LaunchToolA's command line: program "project", AmigaDOS quoting; its
 * length, or 0 when it doesn't fit in size */
odt_u32 odt_tool_command(char *buf, odt_u32 size, const char *program, const char *project);

#ifdef __cplusplus
}
#endif

#endif
