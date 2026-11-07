# GFOpenDatatypes: an open datatypes.library

datatypes.library 47.100 for AmigaOS 3.2, API and ABI compatible, clean-room
(written from the autodocs, the NDK 3.2 headers' layouts and OS 3.2's
answers measured by `tests/DatatypesABITest`, never from Kickstart,
Hyperion or AROS code). It runs on a 68000. One of the open libraries; the
plan is [`../OPEN_LIBRARIES.md`](../OPEN_LIBRARIES.md).

## Steps

| Step | What | State |
| --- | --- | --- |
| D1 | The descriptor database (DEVS:DataTypes), recognition (ObtainDataTypeA by file and memory; ReleaseDataType), recognition code (DTCD), the V45 method tables, GetDTString, GetDTTriggerMethodDataFlags | written, waiting on OS 3.2's probe |
| D2 | datatypesclass and the object calls: NewDTObjectA, DisposeDTObject, Set/GetDTAttrsA, Add/RemoveDTObject, RefreshDTObjectA, DoDTMethodA, DoAsyncLayout (with its layout process), Obtain/ReleaseDTDrawInfo and DrawDTObjectA (-120 to -132), StartDragSelect, SaveDTObjectA, PrintDTObjectA (with its process); DTST_CLIPBOARD; the datatypes.library replacement | written: core tested under qemu, shell built only on the stove, waiting on OS 3.2's OBJ probe |
| D3 | LockDataType, FindToolNodeA, LaunchToolA (-144 to -156), sys/datatypes.catalog (`catalogs/datatypes.cd`), the private call our AddDataTypes makes (the command is GFOpenBench's, `commands/adddatatypes`) | written, as D2 |
| D4 | The class libraries (picture, ilbm, sound, 8svx, text, ascii, animation, anim) | the class helpers, in `classes/datatypes/` |

`datatypes.library` is the replacement; it makes the public class
"datatypesclass". The same code builds as `gfopendatatypes.library`, the
side-by-side twin `tests/DatatypesABITest` opens next to OS 3.2's own,
whose class is "opendatatypesclass" so it doesn't take OS 3.2's name.
Class libraries the twin opens subclass OS 3.2's datatypesclass, so for
their objects the twin's DoAsyncLayout lays out on the caller's task and
PrintDTObjectA and StartDragSelect answer 0.

| File | What it is |
| --- | --- |
| `dt_core.c`, `.h` | Descriptor files, the lists recognition walks, recognition, the method tables, tools and strings, in plain C with no Amiga headers. |
| `dtclass_env.h` | What every datatype class shares: datatypesclass.h's numbers, messages and layouts (DTSpecialInfo, FrameInfo, the dt messages), the gadgetclass and BOOPSI parts they use, `struct odt_cenv` (what a class core asks of the system), and `struct odt_dtinst`, datatypesclass's own instance (not for subclasses: on OS 3.2 it is private, so they use attributes). |
| `dtclass_core.c` | datatypesclass in plain C over `struct odt_cenv`, and the object half of DoAsyncLayout, PrintDTObjectA, StartDragSelect, GetDTAttrsA, LaunchToolA's command line. |
| `opendatatypes_lib.c` | The library: RomTag, the jump table in datatypes.library's LVO order, each call in its documented registers; reads DEVS:DataTypes the first time it is asked, loads recognition code from memory with InternalLoadSeg; makes datatypesclass under gadgetclass and fills its env (BOOPSI through CallHookPkt, DOS, iffparse on files, the clipboard and memory, the layout and printing processes, the catalog). |
| `datatypes.lvo` | The jump table, LVO by LVO, as NDK 3.2's fd has it: -120 to -132 and -144 to -252 private (the comments say what ours keep there), the V45 calls from -258. `../common/check_fd.py` compares it with the NDK's `datatypes_lib.fd`. |
| `ndk_check.c` | Every constant and layout `dt_core.h` and `dtclass_env.h` carry, checked against the NDK 3.2 headers when it compiles. |
| `catalogs/datatypes.cd` | sys/datatypes.catalog's descriptor: every id GetDTString has, with the built-in English. |
| `build-amiga.sh` | With the os32-gcc16 stove: the LVO and NDK checks, then `datatypes.library`, `gfopendatatypes.library`, `AddDataTypes` (GFOpenBench's, built by `commands/build-amiga.sh`) and `DatatypesABITest`. |
| `tests/DatatypesABITest.c` | Runs the calls through OS 3.2's datatypes.library and `gfopendatatypes.library` and prints each line that differs; PROBE prints OS 3.2's answers. DESC adds descriptors made to ask one question each (lab Amigas only), OBJ the object calls (objects from a text, an ILBM, bytes, memory, the clipboard and DTST_RAM), SCAN=dir recognises a directory's files, LIST dumps OS 3.2's own list. |

## What the autodocs leave open

`dt_core.c` marks each answer `measured` once OS 3.2's probe has given it,
or `guess` until then. Measured so far (labs at 1cba45a and 1ea835e):

- Recognition looks at the first 1084 bytes, from a file or from memory.
- Text is every byte but the C1 controls (0x80-0x9f) and the C0 controls
  other than BEL to CR, DC1, DC3 and ESC.
  Text tries only the ASCII types, binary only the binary types, IFF (FORM,
  CAT or LIST: FORM from 6 bytes on) only the IFF types; then the
  library's own type. No data gives no type and leaves IoErr alone; types
  0 and 99 give IoErr 236.
- Order: the longer mask first, then the lower priority (an unsigned word:
  0 beats 5, 1 beats 0xffff), then the newest.
- A mask without DTF_CASE ignores case; a negative mask word is any byte.
  A type without a mask needs a pattern other than "#?". An IFF type's ID
  isn't compared to the FORM's.
- Memory has no name: "#?" fits it, "#?.x" doesn't. A pattern without
  wildcards fits every name but its own (Stricmp), so such a type takes
  nearly all binary data. We copy this because OS 3.2 does it. Patterns see
  the file's name, not its path.
- A DTHD offset of 0 points at the header itself. The tools are not put in
  dtn_ToolList, even from one DTTL chunk. A DTCD that isn't a load file is
  ignored.
- Recognition code gets the context in A0 with every base, IFFParseBase
  included. We also push it on the stack for code with stack arguments.
  For data in memory it still gets a lock, an empty FIB and a file handle;
  ours answer DOS packets from a PA_SOFTINT port (read, seek, examine).

Open: whether the mask outranks the priority in every case.

Recognition's own costs: each descriptor's pattern is parsed
(ParsePatternNoCase) the first time recognition asks it and the token kept
with the descriptor, "#?" never ("#?" fits every name); before, every
pattern asked was allocated, parsed and freed for every file. IFF data's
file is parsed as IFF (AllocIFF, InitIFFasDOS, OpenIFF) only when
recognition code is about to run on it, not for every IFF file.

## datatypesclass (step D2)

- **The instance.** An object is a gadget (gadgetclass) whose SpecialInfo
  is its DTSpecialInfo. OM_NEW opens the source DTA_SourceType names and
  DTA_Handle gives it: for DTST_FILE an IFF type's file as an IFFHandle
  (InitIFFasDOS), any other type's as a DOS file handle; DTST_CLIPBOARD
  an IFFHandle on the unit the name's number gives; DTST_MEMORY an
  IFFHandle on the memory for an IFF type, NULL for others (the class
  reads DTA_SourceAddress and DTA_SourceSize); DTST_RAM nothing.
  OM_DISPOSE stops the layout and printing processes, waits for the
  layout (it takes si_Lock) and the print (its process holds a lock of
  its own while it runs) and closes the source.
- **Scrolling.** DTA_TopVert and DTA_TopHoriz are clamped to 0 ..
  total - visible; a change keeps the old top in si_OTop*. Set by the
  program or the class (OM_SET), every change of the six scroll numbers
  is notified (OM_NOTIFY, with GA_ID last); set by a scroller (OM_UPDATE)
  the object draws the difference at once (GM_RENDER, GREDRAW_UPDATE) and
  says nothing back.
- **GM_LAYOUT** (and DTM_PROCLAYOUT's own part) keeps the gadget's box as
  DTA_Domain, sets the visible numbers from it in the object's units and
  DTSIF_NEWSIZE when its size changed. **DoAsyncLayout** stops a running
  layout, notifies DTA_Busy (interim) and starts a process that holds
  si_Lock, sends DTM_ASYNCLAYOUT, then notifies DTA_Busy FALSE and
  DTA_Sync TRUE with the scroll numbers; a layout stopped meanwhile says
  nothing. A layout stopped before it began leaves everything to the newer
  one. **PrintDTObjectA** runs DTM_PRINT on its own process and notifies
  DTA_PrinterStatus.
- **The mouse.** With something to scroll, dragging scrolls (the contents
  follow the mouse, interim notifications, a final one at SELECTUP; the
  menu button puts it back). After StartDragSelect, dragging draws a
  complemented band and SELECTUP sends DTM_SELECT with the rectangle in
  the object's pixels (the band plus the scroll offset). During a layout
  the object won't go active.

## Edge cases

Each says what we do, and whether it is documented, measured on OS 3.2
or open ("ABI test": `DatatypesABITest OBJ` has the PROBE line).

| Case | Ours | Status |
| --- | --- | --- |
| DTA_GroupID that isn't the data's group | NULL, IoErr ERROR_OBJECT_WRONG_TYPE | documented (the error: ABI test) |
| NewDTObjectA of a type with no class library (binary) | NULL, IoErr DTERROR_COULDNT_OPEN | ABI test |
| NewDTObjectA of a missing file | NULL, Lock's IoErr | ABI test |
| NewDTObjectA with no name for DTST_FILE | NULL, ERROR_REQUIRED_ARG_MISSING | ABI test |
| DTST_RAM: the class each group gets | picture, sound (and instrument), text, animation (and movie), amigaguide (document), DTA_BaseName first; system and music none | ABI test |
| DTST_HOTLINK | NULL, ERROR_NOT_IMPLEMENTED | ABI test |
| DTST_MEMORY, non-IFF type: DTA_Handle | NULL | ABI test |
| DTST_CLIPBOARD: the name | the unit number | documented |
| DTST_CLIPBOARD with DTA_Handle | the program's IFFHandle, recognised and used as the source; not closed with the object | documented (who frees it: ABI test) |
| DTST_FILE with DTA_Handle | the name is what is opened (a lock alone fails, the autodoc's BUGS); the lock becomes the object's and is UnLocked with it | documented |
| AddDTObject | AddGList, then GM_LAYOUT with gpl_Initial 1 for an object without relative size (Intuition sends the others theirs) | documented |
| SetDTAttrsA with a window | SetGadgetAttrsA; a nonzero answer refreshes the object | ours (ABI test) |
| ObtainDataTypeA DTST_CLIPBOARD | the handle is an IFFHandle; parsed one step if it has no chunk yet, the FORM's type recognised | ABI test (how far OS 3.2 parses) |
| Top beyond the end, or negative | clamped | ABI test |
| Visible numbers at GM_LAYOUT | domain / unit, the class may set its own | ABI test |
| DTA_Left/Top/Width/Height and DTA_Rel* | taken as GA_ ones | ABI test (the NDK says use GA_) |
| GetDTAttrsA's answer | how many tags the object knew; NULL object 0 | ABI test |
| DTA_Methods of a class with none of its own | datatypesclass's list | ABI test |
| DTM_FRAMEBOX default | FRAMEF_SPECIFY keeps the frame; else the contents' frame, else the kept one; 1 | ABI test |
| DTM_WRITE DTWM_RAW default | the source's bytes (a file, the file under an IFF type's handle, or memory), the file's position put back; a file over 256 bytes is copied 32 KB a packet (16 KB, then 256 bytes, when memory is short); otherwise 0, ERROR_NOT_IMPLEMENTED | ABI test |
| SaveDTObjectA that fails | the file deleted, IoErr kept (else DTERROR_COULDNT_SAVE) | ABI test |
| SaveDTObjectA's icon | ENV:Sys/def_project, else icon.library's default project | ABI test |
| DTM_PROCLAYOUT default | GM_LAYOUT's part only | ABI test |
| DoAsyncLayout when no process can be made | laid out on the caller's task | ABI test |
| StartDragSelect | 0 for a class without DTM_SELECT, during a layout, a drag or a select already armed | documented in part (ABI test) |
| dts_Select's units | pixels of the object, scroll offset added | ABI test |
| Drag with nothing to scroll and no select armed | not active (GMR_NOREUSE) | ABI test |
| DTM_PRINT on its process | without a GadgetInfo | ABI test |
| -120 to -132 | ObtainDTDrawInfoA, DrawDTObjectA, ReleaseDTDrawInfo (V44 places) | ABI test: the OBJ slots line |
| -144 to -156 | LockDataType, FindToolNodeA, LaunchToolA (V45 places) | ABI test: the OBJ slots line |
| -252 | our AddDataTypes call | ours (OS 3.2: ABI test) |
| LaunchToolA | TF_SHELL: `program "project"` (AmigaDOS quoting) asynchronously on NIL:; TF_RX: `RX program "project"` (for both, a project whose name has a control character, a line feed say, is refused: 0); TF_WORKBENCH: OpenWorkbenchObjectA with the project's lock and name; attrs ignored | ABI test |
| FindToolNodeA on dtn_ToolList | finds nothing: OS 3.2 leaves the list empty (D1, measured) | measured |
| GetDTString with sys/datatypes.catalog | the catalog's string, the built-in when it has none; "" (id 0) never translated | documented |

## Not yet

- Nothing of D2/D3 has run on an Amiga: the shell, AddDataTypes and the
  ABI test's OBJ part are built only with the os32-gcc16 stove, which the
  cloud hasn't got (syntax was checked here against stand-in headers).
- The private slots' places (-120 to -156) and the -252 call wait on the
  OBJ probe; if OS 3.2 keeps something else there, they move. OS 3.2.3's
  own AddDataTypes calls no slot at all (`obtrace`, `commands/tests/WAVE3.md`),
  so -252 stays ours: GFOpenBench's AddDataTypes works with this library
  only.
- OS 3.2's own C:AddDataTypes doesn't work with ours (it uses the library's
  private list, whose layout isn't measured); ours doesn't work with OS 3.2's
  library.
- DTA_ARexxPortName is kept but no ARexx port is opened; DTM_TRIGGER, DTM_GOTO,
  DTM_COPY and the draw methods are the classes' (datatypesclass answers 0).
- No DataTypesList semaphore is published.

## Tests

- `sh tests/m68k/test_opendatatypes.sh` (from the repository root): the
  descriptor core at -O0 and -O2 for the 68000 and 68040 under qemu-m68k.
- `sh tests/m68k/test_opendatatypes_class.sh`: datatypesclass's core at
  -O0 and -O2 for the 68000, 68020 and 68040, hosted on GFOpenIntuition's
  BOOPSI core with a probe subclass, against `opendatatypes_class_ref.py`'s
  own model (random scripts: sources, layouts with and without processes,
  scrolls, drag scrolls, drag selects, printing; LaunchToolA's command
  lines; the DTST_RAM classes; the catalog descriptor against the built-in
  strings). nm: the core calls only libgcc and dt_core, and keeps no data.
  Results with m68k-linux-gnu-gcc 13.3.0 and qemu-m68k 8.2.2: all six
  builds match the model (seeds 3210, 1, 2, 3).
- `DatatypesABITest PROBE DESC OBJ LIST`: OS 3.2's answers, on a lab Amiga.
- `DatatypesABITest DESC OBJ LIB=dir [SCAN=dir]`: ours against OS 3.2's.
