/* GFOpenDatatypes core: datatypes.library's work in plain C.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, written from datatypes.library's documented interface (the
 * autodocs and the structures' published layouts) and OS 3.2's answers
 * measured by tests/DatatypesABITest, never from Kickstart, Hyperion or
 * AROS code. Nothing here includes an Amiga header or calls the system, so
 * the same file is built into the library (opendatatypes_lib.c) and into
 * tests/m68k/opendatatypes_test.c, which runs it under qemu-m68k. Memory,
 * name patterns and recognition code come in through struct odt_env.
 *
 * Types follow the 68k ABI: 32-bit long and pointers, big-endian, the
 * structures laid out as the NDK's datatypes/ headers lay them out
 * (ndk_check.c checks every one on the stove). Descriptor files are read
 * byte by byte, so the parser doesn't care about the host's byte order.
 */
#ifndef OPENDATATYPES_CORE_H
#define OPENDATATYPES_CORE_H

typedef unsigned char  odt_u8;
typedef signed char    odt_s8;
typedef unsigned short odt_u16;
typedef signed short   odt_s16;
typedef unsigned long  odt_u32;
typedef signed long    odt_s32;

#define ODT_ID(a, b, c, d) (((odt_u32)(a) << 24) | ((odt_u32)(b) << 16) | ((odt_u32)(c) << 8) | (odt_u32)(d))

/* ---- exec/nodes.h, exec/lists.h, utility/tagitem.h ------------------------ */

struct odt_Node {
    struct odt_Node *ln_Succ, *ln_Pred;
    odt_u8 ln_Type;
    odt_s8 ln_Pri;
    char *ln_Name;
};
struct odt_List {
    struct odt_Node *lh_Head, *lh_Tail, *lh_TailPred;
    odt_u8 lh_Type, l_pad;
};
struct odt_TagItem { odt_u32 ti_Tag, ti_Data; };
#define ODT_TAG_DONE   0UL
#define ODT_TAG_IGNORE 1UL
#define ODT_TAG_MORE   2UL
#define ODT_TAG_SKIP   3UL

/* ---- datatypes/datatypes.h --------------------------------------------------- */

#define ODT_ID_DTYP ODT_ID('D', 'T', 'Y', 'P')
#define ODT_ID_DTHD ODT_ID('D', 'T', 'H', 'D')
#define ODT_ID_CODE ODT_ID('D', 'T', 'C', 'D')
#define ODT_ID_TOOL ODT_ID('D', 'T', 'T', 'L')
#define ODT_ID_TAGS ODT_ID('D', 'T', 'T', 'G')
#define ODT_ID_NAME ODT_ID('N', 'A', 'M', 'E')
#define ODT_ID_FORM ODT_ID('F', 'O', 'R', 'M')
#define ODT_ID_CAT  ODT_ID('C', 'A', 'T', ' ')
#define ODT_ID_LIST ODT_ID('L', 'I', 'S', 'T')

struct odt_DataTypeHeader {
    char *dth_Name;                     /* what it is, "IFF ILBM" */
    char *dth_BaseName;                 /* its class, "ilbm" -> ilbm.datatype */
    char *dth_Pattern;                  /* file name pattern, "#?" is any */
    odt_s16 *dth_Mask;                  /* first bytes; -1 matches any byte */
    odt_u32 dth_GroupID;                /* GID_ */
    odt_u32 dth_ID;                     /* the IFF FORM type, or a made ID */
    odt_s16 dth_MaskLen;                /* words in dth_Mask */
    odt_s16 dth_Pad;
    odt_u16 dth_Flags;                  /* DTF_ */
    odt_u16 dth_Priority;
};
#define ODT_DTF_TYPE_MASK 0x000f
#define ODT_DTF_BINARY    0x0000
#define ODT_DTF_ASCII     0x0001
#define ODT_DTF_IFF       0x0002
#define ODT_DTF_MISC      0x0003
#define ODT_DTF_CASE      0x0010
#define ODT_DTF_SYSTEM1   0x1000

