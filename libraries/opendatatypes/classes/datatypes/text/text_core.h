/* GFOpenDatatypes text.datatype core, in plain C with no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Clean-room, from the text_dtc autodoc, NDK 3.2's datatypes/textclass.h
 * and datatypesclass.h, the IFF FTXT specification (EA IFF 85) and OS
 * 3.2's answers measured by tests/TextClassABITest; never from
 * Kickstart, Hyperion, AROS or any third-party datatype's code.
 *
 * The class keeps the text (TDTA_Buffer, a copy of its own) and makes the
 * line list (TDTA_LineList: struct Line segments, one per run of text in
 * one style between tabs and line ends), wrapping words to the visible
 * width when TDTA_WordWrap is on. It selects (DTM_SELECT, by character),
 * copies to the clipboard as FTXT (DTM_COPY), writes (DTM_WRITE), finds
 * the word under the mouse (TDTA_WordSelect) and draws the visible rows.
 * The system comes in through struct odt_cenv (dtclass_env.h) and the
 * text family's struct tdt_ext (its class_ext).
 */
#ifndef OPENDATATYPES_TEXT_CORE_H
#define OPENDATATYPES_TEXT_CORE_H

#include "dtc_util.h"

/* ---- datatypes/textclass.h ------------------------------------------------- */

#define TDT_TEXTDTCLASS "text.datatype"
#define TDT_TDTA_Dummy      (ODT_DTA_Dummy + 299)   /* TDTA_Buffer is DTA_Dummy + 300 */
#define TDT_TDTA_Buffer     (TDT_TDTA_Dummy + 1)
#define TDT_TDTA_BufferLen  (TDT_TDTA_Dummy + 2)
#define TDT_TDTA_LineList   (TDT_TDTA_Dummy + 3)
#define TDT_TDTA_WordSelect (TDT_TDTA_Dummy + 4)
#define TDT_TDTA_WordDelim  (TDT_TDTA_Dummy + 5)
#define TDT_TDTA_WordWrap   (TDT_TDTA_Dummy + 6)

struct tdt_Line {                       /* struct Line */
    struct odt_MinNode ln_Link;
    char *ln_Text;
    odt_u32 ln_TextLen;
    odt_u16 ln_XOffset, ln_YOffset, ln_Width, ln_Height, ln_Flags;
    odt_s8 ln_FgPen, ln_BgPen;
    odt_u32 ln_Style;
    void *ln_Data;
};
ODT_STATIC_ASSERT(sizeof(struct tdt_Line) == 36, "Line");
#define TDT_LNF_LF       (1 << 0)
#define TDT_LNF_LINK     (1 << 1)
#define TDT_LNF_OBJECT   (1 << 2)
#define TDT_LNF_SELECTED (1 << 3)

#define TDT_ID_FTXT ODT_ID('F', 'T', 'X', 'T')
#define TDT_ID_CHRS ODT_ID('C', 'H', 'R', 'S')
#define TDT_ID_FORM ODT_ID('F', 'O', 'R', 'M')

/* graphics/text.h soft styles */
#define TDT_FSF_UNDERLINED 1
#define TDT_FSF_BOLD       2
#define TDT_FSF_ITALIC     4

/* intuition/screens.h DrawInfo pens */
#define TDT_TEXTPEN       2
#define TDT_FILLPEN       5
#define TDT_FILLTEXTPEN   6
#define TDT_BACKGROUNDPEN 7

/* ---- the text family's services (odt_cenv.class_ext) ------------------------- */

struct tdt_ext {
    /* fonts: OpenFont (or OpenDiskFont) a TextAttr, CloseFont, the
     * screen's or the system default font (not to be closed) */
    void *(*open_font)(const struct odt_cenv *env, const void *text_attr);
    void (*close_font)(const struct odt_cenv *env, void *font);
    void *(*default_font)(const struct odt_cenv *env, struct odt_GadgetInfo *gi);
    /* tf_YSize, tf_Baseline, tf_XSize */
    void (*font_size)(const struct odt_cenv *env, void *font, odt_u16 *ysize, odt_u16 *baseline, odt_u16 *xsize);
    /* TextLength in a soft style */
    odt_s32 (*text_length)(const struct odt_cenv *env, void *font, odt_u32 style, const char *s, odt_u32 n);
    /* Text at x with its top at y (the baseline below), JAM1 in pen fg */
    void (*text)(const struct odt_cenv *env, void *rp, void *font, odt_u32 style, odt_s32 fg, odt_s32 x, odt_s32 y,
                 const char *s, odt_u32 n);
    void (*fill)(const struct odt_cenv *env, void *rp, odt_s32 pen, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);
    /* a clip region on rp's layer while drawing; NULL when there is none */
    void *(*clip)(const struct odt_cenv *env, void *rp, odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);
    void (*unclip)(const struct odt_cenv *env, void *rp, void *clip);
    /* a DrawInfo pen (TDT_TEXTPEN...) from gi's screen, or its default */
    odt_s32 (*pen)(const struct odt_cenv *env, struct odt_GadgetInfo *gi, odt_u32 which);
    /* intuition's DoubleClick */
    int (*double_click)(const struct odt_cenv *env, odt_u32 s1, odt_u32 m1, odt_u32 s2, odt_u32 m2);
    /* optional: one RastPort for a whole layout's measuring (InitRastPort
     * and SetFont once), TextLength on it in a soft style, and freeing it;
     * without them each width is text_length's own RastPort */
    void *(*measure_open)(const struct odt_cenv *env, void *font);
    odt_s32 (*measure_len)(const struct odt_cenv *env, void *rp, odt_u32 style, const char *s, odt_u32 n);
    void (*measure_close)(const struct odt_cenv *env, void *rp);
};

