# opendatatypes: the rules every datatype keeps

Design 0.1, 23 October 2026. The Team's decisions of that morning:

1. **One home.** Every datatype the Team makes, and datatypes.library with
   its system classes, lives in this repository.
2. **Every datatype writes**, where the licences allow: "all datatypes
   should have a write option if licensing allows".
3. **The best place first**: datatypes "should use our OpenUp capabilities
   to look for cores, GPU etc before falling back to a single core m68k".

Sections 1 and 2 say what rules 2 and 3 mean in code. Section 3 is where
each datatype stands and what it still needs, and section 4 the order of
work.

## 1. Writing

`DTM_WRITE` has two modes, and a datatype answers both:

- **`DTWM_IFF`** writes the group's IFF form: ILBM for a picture, 8SVX for
  a sound, ANIM for an animation, FTXT for text. The system classes do it
  (`libraries/opendatatypes/classes/datatypes/`), so a subclass that leaves
  `DTM_WRITE` to its superclass already has it: what it decoded goes out as
  ILBM or 8SVX. That is what AmigaOS's own datatypes do, and clipboard
  copies (`DTM_COPY`) go the same way.
- **`DTWM_RAW`** writes the datatype's **own** format: a WebP for
  webp.datatype, a PNG for openpng.datatype. This is the new part. It is
  what lets a program save in a format through its datatype, with no
  library of its own (MultiView's Save As, OpenBrowser's image save).
  A converter needs an object of the **target** class, since `DTM_WRITE`
  goes to the class an object was made with: it opens the source file
  (`NewDTObject`), reads its pixels or sound out (`PDTM_READPIXELARRAY`,
  `SDTA_Sample`), makes an empty object of the target class
  (`NewDTObject(NULL, DTA_SourceType, DTST_RAM, DTA_BaseName, "openpng",
  ...)`, which datatypes.library supports), writes them in
  (`PDTM_WRITEPIXELARRAY`, `SDTA_Sample`), sets that datatype's options
  with `SetDTAttrs`, and sends it `DTM_WRITE` in raw mode with a file
  handle open on the destination.

What raw mode writes, in order of preference:

1. The object as it is now, encoded in the datatype's format (after
   `PDTM_WRITEPIXELARRAY`, a scale, or new sound data). This is the real
   write option.
2. Where the object can't be encoded but came from a file of that format,
   the file as it came (openmodule.datatype does this today), **only while
   the object is as it was read**. Once its pixels, size or sound have
   changed, writing the original would lose the changes and say it had
   worked, so raw mode fails with `ERROR_NOT_IMPLEMENTED` instead. This
   keeps Save working while 1 is written, and stays as the answer for
   formats that can't be made from what the object holds (section 3).

Options go through attributes the datatype documents (quality, lossless,
compression level, bit rate), with defaults that need no attribute at all.
Each datatype's README lists them.