#define ODT_GID_SYSTEM     ODT_ID('s', 'y', 's', 't')
#define ODT_GID_TEXT       ODT_ID('t', 'e', 'x', 't')
#define ODT_GID_DOCUMENT   ODT_ID('d', 'o', 'c', 'u')
#define ODT_GID_SOUND      ODT_ID('s', 'o', 'u', 'n')
#define ODT_GID_INSTRUMENT ODT_ID('i', 'n', 's', 't')
#define ODT_GID_MUSIC      ODT_ID('m', 'u', 's', 'i')
#define ODT_GID_PICTURE    ODT_ID('p', 'i', 'c', 't')
#define ODT_GID_ANIMATION  ODT_ID('a', 'n', 'i', 'm')
#define ODT_GID_MOVIE      ODT_ID('m', 'o', 'v', 'i')

struct odt_DTHookContext {
    void *dthc_SysBase, *dthc_DOSBase, *dthc_IFFParseBase, *dthc_UtilityBase;
    odt_u32 dthc_Lock;                  /* BPTR */
    void *dthc_FIB;
    odt_u32 dthc_FileHandle;            /* BPTR */
    void *dthc_IFF;
    char *dthc_Buffer;
    odt_u32 dthc_BufferLength;
};

struct odt_Tool {
    odt_u16 tn_Which;                   /* TW_ */
    odt_u16 tn_Flags;                   /* TF_ */
    char *tn_Program;
};
#define ODT_TW_INFO   1
#define ODT_TW_BROWSE 2
#define ODT_TW_EDIT   3
#define ODT_TW_PRINT  4
#define ODT_TW_MAIL   5
#define ODT_TF_LAUNCH_MASK 0x000f
#define ODT_TF_SHELL       0x0001
#define ODT_TF_WORKBENCH   0x0002
#define ODT_TF_RX          0x0003

struct odt_DataType {
    struct odt_Node dtn_Node1;          /* system use */
    struct odt_Node dtn_Node2;          /* system use */
    struct odt_DataTypeHeader *dtn_Header;
    struct odt_List dtn_ToolList;       /* odt_ToolNode */
    char *dtn_FunctionName;
    struct odt_TagItem *dtn_AttrList;
    odt_u32 dtn_Length;                 /* of the whole memory block */
};

struct odt_ToolNode {
    struct odt_Node tn_Node;
    struct odt_Tool tn_Tool;
    odt_u32 tn_Length;                  /* of the whole memory block */
};

/* DTST_ for ObtainDataTypeA */
#define ODT_DTST_RAM       1
#define ODT_DTST_FILE      2
#define ODT_DTST_CLIPBOARD 3
#define ODT_DTST_HOTLINK   4
#define ODT_DTST_MEMORY    5

/* DTERROR_ and DTMSG_ */
#define ODT_DTERROR_UNKNOWN_DATATYPE        2000
#define ODT_DTERROR_COULDNT_SAVE            2001
#define ODT_DTERROR_COULDNT_OPEN            2002
#define ODT_DTERROR_COULDNT_SEND_MESSAGE    2003
#define ODT_DTERROR_COULDNT_OPEN_CLIPBOARD  2004
#define ODT_DTERROR_Reserved                2005
#define ODT_DTERROR_UNKNOWN_COMPRESSION     2006
#define ODT_DTERROR_NOT_ENOUGH_DATA         2007
#define ODT_DTERROR_INVALID_DATA            2008
#define ODT_DTERROR_NOT_AVAILABLE           2009
#define ODT_DTMSG_TYPE_OFFSET               2100

/* ---- datatypes/datatypesclass.h: methods and their tables --------------------- */

struct odt_DTMethod {
    char *dtm_Label;
    char *dtm_Command;
    odt_u32 dtm_Method;
};

