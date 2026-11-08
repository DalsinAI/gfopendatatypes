# Datatypes

`webp.datatype`, `webm.datatype`, `gfopenpicture.datatype`, `gfopenpng.datatype`,
`gfopentga.datatype`, `gfopengif.datatype`, `gfopensound.datatype`, `gfopenmodule.datatype`,
`gfopendoc.datatype`, `gfopenvideo.datatype`, `cdxl.datatype`, and the data
class `stream.datatype` with `gzip.datatype` and `brotli.datatype`, for
AmigaOS 3.x, by the AmiCradle Team. They let any datatypes program (MultiView, GFOpenBrowser, GFOpenPlay, a
picture viewer) open WebP pictures, WebM video, Targa (TGA) pictures, AVIF,
HEIC, JPEG XL, camera RAW, Photoshop and
GIMP pictures, FLAC, Ogg Vorbis, Opus, AAC, ALAC, WMA and MP3 sounds, MIDI
and SID tunes, music modules (ProTracker MOD, MED and OctaMED, Oktalyzer,
DigiBooster, FastTracker 2 XM, Scream Tracker 3 S3M, Impulse Tracker IT and
more), office documents, PostScript, e-books, MP4, MKV, AVI, WMV and
MPEG video, streams from the network (HLS, transport streams) and CDXL films. They are built for the 68040, soft float (no FPU code; linked
with `../build/m68k-soft-m68040.specs` so libnix's soft maths is used),
so they run on a 68LC040 or 68EC040 too. WebP,
WebM and music modules need no other libraries and play on the Amiga
itself, as do PNG and TGA; the others are decoded by a Cradle (below).

**Status:** Working on the bench: both decode exactly as the same libraries
do on x86 cores. Not yet run on real Amiga hardware; WebM playback inside MultiView
is not yet checked (see Tested).

## What they do

| Datatype | Group | Built on | What it shows |
| --- | --- | --- | --- |
| `webp.datatype` | picture | libwebp 1.6.0 | Lossy, lossless and alpha WebP, through picture.datatype's 24-bit mode (32-bit with alpha). An animated WebP shows its first frame; one without alpha shows it on the ANIM chunk's background colour (47.4). libwebp's function and look-up tables are all set up while the library loads (`webp/webpcore.c`), so decodes in several tasks at once only read them. Saves too (47.2): `DTM_WRITE` with `DTWM_RAW` writes the picture as a lossless WebP through libwebp's own lossless encoder (`webp/webpencode.c`), so it opens again pixel for pixel, the colour under clear pixels included; `OIA_Compression` (`include/datatypes/gfopenimage.h`) sets the effort, 0 the fastest to 9 the smallest. No floating point in the encoder either (`patches/libwebp-1.6.0-encoder-no-float.patch`; the build checks the link map), and no 64-bit multiplies or divides in its busiest sums, which a 68060 traps to 68060.library (`patches/libwebp-1.6.0-m68060.patch`: same files, fewer instructions on a 68040; `tests/test-webp060.sh`). |
| `gfopenpicture.datatype` | picture | the `media.decode/1` service | AVIF and HEIC/HEIF (iPhone photos), JPEG XL, camera RAW (Canon, Nikon, Sony, DNG and more), Photoshop PSD, GIMP XCF, OpenEXR, Radiance HDR, QOI, DDS, JPEG 2000, OpenRaster and Krita (their flattened picture), comic books (CBZ, the first page), SVG (drawn at its own size) and TrueType/OpenType fonts (a sample sheet), decoded on the services card or a paired Cradle and sent back as 32-bit ARGB, scaled down to fit `ENV:GFOpenImage/MaxSide` (default 4096). A sequence shows its first picture. |
| `gfopenpng.datatype` | picture | our own decoder (`png/pngdecode.c`) | PNG: every colour type and bit depth, transparency and Adam7 interlacing, decoded on the Amiga itself (no zlib, no libpng) and handed to picture.datatype as 32-bit ARGB, in strips of about 32 rows as they are converted (47.7: the picture is not held whole twice), so the pixels read back as ARGB are the ones decoded, alpha included. A pixel read or write with no pixel array is refused: that is what `PDTM_READPIXELARRAY` sent with `DoDTMethodA()` instead of `DoMethodA()` arrives as, and picture.datatype would write the picture from address 0. Its descriptor, `GFOpenPNG`, matches the signature and the IHDR after it, a longer mask than the system's `PNG`, so it is tried first while the system's png.datatype stays installed. PNG out too (47.4): `OIM_ENCODEPNG` (`include/datatypes/gfopenimage.h`) encodes ARGB pixels, a program's own (canvas `toDataURL`) or the object's, to a PNG file in memory, and `DTM_WRITE` with `DTWM_RAW` writes the picture as PNG; the deflate and CRCs are z.library's (`png/pngencode.c`). |
| `gfopentga.datatype` | picture | our own decoder (`tga/tgadecode.c`) | Targa (TGA) pictures, written from Truevision's TGA 2.0 specification: uncompressed and run-length encoded; colour-mapped (8 or 16-bit indexes, 15, 16, 24 or 32-bit colour maps), true-colour of 15, 16, 24 and 32 bits, and greyscale of 8 bits or 16 with alpha; rows from the bottom or the top, and right to left. Decoded on the Amiga itself and handed to picture.datatype as 32-bit ARGB in strips of about 32 rows, as gfopenpng.datatype does (47.3). Alpha comes from a 32-bit pixel, a 16-bit pixel's top bit or a 16-bit grey's second byte when the header gives alpha bits; a TGA 2.0 extension area that says there is none wins, premultiplied alpha is undone, and an alpha channel that is 0 everywhere counts as none. A TGA has no signature, so its descriptor, `GFOpenTGA`, goes by the name (`.tga`, `.targa`, `.icb`, `.vda`, `.vst`, in any case) and is tried after every descriptor with a mask. Saves too (47.2): `DTM_WRITE` with `DTWM_RAW` writes a TGA 2.0 file from our own encoder (`tga/tgaencode.c`), 32 bits with alpha when the picture has any, else 24, run-length encoded unless `OIA_Compression` is 0. |
| `gfopengif.datatype` | picture | our own decoder (`gif/gifdecode.c`) | GIF pictures and animations, written from the GIF89a specification: GIF87a and GIF89a, global and local colour tables, interlacing, transparency, frame delays, the NETSCAPE2.0 loop count and the four disposal methods. Decoded on the Amiga itself and handed to picture.datatype as 32-bit ARGB with alpha where the GIF is transparent. Every frame is there, composited as web browsers show them (a transparent screen; "restore to background" clears to transparent): the object's picture is frame 0, or the frame `PDTA_WhichPicture` names at `OM_NEW`; `PDTA_GetNumPictures` and `OIA_Frames` count them, and `OIM_READFRAME` (`datatypes/gfopenimage.h`) writes any frame with its delay into a program's pixels, which is how GFOpenBrowser plays them. Its descriptor, `GFOpenGIF`, masks the signature and the screen size, longer than the system's GIF descriptor, so it is tried first and the system's gif.datatype stays as it was. Saves too (47.4): `DTM_WRITE` with `DTWM_RAW` writes a one-frame GIF89a from our own LZW encoder (`gif/gifencode.c`; the LZW patents ended in 2003 and 2004): a picture held as colour indexes (an ILBM, a GIF's first frame) keeps its table and transparent colour; one held as ARGB is written exactly when it has 256 colours or fewer (255 with transparent pixels), else brought down to 256 by median cut. Alpha under 128 is transparent. |
| `gfopensound.datatype` | sound | the `media.decode/1` service; this CPU (47.2) | FLAC, Ogg (Vorbis, Opus), AAC/M4A, ALAC, WMA and MP3; MIDI (played through FluidSynth on the Cradle), C64 SID tunes (the first three minutes, through sidplayfp) and XM, IT and S3M modules (libopenmpt), sent back as 16-bit PCM at a rate Paula plays (`ENV:GFOpenImage/SoundRate`, default 28000 Hz: 44.1 kHz comes as 22.05 kHz). 16-bit stereo with sound.datatype V44 or newer, else 8-bit mono. With neither a card nor a Cradle (47.2), FLAC, Ogg Vorbis and MP3 are decoded on the Amiga itself (`opensound/snddec.c`: dr_flac, stb_vorbis and dr_mp3, single files `SOURCES` pins; stb_vorbis with `patches/stb_vorbis-1.22-comment-list-null.patch`), 44.1 and 48 kHz halved for Paula. For programs: opened with `OIA_Reader`, FLAC, Vorbis and MP3 come a piece at a time from `OIM_READSOUND` at their own rate, decoded here, never whole (`include/datatypes/gfopenimage.h`). An MP3 without an ID3 tag is taken by its name (`MP3` descriptor). Soft float like the rest: Vorbis and MP3 are slow on the 68k (FLAC is integer). `tests/test-sound.sh` checks the decoders against libsndfile's on x86 or ARM64 cores, damaged files too. |
| `gfopenaiff.datatype`, `gfopenwave.datatype` | sound | our own (`pcm/pcmcore.c`) | AIFF and AIFC, and WAV, written from Apple's AIFF 1.3 and AIFF-C and Microsoft's RIFF WAVE: 8, 16, 24 and 32-bit PCM, AIFC `sowt` and `fl32` and WAV float and `WAVE_FORMAT_EXTENSIBLE`, chunks in any order; mono to 8 channels (the first two of more are kept), as 16 bits. 16-bit stereo with sound.datatype V44 or newer, else 8-bit (stereo through `SDTA_LeftSample` and `SDTA_RightSample`). Save too: `DTM_WRITE` with `DTWM_RAW` writes AIFF (gfopenaiff) or WAV (gfopenwave), 16 bits, or 8 with `OIA_SampleBits` (the program dithers), or only the samples with `OIA_Headerless` (raw PCM, big-endian from gfopenaiff, little-endian from gfopenwave); `DTWM_IFF` is sound.datatype's 8SVX. A program's own sound goes in as a `DTST_RAM` object made with `OIA_PCM`; `OIA_Reader` streams a file through `OIM_READSOUND`. Their descriptors (`GFOpenAIFF`, `GFOpenAIFC` as IFF types, `GFOpenWAVE`) have longer masks than the system's `AIFF` and `WAVE`, so they are tried first. `tests/test-pcm.sh` checks the core on x86 or ARM64 cores. |
| `gfopenmodule.datatype` | sound | libxmp 4.7.3 | Music modules, played on the Amiga itself: ProTracker, NoiseTracker and SoundTracker MODs (M.K., M!K!, FLT4, FLT8, 2CHN to 32CH, and 15-instrument SoundTracker by its name), MED and OctaMED (MMD0 to MMD3, MED 2 to 4), Oktalyzer, DigiBooster and DigiBooster Pro, FastTracker 2 XM, Scream Tracker 3 S3M and Impulse Tracker IT (libxmp reads some 60 module formats in all; a descriptor for another is all it takes). Streamed by a player process of its own, so a song starts at once and never sits in memory whole: 16-bit stereo through AHI (`ahi.device` unit 0, at its own rate), else 8-bit stereo on two Paula channels. Mixed on a cores board core (AmiCradle's, through `gfopenmulticore.library`), else by `media.decode/1` on a services card or a paired Cradle, else on this CPU, and the datatype says which (`OIA_DecodedBy`, `include/datatypes/gfopenimage.h`; `openmodule/DESIGN.md` sections 2 and 7). The module's title and length show; play, pause, stop, volume and repeat work as for a sound. | For programs (47.4): opened with `OIA_Reader` nothing plays; the player mixes 16-bit stereo at `OIA_MixRate` (default 44100) with `OIA_MixInterp` (default spline), on a cores board core or this CPU, and `OIM_READSOUND` hands it out once through the song.
| `gfopendoc.datatype` | picture | the `doc.render/1` service | DOCX, XLSX, PPTX, OpenDocument, Word/Excel/PowerPoint 97, RTF, WordPerfect, PostScript and EPS, EPUB e-books, Markdown and CSV, laid out by LibreOffice or Apache OpenOffice (Ghostscript for PostScript) on the Cradle and shown as their pages one under the other, `ENV:GFOpenImage/DocWidth` pixels wide (default 800), the first `ENV:GFOpenImage/DocPages` pages (default 8). |
| `gfopenvideo.datatype` | animation | the `media.decode/1` service | MP4/MOV, MKV, AVI, WMV, MPEG and FLV video, and animated PNG (H.264, HEVC, AV1, VP9, MPEG-4, MPEG-1/2, WMV), kept open on the Cradle and sent a frame at a time, scaled to fit `ENV:GFOpenImage/VideoWidth` x `VideoHeight`, with its sound as 8-bit mono a frame at a time. On an AGA machine with a chipset screen the frames come in HAM8 (default 320 x 256), elsewhere in webm.datatype's 256 colours (default 640 x 480); `ENV:GFOpenImage/VideoMode` set to `HAM8` or `256` chooses (default `AUTO`). HAM8 uses the fixed 4x4x4 cube as base colours, or each frame's own with `ENV:GFOpenImage/VideoCMap` set. Streams: a NETSTREAM file (`#NETSTREAM`, then `NAME=` and `URL=` lines: what the NETSTREAM: handler gives for a channel) is played from its URL by the Cradle, which fetches and decodes it, so the Amiga never carries the stream; a stream that never ends plays as four hours of frames. A file bigger than `ENV:GFOpenImage/VideoChunk` KB (default 2048) goes to the Cradle in 256 KB parts, so it is never all in memory here. |
| `cdxl.datatype` | animation | our own reader (`cdxl/cdxl_core.c`) | CDXL films (the CDTV's and CD32's streaming video, and what media.cdxl/1 and ToCDXL make), played on the Amiga itself a frame at a time from the file, with no card or Cradle: 1 to 8 bitplanes (one after another, or each row's in turn), HAM6 and HAM8, each frame's own palette (a colour map handed out with every frame: its own, or the first's), and chunky RGB films shown in 256 colours (the 6x6x6 cube, from const tables). Opened in constant time when the frames are one size (the first and last headers and the length), else frame by frame; each frame is one DOS read in order (a Seek too when it isn't where the last ended), into a buffer whose planes are the frame's bitmap, as they lie in the file (47.3). The sound, mono or stereo, is played as 8-bit mono with each frame, at the rate in the header (else 11025 Hz); frames a second from the header, else from the sound's length. It replaces the system's `cdxl.datatype` and `CDXL` descriptor of the same names. Saves too (47.2): `DTM_WRITE` with `DTWM_RAW` writes the film as it plays, frame by frame as in the file, so an intact film comes out byte for byte the same; never onto the file it plays from. With `OIA_WriteFormat` `OIWF_MP4` in `dtw_AttrList` (47.3) raw mode goes to animation.datatype, which writes the film as an MP4. |
| `stream.datatype` | data (new) | our own (`stream/`) | The class for data that is not a picture or a sound: an object turns coded bytes into plain ones, read whole (`SDTA_Data`) or a piece at a time as they arrive (`SDTM_FEED`, `SDTM_TAKE`, `SDTM_END`), from a file, memory, a hook or what the program feeds it; `DTM_WRITE` (`DTWM_RAW`) saves the plain bytes. Its header is `include/datatypes/streamclass.h`; a decoder is a subclass that answers one method, `SDTM_CODEC`. The group, `GID_DATA`, is new. With `SDTA_WriteCoded` in `DTM_WRITE`'s attributes a subclass that can writes the bytes coded again (gzip.datatype); one that can't refuses (brotli.datatype). |
| `gzip.datatype` | data | z.library (GFOpenZlib) | gzip (one member or several), zlib-wrapped deflate (HTTP's `deflate`) and raw deflate, told apart by their first bytes or named with `SDTA_Wrapper`. The inflate is z.library's, opened for each object: no zlib is linked in. Its descriptor, `GZip`, matches `1f 8b 08`. Saves gzip too (47.2): `DTM_WRITE` with `SDTA_WriteCoded` deflates the plain bytes again through z.library, in the wrapper they came in (gzip, zlib or raw deflate), at `OIA_Compression`'s level. |
| `brotli.datatype` | data | our own decoder (`brotli/brdecode.c`) | Brotli (RFC 7932), written from the RFC: streaming (input may come a byte at a time), integer only, no writable data. Its static dictionary and transforms are the RFC's (`brotli/mkbrdict.py` writes them from the RFC's text and checks the dictionary's SHA-256). Brotli has no signature: a program names the class (`Content-Encoding: br`), and the `Brotli` descriptor takes files named `.br`. |
| `webm.datatype` | animation | libvpx 1.17.0 | WebM video, VP8 or VP9, decoded frame by frame as the animation plays. Frames are shown in 256 colours (a 6x6x6 colour cube with ordered dithering), so they play on any screen. Sound through `media.decode/1` when a card or Cradle offers it. |

GFOpenBrowser decodes the pictures in web pages through datatypes, so with
`webp.datatype` installed it shows WebP pictures too.

Every film (webm, cdxl, gfopenvideo, an animated GIF, and IFF ANIM) also
saves as an MP4: `DTM_WRITE` in raw mode with `OIA_WriteFormat` `OIWF_MP4`
(`include/datatypes/gfopenimage.h`) is answered by GFOpenDatatypes'
animation.datatype, their superclass, which encodes the frames as H.264 on
the Amiga itself (no card or Cradle) and keeps the sound as 8-bit PCM
(`../libraries/opendatatypes/classes/datatypes/animation/README.md`).
`tests/dtsave.c`'s `MP4` mode does it from the Shell.

`gfopenpicture.datatype`, `gfopensound.datatype`, `gfopendoc.datatype` and
`gfopenvideo.datatype` decode nothing themselves. It sends the file to
`media.decode/1` through `gfopenservice.device` (DalsinAI/gfopenservice):
the x86 or ARM64 cores do the work on AmiCradle and the Pi appliance, and a paired
Cradle on the LAN does it for a real Amiga, which we expect to have
PiStorm-class networking. With neither, the file does not open
(`not implemented`): AV1 and HEVC are too slow on a 68k at photo sizes, and
68k decoders for FLAC, Vorbis and Opus come next.
The services are described in gfopenservice's `docs/MEDIA_DECODE.md`
and `docs/DOC_RENDER.md`.

`webm.datatype` reads the WebM container itself (`webm/webm_demux.c`) and
decodes with libvpx. The reader's small reads come from a 4 KB cache of the
file (`webm/webm_readcache.h`), one Read for many elements where it took a
Seek and a Read for each (`tests/test-webm-read.sh`). Once GFOpenMedia's `gfopenmedia.library` exists
(DalsinAI/gfopenmedia), the datatype will hand the frames to it for
hardware decoding where a driver offers VP8 or VP9, and keep libvpx as the
fallback.

## Installing

Copy the files from `out/` (after building) to the same places on the Amiga:

| File | Goes to |
| --- | --- |
| `Classes/DataTypes/webp.datatype` | `SYS:Classes/DataTypes/` |
| `Classes/DataTypes/webm.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/WebP` | `DEVS:DataTypes/` |
| `Devs/DataTypes/WebM` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopenpicture.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/AVIF`, `AVIS`, `HEIC`, `HEIX`, `HEIF`, `JXL`, `JXL-ISO`, `RAW`, `PSD`, `XCF`, `EXR`, `HDR`, `HDR-Bin`, `QOI`, `DDS`, `JP2`, `J2K`, `ORA`, `KRA`, `CBZ`, `SVG`, `TTF`, `OTF`, `OTF-TT` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopenpng.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/GFOpenPNG` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopentga.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/GFOpenTGA` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopengif.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/GFOpenGIF` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopensound.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/FLAC`, `Ogg`, `M4A`, `M4B`, `WMA`, `MP3-ID3`, `AAC`, `MIDI`, `PSID`, `RSID`, `XM`, `IT`, `S3M` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopenmodule.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/ProTracker`, `ProTracker-100`, `StarTrekker`, `StarTrekker-8`, `MOD-xCHN`, `MOD-xxCH`, `SoundTracker`, `OctaMED`, `MED-2`, `MED-3`, `MED-4`, `Oktalyzer`, `DigiBooster`, `DigiBooster-Pro`, `Module-XM`, `Module-S3M`, `Module-IT` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopendoc.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/DOCX`, `XLSX`, `PPTX`, `ODF`, `DOC`, `XLS`, `PPT`, `RTF`, `WPD`, `PS`, `EPS`, `EPUB`, `CSV`, `Markdown` | `DEVS:DataTypes/` |
| `Classes/DataTypes/gfopenvideo.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/MP4`, `MOV`, `MKV`, `AVI`, `WMV`, `MPEG-PS`, `MPEG-TS`, `FLV`, `APNG`, `APNG-Name`, `NetStream` | `DEVS:DataTypes/` |
| `Classes/DataTypes/cdxl.datatype` | `SYS:Classes/DataTypes/` |
| `Classes/DataTypes/stream.datatype`, `gzip.datatype`, `brotli.datatype` | `SYS:Classes/DataTypes/` |
| `Devs/DataTypes/GZip`, `Brotli` | `DEVS:DataTypes/` |
| `Devs/DataTypes/CDXL` | `DEVS:DataTypes/` |

`gzip.datatype`, and `gfopenpng.datatype`'s PNG out, need `z.library` in
`LIBS:` (GFOpenZlib, amicradle-guest `libraries/openzlib`, or the Aminet
z.library 53).

`gfopenpicture.datatype`, `gfopensound.datatype`, `gfopendoc.datatype` and
`gfopenvideo.datatype` also need `gfopenservice.device` in `DEVS:` (GFOpenUp's
GFOpenService part installs it) and a services card or a paired Cradle.

`gfopenmodule.datatype` needs nothing else: no card and no Cradle (it then
mixes on this CPU, and plays through Paula without AHI). It plays through
AHI 6 when `ahi.device` is installed (on AmiCradle with ACAHI's driver,
`acaudio.audio`). To hand the mixing to AmiCradle's cores board it needs
`gfopenmulticore.library` in `LIBS:` (DalsinAI/gfopenmulticore, built
with its own `library/build.sh` and the os32 stove, not os32-gcc16; it is
not in GFOpenUp yet), and for `media.decode/1`, `gfopenservice.device`.

Then reboot, or run `AddDataTypes REFRESH`.

## Building

You need the os32-gcc16 compiler (bebbo's amiga-gcc on GCC 16.2 with libnix;
see DalsinAI/gfopenbrowser `stove/`), Python 3 for the descriptor files,
and the libwebp, libvpx and libxmp tarballs listed in `../SOURCES`, in the
repository's `tarballs/` folder; with `AC_HELPERS=1`, also amicradle-guest
at the commit in `../AMIGACHROME_GUEST_PINNED_COMMIT` (see the main README's
Building). Then:

```
./build.sh
```

The datatypes and their descriptors land in `out/` (set `PREFIX` to change
that). `JOBS` sets libvpx's parallel jobs. The build also leaves
`work/modbench` (`tests/modbench.c`), which times libxmp's mixing on an
Amiga.

How they are made:

- **No start-up code.** Each datatype is a plain library with its own ROMTag
  (`common/dtlib.c`): Open, Close, Expunge and ObtainEngine(). Only libnix's
  string and setjmp functions are linked in, and memory comes from
  `AllocVec()`, so several programs can decode at once.
- **The version in the ROMTag.** Its `rt_Version` is `DT_VERSION` (47, every
  datatype's version) and the revision is the one in the ID string, so
  ramlib and amicradle-guest's packed ROM weigh a copy on a disk against
  the ROM's by number and the newer runs. `tests/test-romtag.py` checks each
  datatype's `LibVersion`, `LibRevision` and ID string against it (and, given
  the built `.datatype` files, their ROMTags).
- **AC090's native helpers.** `common/dtlib.c`'s `realloc` copies, and its
  `calloc` clears, through amicradle-guest's helpers (`../achelpers/`,
  `AC_HELPERS=1`, or `auto`, the default, when the pinned commit is to hand): x86 or ARM64 code on AmiCradle, 68k code written
  for the 68020 and 68040 elsewhere, and for under 64 bytes libnix's
  `memcpy` and `memset` as before. `calloc` then asks `AllocVec()` for
  uncleared memory, since exec's clearing is 68k code. `AC_HELPERS=0`
  builds them as before.
- **A stack of their own.** libvpx runs on a 96 KB stack (`common/dtstack.c`),
  because programs and datatypes.library's helper processes may call a
  datatype with only a few kilobytes.
- **No floating point.** libwebp's one use of it, setting up dithering, is
  patched to integer arithmetic (`patches/`), so the datatypes need neither
  an FPU nor the ROM math libraries (which change the FPU's precision in the
  calling task). The one exception is `gfopenmodule.datatype`: libxmp works
  out periods in floating point, through libnix's soft floating point and
  the ROM math libraries, and does all its work in the datatype's own
  player process, which opens those libraries for itself, so the calling
  program's FPU is never touched.
- **Address 0 is memory.** On an Amiga, reading address 0 is not a fault
  (exec's pointer is at 4), so every 68k compile here, libvpx's too, has
  `-fno-delete-null-pointer-checks`: without it GCC drops null checks after
  a pointer is used and puts a trap (Software Failure 80000027) on any path
  it proves goes through a null pointer. The two such paths GCC 16 found
  are patched (`patches/`): VP9's transform size reader takes a 4x4
  largest transform without its NULL table, and libwebp's demuxer returns
  no chunk rather than read one it did not find.
- **gfopenmodule.datatype streams.** sound.datatype 47 can only play a
  sample that is all in memory, so gfopenmodule is what the V44 autodoc
  calls a streaming subclass: it answers `DTM_TRIGGER` itself and plays
  through audio.device, four buffers ahead, from a process of its own
  (`openmodule/DESIGN.md`). libxmp is built unchanged, with all its
  loaders; its depackers, ProWizard and Ogg Vorbis samples are left out
  (`openmodule/xmpglue.c`, `openmodule/novorbis.c`).
- **Descriptors** are written by `common/mkdtdesc.py`, which reproduces the
  system's own `DEVS:DataTypes/PNG` byte for byte.

## Tested

On AmigaOS 3.2.3 on AmiCradle's AC090 emulation (68040 with FPU, 256 MB),
Instance-24, 4 October 2026, with test files made by `tests/make-media.sh`.

`tests/dtpic.c` opens each picture through datatypes.library and reads it
back as ARGB. Its checksums equal `tests/webpraw.c` decoding the same files
with libwebp on x86 cores:

```
DH1:OB/webp/lossy.webp 64 48 alpha=0 24b3d77f
DH1:OB/webp/lossless-alpha.webp 40 30 alpha=1 65a9a948
DH1:OB/webp/lossy-alpha.webp 50 20 alpha=1 ca376736
DH1:OB/webp/anim.webp 32 32 alpha=0 02bb8100
```

`tests/dtanim.c` opens each clip as an animation and loads every frame with
`ADTM_LOADFRAME`. All 60 frames (30 VP8, 30 VP9, 160x120 at 10 frames a
second) have the same 256-colour pixels as `tests/webmprobe.c -chunky` on
x86 cores:

```
DH1:OB/webm/vp8.webm 160x120 depth=8 frames=30 fps=10 key=yes
frame 0 shown=0 sum=d0a7d409
frame 1 shown=1 sum=d5d0880e
...
DH1:OB/webm/vp9.webm 160x120 depth=8 frames=30 fps=10 key=yes
```

MultiView opens the VP8 clip and shows its first frame with the animation
controls. Playback inside MultiView, and running on real Amiga hardware,
are not yet checked.

### gfopenmodule.datatype

On AmigaOS 3.2.3 on AC090 (68040 with FPU), the modlab scratch copy of an
GFOpenUp install, 8 October 2026, with modules made by
`tests/make-modules.py` (synthesised samples, a 30-second tune; checked on
x86 cores first by `tests/modbench.c` built against libxmp for the host,
which renders each to a WAV file). `tests/dtsound.c` opens each through
datatypes.library and plays it as GFOpenPlay does (`DTM_TRIGGER`: play,
pause, play, stop), while the x86 cores record the instance's Paula output:

```
mods/mod.ot-4ch: datatype=gfopenmodule descriptor="ProTracker module" name="openmodule 4ch test" kind="Protracker M.K." frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/mod.ot-8ch: datatype=gfopenmodule descriptor="FastTracker MOD" name="openmodule 8ch test" kind="Scream Tracker 8CHN" frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/mod.ot-st15: datatype=gfopenmodule descriptor="SoundTracker module" name="openmodule st15" kind="unknown tracker 15 instrument" frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/med.ot-mmd0: datatype=gfopenmodule descriptor="MED module" name="openmodule MMD0 test" kind="OctaMED 3.00 MMD0" frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/ot-okt.okta: datatype=gfopenmodule descriptor="Oktalyzer module" name="ot-okt.okta" kind="Oktalyzer" frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/ot-xm.xm: datatype=gfopenmodule descriptor="FastTracker 2 module" name="openmodule XM test" kind="openmodule tests XM 1.04" frames=857948 rate=27928 period=127 sample=NULL seconds=30
mods/mod.random: NEWDTOBJECT_FAIL ioerr=2008
```

(`ot-xm.xm` with gfopensound.datatype installed too; `mod.random` is 3000
random bytes, refused as invalid data.) Each recording has sound where it
should, silence through the second's pause, and the same spectrum as the
x86 cores' render of the same module at the same rate (correlation of the log
band energies 1.00 for all six; left and right in the same balance; the
loudness over time correlates 0.6 to 0.9 with the x86 cores'). Setting
`SDTA_Volume` to 16 while it plays brings the level to 0.25 of what it
was at once; a song left to play to its end signals `SDTA_SignalTask`
after its 30.7 seconds. In GFOpenPlay the module plays as soon as it opens,
the status line names `gfopenmodule.datatype - 27928 Hz - ProTracker
module` and the bar shows 0:30; in MultiView it shows the sound icon and
plays when clicked.

`work/modbench` (10 seconds of each module, 8-bit stereo, as the datatype
mixes; share of one AC090 CPU):

| Module | Voices | 28 kHz linear | 28 kHz nearest | 16 kHz nearest |
| --- | --- | --- | --- | --- |
| ProTracker M.K. | 4 | 22.6 % | 12.4 % | 8.9 % |
| FastTracker 8CHN MOD | 8 | 33.9 % | 14.8 % | 12.2 % |
| OctaMED MMD0 | 4 | 21.7 % | | 8.4 % |
| Oktalyzer | 4 | 24.6 % | | 8.7 % |
| FastTracker 2 XM | 6 | 29.4 % | | 10.6 % |

(The table above is libxmp alone, mixing 8-bit as for Paula.)

Through AHI, with the ladder (`openmodule/DESIGN.md` sections 2 and 7),
on the same copy with the cores board fitted and AHI 6.6 with ACAHI's
Host mix on unit 0 at 44100 Hz, the test modules and a 32-channel XM,
`tests/dtsound.c LOAD=1`, two runs: chosen without being told, every
module plays on a core (4-74 % of the sound's time a job) and playing
takes 0-5 % of the main CPU. Forced onto `media.decode/1` (2 s pieces,
83-469 ms a call) 0-6 %; forced onto this CPU, 19-38 % for 4 to 8
voices and 87 % for the 32-channel XM, which breaks up. Recorded from
ACAHI's ring, every other case has no gaps but the pause the test gives,
and the spectrum and stereo balance of libxmp's render on x86 cores.
Without AHI (`ENV:GFOpenImage/ModuleOutput` `paula`) it plays through Paula
as before, and says so. GFOpenPlay's status line and Info window name the
rung, the output and the numbers.

Not yet checked: real Amiga hardware (a stock 68020 above all, where the
16 kHz default is a guess), DigiBooster, S3M, IT and the MED 2 to 4 and
StarTrekker descriptors with real files (their masks are libxmp's own
tests), modules from the wider world, `DTA_Repeat` and `DTA_Immediate`,
the ladder over a paired Cradle on the LAN, AHI modes other than ACAHI's
Host mix, and AHI on real hardware.

### stream.datatype, gzip.datatype, brotli.datatype and PNG out

On x86 cores, 23 October 2026. `tests/test-stream.sh` makes 319 test files
(`tests/make-stream.py`) and decodes each through the stream class's core
(`stream/sdtcore.c`) four ways: whole, fed and taken in 64 KB pieces, fed
and taken a byte at a time, and pulled through a hook three bytes at a
time. gzip, zlib and raw deflate come from Python's own modules (every
level, windows of 2^9 to 2^15, stored blocks, gzip's FEXTRA, FNAME,
FCOMMENT and FHCRC, two members, trailing bytes) and inflate through
GFOpenZlib's own core built for the host, as z.library would. Brotli comes
from Google's encoder (Python's `brotli` 1.2.1: qualities 0 to 11, windows
of 2^10 to 2^24, its text and font modes, flushed streams), from streams
written bit by bit from the RFC (every word length with all 121
transforms, simple prefix codes of every shape, uncompressed and metadata
meta-blocks, every window size; Google's decoder must read them as
expected too) and from small streams kept in `tests/brotli-vectors.txt`,
so the test runs without the module. All 1276 decodes give the expected
bytes, and the damaged and cut-short ones fail as they should. Then files
cut short at every length and with bytes changed, 93,280 decodes
under AddressSanitizer and UBSan: nothing read or written outside the
buffers. The cores build as C99 with `-Wall -Wextra -Werror -pedantic`
and hold no writable data.

`tests/test-pngenc.sh` encodes 51 pictures (opaque and with alpha,
premultiplied, noise and flat, 1x1 to 1023x3 and 3x700, padded rows,
levels 0, 1, 6 and 9) through `png/pngencode.c` and GFOpenZlib's deflate;
gfopenpng's own decoder, a reading of the chunks in Python (every CRC, the
filters, Python's zlib) and Pillow 12 all read every pixel back.

Built as 68040 code with GFOpenAmigaGCC 16.2.0 (no NDK headers to hand, so
the cores only), the sizes are: `sdtcore.c` 1,192 bytes, `gzcore.c` 764,
`brdecode.c` 7,184 and the dictionary and transforms 123,364 (all at
`-Os`); `pngencode.c` 2,996 at the PNG datatype's `-O2`. Not yet built
whole for the Amiga nor run on the bench; that an OS 3.2
datatypes.library takes the new group, `GID_DATA`, is still to be checked
there.

### Saving: gfopentga, webp, cdxl and gzip (47.2)

On x86 cores, 23 October 2026, under AddressSanitizer and UBSan:

- `tests/test-tgaenc.sh`: 96 pictures (noise, flat, runs of 1 to 300
  pixels across the 128-pixel packet limit, stripes, gradients, alpha;
  1x1 to 333x77) encoded four ways (run-length or not, alpha kept or not)
  through `tga/tgaencode.c`: gfopentga's own decoder reads every pixel back
  (3,125 checks) and Pillow reads all 384 files the same. The encoder is
  C99 with no writable data.
- `tests/test-webpenc.sh`: 30 pictures (noise, gradients, flat, alpha,
  clear pixels with colour, few colours; 1x1 to 200x131) encoded lossless
  at levels 0, the default and 9 through `webp/webpencode.c` and libwebp
  1.6.0 patched as for the Amiga: libwebp's own decoder reads every pixel
  back, the colour under clear pixels too (195 checks), and Pillow reads
  all 90 files the same.
- `tests/test-gzenc.sh`: six inputs (empty to 300 KB) in gzip, zlib and raw
  deflate at levels -1, 0, 1 and 9 through `gz_encode()` and GFOpenZlib's
  core as z.library: Python's zlib inflates all 72 files to their input,
  and a write that fails stops the encode with its error.

Built with GFOpenAmigaGCC 16.2.0 (open3) and the Team's GFOpenNDK headers:
gfopentga.datatype 10,712 bytes (8,748 before), cdxl.datatype 13,128,
gzip.datatype 7,392 (6,516), stream.datatype 8,360, webp.datatype 226,068
(142,048: the lossless encoder), with no warnings; webp.datatype's link
map has no floating-point helper. Not yet run on an Amiga: `tests/dtsave.c`
(`tests/build-dtsave.sh`) is the check to run there, a picture saved and
opened again pixel for pixel, a conversion through an empty object of
another class, and SAMEBYTES for a CDXL film.

### gfopengif.datatype

On x86 cores, 3 November 2026: `tests/test-gif.sh` makes 29 files (61
frames) with its own LZW encoder: stills of 2 to 256 colours, interlaced,
transparent, local tables, offset and past-the-edge frames, no screen
size, no trailer, indexes past the table, clear codes mid-stream, and
animations with each disposal method, a mixed one and an advertisement's
small first frame. Every frame's screen matches the one `make-gif.py`
works out, Pillow reads the 16 full-screen one-frame files the same, and
three files from Pillow's encoder match Pillow's decode. Every file cut at
every length and with each of its first 64 bytes changed (46,764 files)
decodes under AddressSanitizer and UBSan without a fault. Animations Pillow
writes (eight frames, disposal 0, 1 and 2, with and without transparency,
optimised or not) give Pillow's own frames.

Not yet checked: the Amiga build and a run on AmiCradle (MultiView,
GFOpenBrowser), GIFs from the wider world, and the speed on a real 68k.

The writer (47.4), on x86 cores, 5 November 2026: `tests/test-gifenc.sh`
encodes 445 pictures (1x1 to 1500x700; 1 to 256 colours with and without
transparent pixels, runs, gradients, noise of every colour, and colour
indexes with tables of 1 to 256), reads each back with our decoder under
AddressSanitizer and UBSan, and has Pillow read each one too: 2,286
checks, every one as written where there are 256 colours or fewer, and
Pillow sees all 445 as our decoder does. The 1500x700 pictures fill the
LZW table and clear it many times at every minimum code size. Median cut
on uniform noise leaves an average error of 10 a channel (the most 256
colours can do for it), on a 640x480 gradient 7. `gifencode.c` builds as
C99 with no writable data, to run as a core job later; the Amiga build has
no floating-point helper (`__udivdi3` only, for the colour averages).

### gfopentga.datatype

On x86 cores, 12 October 2026: `tests/test-tga.sh` makes 60 files with
`tests/make-tga.py` (53 pictures of every kind above, uncompressed, RLE
with packets inside rows and across them, bottom-up, top-down and right to
left, plus 7 that are not TGAs it reads), decodes each with
`tga/tgadecode.c` built for the host (`tests/tgatest.c`) and compares every
byte of every picture with what make-tga.py works out: all 60 as expected.
Pillow 12.1.1 reads 24 of them the same way; the others are kinds it reads
otherwise or not at all (make-tga.py lists them). Then each file cut short at every
length and with every header byte changed, 136,452 files, under
AddressSanitizer and UBSan: nothing read or written outside the file or
the picture.

On AmigaOS 3.2.3 on AC090 (68040 with FPU), a lab copy of Instance-32,
12 October 2026, built with os32-gcc16: `tests/dtpic.c` opens each file
through datatypes.library and reads it back as ARGB. All 53 pictures give
the checksums in make-tga.py's `expected.txt`, and the 7 others fail as
invalid data (2008). A name in capitals (`PIC.TGA`) and `.targa` open too.
3.2.3's MultiView, GFOpenView 0.5 and GFOpenPlay 0.1.3 show a 480 x 320 32-bit
picture with alpha (GFOpenPlay: "gfopentga.datatype - 480 x 320 - Targa
picture - decoded by this Amiga"), and a 1920 x 1080 RLE TGA named in
`WBPattern.prefs` is the Workbench backdrop.

Not yet checked: real Amiga hardware, TGA files from the wider world (every
test file is make-tga.py's), 8-bit screens, and the speed on a real 68k.

## Known issues

- picture.datatype 47.19 keeps the alpha of a picture's last pixel only when
  its depth is 32, so `webp.datatype` gives pictures with alpha a depth of 32.
- `gfopenpicture.datatype`: not yet run on the bench. Rotation and mirroring
  stored in AVIF files are not applied yet (HEIC's are), nor EXIF rotation.
  No 68k decoder. Camera RAW is chosen by its name, ahead of a TIFF
  datatype, and SVG by its name, ahead of the text datatype. PCX, ICO
  and the other classic formats are left to the Amiga's own datatypes, which
  work without a Cradle (`media.decode/1` still reads ICO, for GFOpenBrowser's
  site icons); TGA has its own, `gfopentga.datatype`. An APNG is told by an acTL chunk straight after IHDR, or by
  the name `.apng`; others show as a still PNG. An APNG's frames come
  composited on black, without alpha (animation.datatype has none), at a
  steady rate that keeps its uneven frame delays; it repeats when its
  play count is not one. Animated WebP stays a still in `webp.datatype` until the
  Cradle's FFmpeg decodes WebP animation. An OpenType font with TrueType
  outlines is told by its `.otf` name.
- Text formats (SVG, Radiance HDR's header, PostScript, RTF, CSV and
  Markdown) have text descriptors (DTF_ASCII): datatypes.library only tries
  those on a file that reads as text.
- Pictures in JPEG stay with the system's datatypes (PNG and GIF have
  gfopenpng and gfopengif);
  `media.decode/1` decodes them for GFOpenBrowser.
- AmigaOS 3.2.3's datatypes.library tries a longer mask first and, between
  equal masks, priority 0 before higher ones (`openmodule/DESIGN.md`), so
  the priority 1 given to APNG, MKV, Camera RAW, SVG, CSV and Markdown
  above does not put them first there; not yet re-checked for those.
- `gfopenmodule.datatype`: PowerPacker and XPK packed modules don't open
  (libxmp's depackers are left out), nor XM files with Ogg Vorbis samples.
  A file named `mod.something` or `something.mod` that no other datatype
  knows is offered to it and refused as invalid data. Through AHI, a
  volume change is heard from the next half-second request, and a pause
  skips what was queued (up to a second). Through Paula (no AHI) it is
  8-bit, and each module playing takes two of Paula's four channels, so two
  can play at once and a third stays silent. Saving writes the module as
  it came; there is no copy to the clipboard. libxmp's floating point goes through the ROM math libraries, in
  the player process only, and in a cores board job too (library code read
  where it is: it works, but GFOpenMulticore's rules say no library calls,
  and a strict job over the LAN could not do it). The rung is chosen when a
  module opens. gfopenmulticore.library 0.1 built with os32-gcc16
  passes jobs the wrong arguments (OMCTest's first job ran for minutes,
  even on the main CPU); built as its `build.sh` says, with the os32 stove
  (GCC 6.5), OMCTest passes and gfopenmodule's jobs run.
- `gfopensound.datatype`: not yet run on the bench. To check there: the
  sample length sound.datatype V44+ expects for 16-bit samples (frames are
  given), and that it frees the sample with FreeVec(). The Ogg and WMA
  descriptors also match Theora and WMV video, which it refuses.
- `gfopendoc.datatype`: not yet run on the bench. Every page is drawn at the
  first page's size. Office Open XML and Office 97 files are told apart by
  their names (`.docx`, `.xls`...), as they share a ZIP or OLE2 header with
  other files. CSV and Markdown are chosen by their names. No text view
  without a Cradle yet.
- `cdxl.datatype`: not yet run on the bench. The core is checked on the
  host and as 68040 code under qemu-m68k (`tests/test-cdxl.sh`): ten test
  films in every layout it plays show byte for byte what they were made
  with, and what FFmpeg's own CDXL decoder shows, read as the datatype
  reads them (one-size films opened with two reads and the length, a read
  a frame in order, films whose frame sizes vary indexed frame by frame,
  and one whose sizes vary yet add up found when its second frame isn't
  there); nearly 1,900 films cut short or with a header byte changed read
  nothing outside the file. Frames
  are read from a handle of its own (DupLockFromFH, OpenFromLock), so a
  handler without those (a CD's, say) is not yet played. Type 0 (24-bit
  palette) frames are not played.
- `gfopenvideo.datatype`: not yet run on the bench. HAM8, streams, sound with
  each frame (VSOUND) and files in parts (VUPLOAD) need a Cradle or card
  whose media.decode/1 has them (gfopenservice, 20 October 2026). A file
  up to `VideoChunk` KB is read into memory and sent once. Each
  frame opens the service afresh, because animation.datatype loads frames
  from its own process. MKV is chosen by its name and tried before
  webm.datatype (both are EBML). Each frame is a planar bitmap in chip RAM:
  when the largest free chip block holds fewer than four frames, the video
  is asked for at half the size (down to 160 wide), and running out says
  "not enough memory" rather than nothing.
- `webm.datatype`: sound (Vorbis or Opus) only through a services card or a
  paired Cradle whose media.decode/1 has VUPLOAD and VSOUND (gfopenservice,
  20 October 2026), as 8-bit mono, for files up to 1 GB: as gfopenvideo.datatype,
  a clip with an audio track goes up in 256 KB parts when it opens and is
  kept open there, and each frame's sound comes as the frame loads, into a
  ring of 64 frames in chip RAM (`tests/test-webm-read.sh`); an older
  service plays it silent; 256 colours only
  (VP9 in 4:2:0, 4:4:4 or RGB; 8-bit only); VP9 decoded in software is slow on a real 68k;
  files over 4 GB and laced video blocks are not supported.

## Licence

The datatype code (`build.sh`, `common/`, `include/`, `webp/`, `webm/`, `openpicture/`, `png/`, `tga/`, `stream/`, `gzip/`, `brotli/`, `opensound/`, `openmodule/`, `opendoc/`, `openvideo/`, `cdxl/`, `tests/`) is
MIT, Copyright (c) 2026 AmiCradle Team, as the rest of this repository.
libwebp and libvpx keep their BSD licences and the WebM Project's patent
grants (`../upstream/libwebp/`, `../upstream/libvpx/`); a patch to their
source stays under their licence. libxmp, built unchanged into
`openmodule.datatype`, keeps its MIT licence (Copyright (C) 1996-2026
Claudio Matsuoka and Hipolito Carraro Jr; `../upstream/libxmp/COPYING`,
and `CREDITS` for the code it carries from others).