/* ---- the pure part: layout, selection, copying ------------------------------- */

/* How wide text is in the object's font */
struct tdt_measure {
    const struct odt_cenv *env;
    void *font;
    odt_u16 ysize, baseline, xsize;
    odt_s32 (*len)(const struct tdt_measure *m, odt_u32 style, const char *s, odt_u32 n);
    void *rp;                           /* tdt_ext's measure_open's, while a layout measures */
};

/* One segment as the class makes it: the public Line, then ours */
struct tdt_seg {
    struct tdt_Line l;
    odt_u32 row;                        /* the row (ln_YOffset is a UWORD: rows past it wrap) */
    odt_u8 tab;                         /* a tab came before it */
    odt_u8 brk;                         /* how its row ends: TDT_BRK_ */
    odt_u16 pad;
};
#define TDT_BRK_NONE  0                 /* the row goes on */
#define TDT_BRK_HARD  1                 /* a line end in the text */
#define TDT_BRK_SPACE 2                 /* wrapped at a space, which went */
#define TDT_BRK_SOFT  3                 /* wrapped with nothing taken out */

struct tdt_layout {
    struct odt_MinList lines;           /* struct tdt_seg */
    odt_u32 rows;
    odt_u32 width;                      /* the widest row, pixels */
    odt_u32 segs;
    void *pool;                         /* the blocks the segments are carved from */
    struct tdt_seg **row_seg;           /* each row's first segment (NULL: no index) */
    odt_u32 *row_at;                    /* the places before each row; [rows] is all of them */
};

#define TDT_TABCHARS 8                  /* a tab stop each 8 spaces (ABI test) */

/* Lays out len bytes of text: ANSI SGR escapes (ESC [ or CSI) set the
 * style and pens and are not shown, other escapes are skipped; LF, CR
 * and CR LF end a row; wrap (pixels, 0 for none) breaks rows at spaces.
 * Returns 0, or DTC_ERROR_NO_FREE_STORE (what was laid out stays).
 * In linear time: wrapping adds each character's width (measured once a
 * style) instead of measuring the row so far again for each character;
 * each segment's own width is still measured whole, and if that ever
 * isn't the sum of its characters' (a font measured some other way) the
 * layout is made again measuring as before. Segments come from blocks of
 * TDT_POOL; each row's first segment and the places before it are
 * indexed for selecting, finding words and drawing. */
#define TDT_POOL 64
odt_s32 tdt_layout(const struct odt_cenv *env, const struct tdt_measure *m, const char *buf, odt_u32 len,
                   odt_u32 wrap, struct tdt_layout *out);
void tdt_free_layout(const struct odt_cenv *env, struct tdt_layout *lay);

/* Selects the characters from (x0, y0) to (x1, y1), content pixels, in
 * reading order, splitting segments at the ends; returns how many
 * segments are selected. */
odt_u32 tdt_select(const struct odt_cenv *env, const struct tdt_measure *m, struct tdt_layout *lay,
                   odt_s32 x0, odt_s32 y0, odt_s32 x1, odt_s32 y1);
void tdt_clear_selected(struct tdt_layout *lay);

/* The selected text (all of it when nothing is selected) as it reads:
 * tabs and line ends back, soft wraps joined. Writes at most size bytes
 * when buf isn't NULL; returns the length. */
odt_u32 tdt_copy_text(const struct tdt_layout *lay, int all, char *buf, odt_u32 size);

/* An FTXT form with one CHRS chunk of text, into buf (size >= n + 21);
 * returns its length. */
odt_u32 tdt_ftxt(const char *text, odt_u32 n, odt_u8 *buf);

/* The word at (x, y), content pixels, between delimiters (NULL: the
 * default set); into buf (size bytes, NUL ended); returns its length. */
odt_u32 tdt_word_at(const struct tdt_measure *m, const struct tdt_layout *lay, const char *delims,
                    odt_s32 x, odt_s32 y, char *buf, odt_u32 size);
extern const char tdt_default_delims[];

/* Draws rows top.. of the layout into the box, scrolled left by left
 * pixels: the box filled in the background pen, each segment's own
 * background where it isn't the default, selected ones in FILLPEN with
 * FILLTEXTPEN text. */
struct tdt_pens { odt_s32 text, back, fill, filltext; };
void tdt_render(const struct odt_cenv *env, const struct tdt_ext *x, const struct tdt_measure *m,
                const struct tdt_layout *lay, void *rp, const struct tdt_pens *pens, odt_s32 left, odt_s32 top,
                odt_s32 width, odt_s32 height, odt_u32 top_row, odt_s32 left_px);

/* ---- the class --------------------------------------------------------------- */

struct tdt_inst {
    char *buffer;                       /* TDTA_Buffer, our copy */
    odt_u32 buffer_len;
    struct tdt_layout lay;              /* our line list */
    struct odt_MinList *user_lines;     /* a subclass's TDTA_LineList, when it gives one */
    char *word_delim;                   /* TDTA_WordDelim, the caller's */
    char word[128];                     /* TDTA_WordSelect */
    odt_u8 word_wrap;                   /* TDTA_WordWrap */
    odt_u8 laid_out;
    odt_u8 open_font;                   /* font is ours to close */
    odt_u8 pad;
    void *font;
    struct tdt_measure m;
    odt_u32 laid_wrap;                  /* the wrap width of the layout we have */
    odt_u32 last_secs, last_micro;      /* the last click, for double clicks */
    odt_s32 last_x, last_y;
};

odt_u32 tdt_dispatch(const struct odt_cenv *env, struct odt_IClass *cl, void *obj, void *msg);
odt_u32 tdt_inst_size(void);

/* What the class answers to DTA_Methods (ends with ~0) */
extern const odt_u32 tdt_methods[];

#endif