#define ODT_STM_PAUSE          1
#define ODT_STM_PLAY           2
#define ODT_STM_CONTENTS       3
#define ODT_STM_INDEX          4
#define ODT_STM_RETRACE        5
#define ODT_STM_BROWSE_PREV    6
#define ODT_STM_BROWSE_NEXT    7
#define ODT_STM_NEXT_FIELD     8
#define ODT_STM_PREV_FIELD     9
#define ODT_STM_ACTIVATE_FIELD 10
#define ODT_STM_COMMAND        11
#define ODT_STM_REWIND         12
#define ODT_STM_FASTFORWARD    13
#define ODT_STM_STOP           14
#define ODT_STM_RESUME         15
#define ODT_STM_LOCATE         16
#define ODT_STM_HELP           17
#define ODT_STM_SEARCH         18
#define ODT_STM_SEARCH_NEXT    19
#define ODT_STM_SEARCH_PREV    20
#define ODT_STM_USER           100

#define ODT_STMF_METHOD_MASK 0x0000ffffUL
#define ODT_STMF_DATA_MASK   0x00ff0000UL
#define ODT_STMF_RESERVED_MASK 0xff000000UL
#define ODT_STMD_VOID    0x00010000UL
#define ODT_STMD_ULONG   0x00020000UL
#define ODT_STMD_STRPTR  0x00030000UL
#define ODT_STMD_TAGLIST 0x00040000UL

/* FindToolNodeA's tags (datatypes/datatypes.h) */
#define ODT_TOOLA_Dummy      0x80000000UL
#define ODT_TOOLA_Program    (ODT_TOOLA_Dummy + 1)
#define ODT_TOOLA_Which      (ODT_TOOLA_Dummy + 2)
#define ODT_TOOLA_LaunchType (ODT_TOOLA_Dummy + 3)

/* ---- what the core needs from the system ------------------------------------ */

struct odt_entry;

struct odt_env {
    void *ctx;
    /* memory: AllocVec(n, MEMF_ANY | MEMF_CLEAR) and FreeVec */
    void *(*alloc)(void *ctx, odt_u32 n);
    void (*free)(void *ctx, void *p);
    /* AmigaDOS name patterns, without case. parse_name, as
     * ParsePatternNoCase: pattern's token into tok (size bytes, twice the
     * pattern and 2), answering 1 when it has wildcards, 0 when it has none
     * (the core then compares the pattern itself as OS 3.2 does) and -1
     * when it can't be parsed (it fits no name). match_name, as
     * MatchPatternNoCase: does name fit the token, 1 or 0. The core parses
     * each entry's pattern once, the first time recognition asks it, and
     * keeps the token with the entry. */
    int (*parse_name)(void *ctx, const char *pattern, char *tok, odt_u32 size);
    int (*match_name)(void *ctx, const char *tok, const char *name);
    /* run an entry's recognition code (its DTCD) on the context: 1 yes, 0 no */
    int (*run_hook)(void *ctx, struct odt_entry *e, struct odt_DTHookContext *dthc);
};

/* ---- the descriptor database --------------------------------------------- */

/* One loaded descriptor: the public DataType first, then what the library
 * keeps for itself, then the header, strings, mask and tools in the same
 * memory block (dt.dtn_Length long). */
struct odt_entry {
    struct odt_DataType dt;
    struct odt_DataTypeHeader dth;
    odt_u32 uses;                       /* ObtainDataType/LockDataType less ReleaseDataType */
    const odt_u8 *code;                 /* the DTCD chunk, when there is one */
    odt_u32 code_len;
    void *seg;                          /* its loaded code (the library's to load and free) */
    odt_u32 serial;                     /* order of adding: the newest goes first */
    odt_u8 builtin;                     /* one of the library's own types */
    odt_u8 pat_state;                   /* dth_Pattern: ODT_PAT_ */
    char *pat_tok;                      /* its token (ODT_PAT_WILD), the core's to free */
    struct odt_List tools;              /* its DTTL tools (OS 3.2 leaves dtn_ToolList empty) */
};

/* The lists, one for each kind of data (DTF_TYPE_MASK), each in the order
 * recognition tries it. */
