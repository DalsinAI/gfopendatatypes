/*
 * The AmigaOS names common/dtservice.c uses, for building it on x86 or
 * ARM64 cores (tests/dtservicetest.c, through tests/test-dtservice.sh, which
 * points each Amiga header dtservice.c includes at this one). The structures
 * are laid out as exec's; the functions are the test's fakes.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef DTSERVICE_HOST_H
#define DTSERVICE_HOST_H

#include <stddef.h>
#include <stdint.h>

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
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define NT_MESSAGE 5
#define NT_REPLYMSG 7
#define NT_MSGPORT 4
#define PA_SIGNAL 0
#define PA_IGNORE 2
#define MEMF_ANY 0UL
#define MEMF_PUBLIC 1UL
#define MEMF_CLEAR 0x10000UL
#define CMD_NONSTD 9
#define IOF_QUICK 1
#define GVF_GLOBAL_ONLY 0x100

struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type; UBYTE l_pad; };
struct Task { struct Node tc_Node; int tc_Id; };
struct MsgPort { struct Node mp_Node; UBYTE mp_Flags; UBYTE mp_SigBit; void *mp_SigTask; struct List mp_MsgList; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct Device;
struct Unit;
struct IORequest {
    struct Message io_Message;
    struct Device *io_Device;
    struct Unit *io_Unit;
    UWORD io_Command;
    UBYTE io_Flags;
    BYTE io_Error;
};

APTR AllocVec(ULONG size, ULONG flags);
void FreeVec(APTR p);
BYTE AllocSignal(LONG bit);
void FreeSignal(LONG bit);
struct Task *FindTask(CONST_STRPTR name);
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size);
void DeleteIORequest(struct IORequest *io);
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags);
void CloseDevice(struct IORequest *io);
BYTE DoIO(struct IORequest *io);
void SendIO(struct IORequest *io);
BYTE WaitIO(struct IORequest *io);
struct IORequest *CheckIO(struct IORequest *io);
LONG AbortIO(struct IORequest *io);
void Disable(void);
void Enable(void);
LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, ULONG flags);

/* dtlib.h's needs */
typedef ULONG Object;
typedef struct IClass { ULONG cl_InstOffset; } Class;
typedef struct { ULONG MethodID; } *Msg;

#endif
