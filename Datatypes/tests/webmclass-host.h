/*
 * The AmigaOS, datatypes and libvpx names webm/webmclass.c uses, for
 * building the class itself on x86 or ARM64 cores (tests/webmsoundtest.c,
 * through tests/test-webm-read.sh, which points each Amiga header at this
 * one). Types are the Amiga's, with ULONG an unsigned long so a pointer
 * fits in one as on the Amiga; the functions are the test's fakes.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef WEBMCLASS_HOST_H
#define WEBMCLASS_HOST_H

#include <stddef.h>
#include <stdint.h>

/* exec/types.h */
#define EXEC_TYPES_H
typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef short BOOL;
typedef void *APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
typedef long BPTR;
typedef ULONG Tag;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* exec */
#define MEMF_ANY 0UL
#define MEMF_CHIP 2UL
#define MEMF_CLEAR 0x10000UL
#define CMD_NONSTD 9
struct MsgPort { int mp_Dummy; };
struct IORequest { struct MsgPort *io_ReplyPort; UWORD io_Command; BYTE io_Error; };
struct SignalSemaphore { int ss_Count; };
APTR AllocVec(ULONG size, ULONG flags);
void FreeVec(APTR p);
void InitSemaphore(struct SignalSemaphore *s);
void ObtainSemaphore(struct SignalSemaphore *s);
void ReleaseSemaphore(struct SignalSemaphore *s);

/* dos */
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT 0
#define OFFSET_END 1
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_SEEK_ERROR 219
LONG Seek(BPTR file, LONG position, LONG mode);
LONG Read(BPTR file, APTR buffer, LONG length);
void SetIoErr(LONG code);
LONG IoErr(void);
STRPTR FilePart(CONST_STRPTR path);

/* graphics */
#define BMF_CLEAR 1UL
struct BitMap { UWORD BytesPerRow, Rows; };
struct RastPort { struct BitMap *BitMap; };
struct ColorMap;
struct BitMap *AllocBitMap(ULONG width, ULONG height, ULONG depth, ULONG flags, struct BitMap *friend_);
void FreeBitMap(struct BitMap *bm);
void InitRastPort(struct RastPort *rp);
void WriteChunkyPixels(struct RastPort *rp, ULONG x0, ULONG y0, ULONG x1, ULONG y1, UBYTE *array, LONG modulo);
void WaitBlit(void);

/* intuition (BOOPSI) */
typedef ULONG Object;
typedef struct IClass { ULONG cl_InstOffset; ULONG cl_InstSize; } Class;
typedef struct { ULONG MethodID; } *Msg;
#define OM_NEW 0x101UL
#define OM_DISPOSE 0x102UL
#define INST_DATA(cl, o) ((APTR)(((UBYTE *)(o)) + (cl)->cl_InstOffset))
ULONG DoSuperMethodA(Class *cl, Object *o, Msg msg);
ULONG fake_coerce(Class *cl, Object *o, ULONG method);
#define CoerceMethod(cl, o, method) fake_coerce(cl, o, method)

/* datatypes */
#define TAG_DONE 0UL
#define DTA_Dummy 0x80001000UL
#define DTA_SourceType (DTA_Dummy + 1)
#define DTA_Handle (DTA_Dummy + 2)
#define DTA_Name (DTA_Dummy + 100)
#define DTA_ObjName (DTA_Dummy + 101)
#define DTA_NominalHoriz (DTA_Dummy + 104)
#define DTA_NominalVert (DTA_Dummy + 105)
#define DTST_FILE 2UL
#define DTERROR_INVALID_DATA 2005
#define ADTA_Dummy (DTA_Dummy + 600)
#define ADTA_ModeID (ADTA_Dummy + 1)
#define ADTA_KeyFrame (ADTA_Dummy + 2)
#define ADTA_ColorRegisters (ADTA_Dummy + 3)
#define ADTA_CRegs (ADTA_Dummy + 4)
#define ADTA_GRegs (ADTA_Dummy + 5)
#define ADTA_NumColors (ADTA_Dummy + 7)
#define ADTA_Width (ADTA_Dummy + 9)
#define ADTA_Height (ADTA_Dummy + 10)
#define ADTA_Depth (ADTA_Dummy + 11)
#define ADTA_Frames (ADTA_Dummy + 12)
#define ADTA_FramesPerSecond (ADTA_Dummy + 14)
#define ADTA_Period (ADTA_Dummy + 18)
#define ADTA_Volume (ADTA_Dummy + 19)
#define ADTA_Cycles (ADTA_Dummy + 20)
#define ADTM_Dummy 0x700UL
#define ADTM_LOADFRAME (ADTM_Dummy + 1)
#define ADTM_UNLOADFRAME (ADTM_Dummy + 2)
struct ColorRegister { UBYTE red, green, blue; };
struct adtFrame {
    ULONG MethodID;
    ULONG alf_TimeStamp;
    ULONG alf_Frame;
    ULONG alf_Duration;
    struct BitMap *alf_BitMap;
    struct ColorMap *alf_CMap;
    BYTE *alf_Sample;
    ULONG alf_SampleLength;
    ULONG alf_Period;
    APTR alf_UserData;
};
/* Each tag and value converted to a ULONG, as the Amiga's varargs stubs do. */
ULONG fake_getdtattrs(Object *o, const ULONG *tags);
ULONG fake_setdtattrs(Object *o, const ULONG *tags);
#define GetDTAttrs(o, ...) fake_getdtattrs(o, (const ULONG[]){ __VA_ARGS__ })
#define SetDTAttrs(o, win, req, ...) fake_setdtattrs(o, (const ULONG[]){ __VA_ARGS__ })

/* libvpx: decoding only has to succeed; no picture comes out */
#define VPX_PLANE_Y 0
#define VPX_PLANE_U 1
#define VPX_PLANE_V 2
typedef enum { VPX_CS_UNKNOWN = 0, VPX_CS_BT_601 = 1, VPX_CS_SRGB = 7 } vpx_color_space_t;
typedef struct vpx_image {
    vpx_color_space_t cs;
    unsigned int d_w, d_h;
    unsigned int x_chroma_shift, y_chroma_shift;
    unsigned char *planes[4];
    int stride[4];
} vpx_image_t;
typedef struct { int dummy; } vpx_codec_ctx_t;
typedef struct { int dummy; } vpx_codec_iface_t;
typedef const void *vpx_codec_iter_t;
int vpx_codec_dec_init(vpx_codec_ctx_t *ctx, vpx_codec_iface_t *iface, const void *cfg, long flags);
int vpx_codec_decode(vpx_codec_ctx_t *ctx, const uint8_t *data, unsigned int size, void *user, long deadline);
vpx_image_t *vpx_codec_get_frame(vpx_codec_ctx_t *ctx, vpx_codec_iter_t *iter);
int vpx_codec_destroy(vpx_codec_ctx_t *ctx);
vpx_codec_iface_t *vpx_codec_vp8_dx(void);
vpx_codec_iface_t *vpx_codec_vp9_dx(void);

#endif
