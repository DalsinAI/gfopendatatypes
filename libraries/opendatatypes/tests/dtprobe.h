/* What the test's recognition code writes down each time it runs: the
 * record is the calling task's tc_UserData, set by DatatypesABITest around
 * each ObtainDataTypeA. The code itself (DatatypesABITest.c, probe_code)
 * fills it at these offsets. Copyright (c) 2026 AmiCradle Team. MIT. */
#ifndef DTPROBE_H
#define DTPROBE_H
#define DTPROBE_MAGIC 0x44545048UL      /* 'DTPH' */

struct dtprobe {
    ULONG magic;                        /* +0 */
    ULONG answer;                       /* +4: what the code says, 1 yes, 0 no */
    ULONG calls;                        /* +8 */
    ULONG a0;                           /* +12: A0 on entry */
    ULONG stackarg;                     /* +16: the long at 4(sp) on entry */
    ULONG ctx[10];                      /* +20: 40 bytes from A0, when A0 is an even address */
};
#endif