A format whose encoder the Team may not ship (section 3's "no" rows) answers
`DTWM_RAW` as in 2: the original file while the object is unchanged, else
`ERROR_NOT_IMPLEMENTED`. It still writes IFF.

## 2. The ladder: where the work happens

Every datatype looks, when an object opens, for the places OpenUp offers,
and uses the first that is there for each kind of work, in that kind's
order. Only when none is does it fall back to this CPU alone.

The places (`OIA_Rung` is the number an object reports; it is a name, not
the order they are tried in):

| Place | `OIA_Rung` | Through | What it can do |
| --- | --- | --- | --- |
| **A cores board core**: AmigaChrome's cores board ($DA15 product 7, up to eight translated 68040 cores) | 2 | `openmulticore.library` jobs (`OMC_Submit`, `OMC_Wait`), DalsinAI/openamigamulticore | 68k compute: decoding and encoding (libwebp, libvpx, our PNG and TGA code), mixing, and pixel work too. A job calls no OS, touches no Chip RAM and writes only its granted buffers |
| **The GPU** | 4 (new) | `opengpu.library` (the one library: OpenGPU, OpenGfx and SDL 2) | Pixel work: scaling, colour conversion, dithering to 256 colours or HAM8, presenting video frames. Video decode where a driver offers it, through `openmedia.library` once it exists |
| **A services card or a paired Cradle** | 3 | `openservice.device`: `media.decode/1`, `doc.render/1`, and the encode calls section 4 adds, DalsinAI/openamigaservice | Formats with no 68k code (AVIF, HEIC, office documents, most video), and heavy work a core can't take |
| **This CPU**, a single 68k | 1 | plain calls | Everything that has 68k code, when nothing above is there |

The order, per kind of work:

| Kind of work | Tried in this order |
| --- | --- |
| Decoding, encoding, mixing (68k code) | a core, the service, this CPU |
| Formats with no 68k code | the service only |
| Pixel work: scaling, colour conversion, dithering, HAM8 | the GPU, a core, this CPU |
| Video decode | the GPU (a driver through `openmedia.library`), a core, the service, this CPU |

The rules:

- **Chosen per object, per kind of work**, when the object opens: decoding
  may go to a core while dithering goes to the GPU. A rung that fails (a
  job faults, the GPU refuses a surface, the service drops) falls to the
  next, as openmodule.datatype does when a core fails it.
- **The same answer on every rung.** A rung is faster, never different:
  each kind of work is tested against this CPU's result.
- **This CPU is always there for formats with 68k code.** Formats with
  none (the service only) fail to open without a service, as they do today,
  and say why.
- **One piece of code chooses**, `Datatypes/common/`, shared by every
  datatype, so each one asks the same questions in the same order.
  openmodule.datatype's choice (`openmodule/DESIGN.md`, section 7) is the
  first user and the model.
- **Forcing a place** for tests: `ENV:OpenImage/Rung` set to `cores`,
  `gpu`, `service` or `cpu`, per datatype as `ENV:OpenImage/<Datatype>Rung`.
  A forced place that is not there, or fails, **fails the open** and says
  which and why, so a test that forces the GPU never passes on this CPU
  without knowing. Tests also check `OIA_Rung`. openmodule's older
  `ModulePlayer` keeps its meaning (a forced place that is not there falls
  to the next) for the settings people already have.
- **Saying where**: `OIA_DecodedBy`, `OIA_Stats` and `OIA_Rung`
  (`Datatypes/include/datatypes/openimage.h`) on every datatype, not only
  openmodule, so OpenPlay and MultiView's info can show it. `OIA_Rung`
  keeps today's numbers (1 this CPU, 2 a core, 3 the service) and adds 4
  for the GPU.
- **Writing uses the same ladder.** Encoding is compute like decoding:
  WebP and VP9 encodes go to a core first, then the service, then this CPU.

`openmulticore.library` and `opengpu.library` are not in OpenUp yet, so
the cores board and the GPU are only there where they are installed by
hand. OpenUp should carry both before the datatypes rely on them.

## 3. Where each datatype stands

"Licence" is about shipping an encoder: the Team's code, or an encoder whose
licence and patents let the Team ship it. It is the Team's reading, not
legal advice; the "no" rows need the Team's word before anyone writes them.

### The Team's datatypes (`Datatypes/`)

| Datatype | Raw write today | Raw write to add | Licence | Ladder today | Ladder to add |
| --- | --- | --- | --- | --- | --- |
| webp | lossless WebP (#6) | lossy WebP, as an option | yes: libwebp's encoder is in the same BSD tarball | this CPU | encode and decode as core jobs; the service |
| webm | none | WebM, VP8 or VP9, with Opus or Vorbis sound | yes: libvpx (BSD, WebM patent grant); Opus and Vorbis are BSD and royalty-free | this CPU (sound by the service) | frames as core jobs; dithering on the GPU |
| openpng | PNG (#2) | done | yes: our own code | this CPU | core jobs |
| opentga | TGA 2.0, RLE or not (#6) | done | yes: our own code, Truevision's public spec | this CPU | core jobs |
| cdxl | the film as it plays (#6) | done | yes: our own code | this CPU | GPU for chunky films' dithering |
| openmodule | the module as it came | (IFF: the song rendered to 8SVX) | yes for 8SVX (libxmp, MIT); a module can't be made from sound | cores, service, this CPU | the GPU has nothing to do here |
| openpicture | none | AVIF, JPEG XL, QOI, OpenEXR, Radiance HDR, JPEG 2000, DDS through the service | yes: libavif, libjxl, OpenJPEG and OpenEXR are BSD; AV1 is royalty-free. **No**: HEIC (HEVC patents), camera RAW and fonts (nothing to write); PSD and XCF later | the service | scaling on the GPU |
| opensound | none | FLAC, Ogg Vorbis, Opus, ALAC, MP3 through the service | yes: FLAC, Vorbis, Opus (BSD), ALAC (Apache 2.0), MP3 (LAME runs in the service; the patents ended in 2017). **No**: AAC and WMA (encoder licences and patents); MIDI, SID and modules can't be made from sound | the service | decode as core jobs where there is 68k code (FLAC first) |
| opendoc | none | the document as it came; converted by the service to ODF, PDF or DOCX | yes: LibreOffice (MPL 2.0) runs as its own program on the Cradle | the service | page scaling on the GPU |
| openvideo | none | WebM (VP9 or AV1 with Opus), MKV with the same, MPEG-1 | yes for those (royalty-free; MPEG-1's patents have ended). **No**: MP4/H.264, HEVC, WMV (encoder patents) | the service | HAM8 and 256-colour conversion on the GPU, then cores |
| stream, gzip, brotli | plain bytes; gzip coded again on request, `SDTA_WriteCoded` (#6) | a Brotli encoder (our own, from RFC 7932) | yes: zlib licence and MIT | this CPU | core jobs for compression |

### datatypes.library's system classes (`libraries/opendatatypes/`)

| Class | IFF write today | To add |
| --- | --- | --- |
| picture | ILBM | the ladder for scaling and remapping (`PDTM_SCALE`, the pen remap) |
| sound | 8SVX | none |
| text, ascii | FTXT, and raw text | none |
| animation, anim | **none** | ANIM writing (ANIM-5 deltas), so every animation datatype gets IFF write |
| ilbm, 8svx | through picture and sound | raw write of their own forms |

## 4. Order of work

1. `Datatypes/common/`: the shared ladder code, lifted from
   openmodule.datatype, with `ENV:OpenImage/Rung` and `OIA_*` on every
   datatype. Openmodule moves onto it with no change in behaviour.
2. Raw write for the Team's own formats: openpng (#2); opentga, cdxl,
   lossless webp and gzip (#6). Each still to be checked on an Amiga with
   `tests/dtsave.c`.
3. ANIM write in the animation class, so every animation datatype writes
   IFF.
4. Core jobs for webp, openpng and opentga decoding and encoding; GPU
   dithering for webm, cdxl and openvideo.
5. The service's encode calls (`media.encode/1`, `doc.convert/1`, in
   openamigaservice), then raw write for openpicture, opensound, opendoc,
   openvideo and webm.

Each step: a lab copy of an OS 3.2 instance, the same answer on every rung
against this CPU's, and the timings in the datatype's README.
