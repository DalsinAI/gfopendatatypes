# text.datatype and ascii.datatype

Open versions of OS 3.2's `text.datatype` (a subclass of datatypesclass)
and `ascii.datatype` (a minimal subclass of it). Version 47.100. The
side-by-side builds are `gfopentext.datatype` and `gfopenascii.datatype`, and
`gfopenascii` sits on `gfopentext`. They are written from the NDK 3.2 headers,
the autodocs and measured behaviour only.

| File | What |
|---|---|
| `text_core.c/.h` | Plain C core, no Amiga headers: TDTA_ attributes, line list, ANSI styles, tabs, word wrap, selection, DTM_COPY/DTM_WRITE (raw and FTXT), word select, render |
| `dtc_util.h` | Tag, list and OM_GET helpers shared by the four classes |
| `dtc_shell.h` | Amiga shell: env over BOOPSI, exec, dos, iffparse, layers; RomTag, MakeClass/AddClass, one LVO (`ObtainEngine`, -30) |
| `opentext_class.c` | text.datatype's shell: fonts, TextLength/Text/RectFill, clip region, DrawInfo pens, DoubleClick |
| `../ascii/ascii_core.c/.h` | ascii.datatype: reads FILE, MEMORY, clipboard/IFF (FTXT CHRS) and RAM sources into TDTA_Buffer |
| `../ascii/openascii_class.c` | ascii.datatype's shell |
| `build-amiga.sh`, `build-family.sh` | Builds all four classes and their ABI tests (stove) |
| `tests/TextClassABITest.c` | OS 3.2's ascii.datatype next to gfopenascii.datatype |

The qemu test is `tests/m68k/test_opendatatypes_textanim.sh`, against the
python model `tests/m68k/opendatatypes_textanim_ref.py`.

## Speed

Layout is linear in the text. Wrapping used to measure the row so far
again for every character (TextLength on a new RastPort each time, so a
32 KB text at 100 columns was 33,326 calls over 1.6 million characters);
now each character's width is measured once a style and added up, each
segment is still measured whole, and if a segment's width is ever not the
sum of its characters' (a font measured another way) the layout is made
again measuring as before, so the line list is the same either way. One
RastPort serves the whole layout (`measure_open` in `tdt_ext`). Segments
come from blocks of 64 instead of one allocation each, and each row's
first segment and the places before it are indexed, so drawing, selecting
and finding a word start at their row instead of the first line.

## Edge cases

| Case | What we do | Source |
|---|---|---|
| Line ends | LF, CR and CRLF each end one row | ABI test |
| Tabs | Stop every 8 space widths, measured from the line's left edge | ABI test |
| ANSI | `ESC[` and CSI (0x9b) SGR: 0, 1, 3, 4, 22-24, 30-37, 39, 40-47, 49; other sequences are dropped | ABI test |
| Word wrap | Breaks at a space (eaten), else after a delimiter, else mid-word when one word is wider than the view | measured |
| Default delimiters | `"\t *-,<>()[];\""` | ABI test |
| TDTA_Buffer on OM_SET | Copied; the caller's buffer may go away | documented |
| DTM_COPY with nothing selected | Copies the whole text | ABI test |
| Copied text | Tabs and line ends are rebuilt, ANSI is not | measured |
| DTM_WRITE DTWM_IFF | FORM FTXT with one CHRS | documented |
| Units | VertUnit = font height, HorizUnit = font width | measured |
| Double click | Selects the word under the pointer, then the superclass's GM_GOACTIVE | measured |
| Empty file | Opens, with no lines | ABI test |
| Busy layout | GM_RENDER skips when the layout semaphore is held | measured |
| No memory for TDTA_Buffer's copy | OM_NEW fails (ERROR_NO_FREE_STORE); OM_SET keeps the old text and sets IoErr | ours |
| ascii: DTA_SourceSize near 4 GB | ERROR_NO_FREE_STORE (checked before it is added); no copy of the read text: no object | ours |

## Not yet

- DTM_PRINT, and the `printer.device` path.
- ScrollRaster on scrolling: we redraw the whole view.
- Links (LNF_LINK) and embedded objects (LNF_OBJECT) are never made.
- Charset conversion (only bytes in, bytes out).
- The shells and ABI tests are not yet built or run on the stove; the cores
  are tested under qemu (m68k-linux-gnu-gcc 13, -O0/-O2 for 68000/68020/68040).