struct odt_db {
    struct odt_List kind[4];            /* binary, ascii, iff, misc; linked through dtn_Node1 */
    struct odt_List all;                /* every entry, through dtn_Node2 */
    odt_u32 serial;
};

/* odt_entry.pat_state: the pattern not parsed yet; parsed, with wildcards;
 * without wildcards (compared as it is); not a pattern (fits nothing);
 * "#?" (fits every name, "" too: no token, no matching) */
#define ODT_PAT_NEW     0
#define ODT_PAT_WILD    1
#define ODT_PAT_LITERAL 2
#define ODT_PAT_BAD     3
#define ODT_PAT_ANY     4

/* Descriptor parse results */
#define ODT_OK          0
#define ODT_ERR_FORM    1               /* not a FORM DTYP */
#define ODT_ERR_NOHEAD  2               /* no DTHD */
#define ODT_ERR_BAD     3               /* a chunk points outside itself */
#define ODT_ERR_MEMORY  4

void odt_list_init(struct odt_List *l);
void odt_db_init(struct odt_db *db);

/* Reads one descriptor file (a FORM DTYP, len bytes) into a new entry. */
int odt_parse(const struct odt_env *env, const odt_u8 *form, odt_u32 len, struct odt_entry **out);

/* Makes one of the library's own types (no descriptor file). */
struct odt_entry *odt_builtin(const struct odt_env *env, const char *name, const char *base,
                              odt_u32 group, odt_u32 id, odt_u16 flags, odt_u16 pri);

/* Adds e in recognition order; an entry of the same name already there is
 * replaced and returned (to be freed once nobody uses it), else NULL. */
struct odt_entry *odt_db_add(struct odt_db *db, struct odt_entry *e);
void odt_db_remove(struct odt_entry *e);
void odt_free_entry(const struct odt_env *env, struct odt_entry *e);

/* An entry by its base name, then by its name, no case (DTST_RAM). */
struct odt_entry *odt_db_find_name(struct odt_db *db, const char *name);

/* ---- recognition ------------------------------------------------------------ */

/* What kind of data the first bytes are: DTF_IFF, DTF_ASCII or DTF_BINARY.
 * For IFF, *form gets the FORM (or CAT/LIST contents) type. */
odt_u16 odt_classify(const odt_u8 *buf, odt_u32 len, odt_u32 *form);

/* Do the first bytes fit the entry's mask? */
int odt_mask_match(const struct odt_DataTypeHeader *h, const odt_u8 *buf, odt_u32 len);

/* The entry that recognises the data, or NULL. name is the file's name
 * (NULL when there is none); dthc goes to recognition code as it is. */
struct odt_entry *odt_recognize(const struct odt_env *env, struct odt_db *db,
                                const odt_u8 *buf, odt_u32 len, const char *name,
                                struct odt_DTHookContext *dthc);

/* ---- the V45 method tables -------------------------------------------------- */

odt_u32 *odt_FindMethod(odt_u32 *methods, odt_u32 id);
struct odt_DTMethod *odt_FindTriggerMethod(struct odt_DTMethod *methods, const char *command,
                                           odt_u32 method);
odt_u32 *odt_CopyDTMethods(const struct odt_env *env, const odt_u32 *methods,
                           const odt_u32 *include, const odt_u32 *exclude);
struct odt_DTMethod *odt_CopyDTTriggerMethods(const struct odt_env *env,
                                              const struct odt_DTMethod *methods,
                                              const struct odt_DTMethod *include,
                                              const struct odt_DTMethod *exclude);
odt_u32 odt_GetDTTriggerMethodDataFlags(odt_u32 method);

/* ---- tools and strings ------------------------------------------------------ */

struct odt_ToolNode *odt_FindToolNodeA(struct odt_List *tools, const struct odt_TagItem *tags);
const char *odt_GetDTString(odt_u32 id);     /* NULL for an id it has no string for */

/* utility.library's NextTagItem, for the core's own tag lists */
const struct odt_TagItem *odt_next_tag(const struct odt_TagItem **tp);
int odt_stricmp(const char *a, const char *b);

#endif
