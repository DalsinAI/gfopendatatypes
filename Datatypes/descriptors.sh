# The DEVS:DataTypes descriptors of every datatype the Team builds, written
# by common/mkdtdesc.py: sourced by build.sh and the per-datatype build
# scripts, each of which calls its datatype's function, and by the
# recognition test (tests/recognition/test_recognition.py), which writes
# them all and asks datatypes.library's recognition (libraries/opendatatypes/
# dt_core.c) which one each sample file gets. One place, so the test checks
# what ships.
#
# How datatypes.library picks (dt_core.c, measured on AmigaOS 3.2): only the
# descriptors of the data's kind are tried (binary, text or IFF: DT_TEXT,
# DT_IFF); among them a longer mask first (ANY counts), then the LOWER
# priority as an unsigned word (0 before 1, 1 before -1), then the one added
# last; the mask, then the pattern, then the recognition code (DT_CODE, a
# hook in hooks/) must all agree. A descriptor with no mask needs a pattern
# other than "#?" and comes after every one with a mask. So to come ahead
# of another descriptor, make the mask longer, never rely on DT_PRIORITY.
#
# Needs HERE (this directory) and, for the descriptors with recognition
# code, DTHOOKS: the folder hooks/build.sh wrote the hooks to.
#
# MIT, Copyright (c) 2026 AmiCradle Team.

# desc FILE NAME CLASS GROUP ID PATTERN VERSION MASK...: one descriptor in
# $DESC_OUT/Devs/DataTypes (DT_TEXT, DT_IFF, DT_CASE, DT_CODE and
# DT_PRIORITY pass through to mkdtdesc.py)
desc() {
    _f=$1; shift
    python3 "$HERE/common/mkdtdesc.py" "$DESC_OUT/Devs/DataTypes/$_f" "$@"
    echo "Devs/DataTypes/$_f: $(wc -c < "$DESC_OUT/Devs/DataTypes/$_f") bytes"
}
chars() {        # each byte of $1 as a mask item, in hex (spaces and all)
    printf %s "$1" | od -An -v -tx1 | sed 's/[0-9a-f][0-9a-f]/0x&/g'
}
anys() {         # $1 ANY mask items
    _i=0; while [ "$_i" -lt "$1" ]; do printf 'ANY '; _i=$((_i+1)); done
}
hook() {         # the load file of hook $1, for DT_CODE (mkdtdesc.py refuses a missing one)
    printf %s "${DTHOOKS:-DTHOOKS-unset}/$1"
}

# --- webp.datatype ------------------------------------------------------------
desc_webp() {
    DESC_OUT=$1
    # RIFF, any length, WEBP.
    desc WebP WebP webp pict webp "#?" \
        '$VER: WebP 47.1 (4.10.2026)' "'R'" "'I'" "'F'" "'F'" ANY ANY ANY ANY "'W'" "'E'" "'B'" "'P'"
}

# --- webm.datatype ------------------------------------------------------------
desc_webm() {
    DESC_OUT=$1
    # The EBML magic; datatypes.library then offers the file to webm.datatype,
    # which checks the document type ("webm" or "matroska"). gfopenvideo's
    # MKV, a longer mask, takes Matroska first when it is installed.
    desc WebM WebM webm anim webm "#?" \
        '$VER: WebM 47.1 (4.10.2026)' 0x1a 0x45 0xdf 0xa3
}

# --- gfopenpicture.datatype -----------------------------------------------------
desc_gfopenpicture() {
    DESC_OUT=$1
    # An ISO BMFF ftyp box (any size) with the brand: one descriptor per brand.
    # shellcheck disable=SC2046
    for d in "AVIF:AVIF:avif" "AVIS:AVIF sequence:avis" "HEIC:HEIC:heic" "HEIX:HEIC (heix):heix" "HEIF:HEIF:mif1"; do
        file=${d%%:*}; rest=${d#*:}; title=${rest%%:*}; brand=${rest#*:}
        picdesc "$file" "$title" "$brand" "#?" ANY ANY ANY ANY $(chars "ftyp$brand")
    done
    picdesc JXL "JPEG XL" jxl "#?" 0xff 0x0a
    # shellcheck disable=SC2046
    picdesc JXL-ISO "JPEG XL (container)" jxl "#?" 0 0 0 0x0c $(chars "JXL ")
    picdesc EXR OpenEXR exr "#?" 0x76 0x2f 0x31 0x01
    # Its header is text: a file whose first block reads as text needs the text
    # descriptor, one with pixels early on the binary one.
    # shellcheck disable=SC2046
    DT_TEXT=1 picdesc HDR "Radiance HDR" hdr "#?" $(chars "#?RADIANCE")
    # shellcheck disable=SC2046
    picdesc HDR-Bin "Radiance HDR (binary)" hdr "#?" $(chars "#?RADIANCE")
    # shellcheck disable=SC2046
    picdesc PSD "Photoshop" psd "#?" $(chars "8BPS")
    # shellcheck disable=SC2046
    picdesc XCF "GIMP picture" xcf "#?" $(chars "gimp xcf")
    # shellcheck disable=SC2046
    picdesc QOI QOI qoi "#?" $(chars "qoif")
    # shellcheck disable=SC2046
    picdesc DDS "DirectDraw surface" dds "#?" $(chars "DDS ")
    # shellcheck disable=SC2046
    picdesc JP2 "JPEG 2000" jp2 "#?" 0 0 0 0x0c $(chars "jP  ")
    picdesc J2K "JPEG 2000 codestream" j2k "#?" 0xff 0x4f 0xff 0x51
    # OpenRaster and Krita: a ZIP whose first entry is the mimetype; comics by name.
    # shellcheck disable=SC2046
    picdesc ORA OpenRaster ora "#?" $(chars "PK") 3 4 $(anys 26) $(chars "mimetypeimage/openraster")
    # shellcheck disable=SC2046
    picdesc KRA "Krita picture" kra "#?" $(chars "PK") 3 4 $(anys 26) $(chars "mimetypeapplication/x-krita")
    # shellcheck disable=SC2046
    picdesc CBZ "Comic book" cbz "#?.cbz" $(chars "PK") 3 4
    # SVG is drawn on the host at the picture's own size (media_svg.c); it is
    # text (DT_TEXT), so the name tells it, ahead of the ascii datatype.
    DT_TEXT=1 DT_PRIORITY=1 desc SVG "SVG drawing" gfopenpicture pict svg "#?.svg" '$VER: SVG 47.2 (7.11.2026)'
    # SVGZ is gzipped SVG, binary: gzip's magic and its method (gzip's own
    # descriptor) and one byte more, so it is tried first, by its name.
    desc SVGZ "SVG drawing (compressed)" gfopenpicture pict svgz "#?.svgz" '$VER: SVGZ 47.2 (7.11.2026)' \
        0x1f 0x8b 0x08 ANY
    # Fonts as ImageMagick's sample sheet.
    picdesc TTF "TrueType font" ttf "#?.(ttf|ttc)"
    # shellcheck disable=SC2046
    picdesc OTF "OpenType font" otf "#?" $(chars "OTTO")
    # OpenType with TrueType outlines starts as a TrueType font does.
    picdesc OTF-TT "OpenType font (TrueType outlines)" otf "#?.otf" 0 1 0 0
    # Camera RAW is TIFF (or its own thing) inside: the name tells it, ahead of
    # the TIFF datatype.
    DT_PRIORITY=1 picdesc RAW "Camera RAW" raw \
        "#?.(cr2|cr3|crw|nef|nrw|arw|srf|sr2|dng|orf|rw2|raf|pef|srw|x3f|erf|kdc|dcr|mrw|3fr|iiq|rwl)"
}
picdesc() {      # FILE NAME ID PATTERN MASK...
    _p=$1; _n=$2; _id=$3; _pat=$4; shift 4
    desc "$_p" "$_n" gfopenpicture pict "$_id" "$_pat" "\$VER: $_p 47.1 (5.10.2026)" "$@"
}

# --- gfopenpng.datatype ---------------------------------------------------------
desc_gfopenpng() {
    DESC_OUT=$1
    # The signature and the IHDR chunk that must come next (length 13): sixteen
    # bytes, longer than the system's PNG descriptor, so it is tried first.
    desc GFOpenPNG GFOpenPNG gfopenpng pict png "#?" \
        '$VER: GFOpenPNG 47.3 (11.10.2026)' 0x89 "'P'" "'N'" "'G'" 0x0d 0x0a 0x1a 0x0a 0x00 0x00 0x00 0x0d \
        "'I'" "'H'" "'D'" "'R'"
}

# --- gfopentga.datatype ---------------------------------------------------------
desc_gfopentga() {
    DESC_OUT=$1
    # A TGA file starts with no signature (its first bytes are lengths and
    # kinds), so the name tells it: Truevision's .tga, and .targa, .icb, .vda
    # and .vst. With no mask it is tried after every descriptor that has one.
    desc GFOpenTGA "Targa picture" gfopentga pict tga \
        "#?.(tga|targa|icb|vda|vst)" '$VER: GFOpenTGA 47.1 (12.10.2026)'
}

# --- gfopengif.datatype ---------------------------------------------------------
desc_gfopengif() {
    DESC_OUT=$1
    # "GIF87a" or "GIF89a" and the four bytes of the screen size: ten bytes, a
    # longer mask than the system's GIF descriptor, so it is tried first.
    desc GFOpenGIF GFOpenGIF gfopengif pict gif "#?" \
        '$VER: GFOpenGIF 47.1 (3.11.2026)' "'G'" "'I'" "'F'" "'8'" ANY "'a'" ANY ANY ANY ANY
}

# --- gzip.datatype, brotli.datatype ---------------------------------------------
desc_stream() {
    DESC_OUT=$1
    # gzip: 1f 8b and method 8 (RFC 1952). Brotli has no signature: by name.
    desc GZip GZip gzip data gzip "#?" \
        '$VER: GZip 47.1 (23.10.2026)' 0x1f 0x8b 0x08
    desc Brotli Brotli brotli data brot "#?.br" \
        '$VER: Brotli 47.1 (23.10.2026)'
}

# --- gfopensound.datatype -------------------------------------------------------
desc_gfopensound() {
    DESC_OUT=$1
    sounddesc FLAC FLAC flac "'f'" "'L'" "'a'" "'C'"
    # Ogg Vorbis, Opus, FLAC; gfopenvideo's Ogg-Theora (a longer mask) takes
    # the films first.
    sounddesc Ogg "Ogg sound" ogg "'O'" "'g'" "'g'" "'S'"
    # shellcheck disable=SC2046
    sounddesc M4A "MPEG-4 audio" m4a $(anys 4) $(chars "ftypM4A ")
    # shellcheck disable=SC2046
    sounddesc M4B "MPEG-4 audiobook" m4b $(anys 4) $(chars "ftypM4B ")
    # An .m4a with another brand (mp42, isom, dash...): by its name. MP4 video
    # (gfopenvideo) has the same mask and other names.
    # shellcheck disable=SC2046
    desc M4A-Name "MPEG-4 audio (by name)" gfopensound soun m4a "#?.(m4a|m4b|m4r)" \
        '$VER: M4A-Name 47.2 (7.11.2026)' $(anys 4) $(chars "ftyp")
    # ASF holds both: Windows Media audio by its name here, video in gfopenvideo.
    desc WMA "Windows Media audio" gfopensound soun wma "#?.wma" \
        '$VER: WMA 47.1 (5.10.2026)' 0x30 0x26 0xb2 0x75 0x8e 0x66 0xcf 0x11
    sounddesc MP3-ID3 "MP3 (ID3)" mp3 "'I'" "'D'" "'3'"
    # MP3 (and MP2) with no ID3v2 tag: a frame header, FF Ex or FF Fx; the
    # hook checks it is a valid Layer II or III one and that the next frame
    # follows it (hooks/dthooks.h, dth_mpegaudio).
    DT_CODE=$(hook mpegaudio) DT_CASE=1 desc MP3 "MPEG audio" gfopensound soun mp3 "#?" \
        '$VER: MP3 47.2 (7.11.2026)' 0xff ANY
    # AAC in ADTS frames: MPEG-4 (FF F1) and MPEG-2 (FF F9), with no CRC and
    # with one (FF F0, FF F8); and ADIF, a header and raw AAC.
    sounddesc AAC "AAC (ADTS)" aac 0xff 0xf1
    DT_CASE=1 desc AAC-CRC "AAC (ADTS, CRC)" gfopensound soun aac "#?" '$VER: AAC-CRC 47.2 (7.11.2026)' 0xff 0xf0
    DT_CASE=1 desc AAC-MPEG2 "AAC (ADTS, MPEG-2)" gfopensound soun aac "#?" '$VER: AAC-MPEG2 47.2 (7.11.2026)' 0xff 0xf9
    DT_CASE=1 desc AAC-MPEG2-CRC "AAC (ADTS, MPEG-2, CRC)" gfopensound soun aac "#?" \
        '$VER: AAC-MPEG2-CRC 47.2 (7.11.2026)' 0xff 0xf8
    # shellcheck disable=SC2046
    DT_CASE=1 desc ADIF "AAC (ADIF)" gfopensound soun aac "#?" '$VER: ADIF 47.2 (7.11.2026)' $(chars "ADIF")
    # Tunes the host plays: MIDI through FluidSynth, SID through sidplayfp.
    # shellcheck disable=SC2046
    sounddesc MIDI MIDI midi $(chars "MThd") 0 0 0 6
    # shellcheck disable=SC2046
    sounddesc PSID "C64 SID tune" psid $(chars "PSID")
    # shellcheck disable=SC2046
    sounddesc RSID "C64 SID tune (RSID)" rsid $(chars "RSID")
    # PC tracker modules through libopenmpt, when gfopenmodule.datatype (below,
    # played on the Amiga itself, tried first) is not installed.
    # shellcheck disable=SC2046
    sounddesc XM "FastTracker module" xm $(chars "Extended Module: ")
    # shellcheck disable=SC2046
    sounddesc IT "Impulse Tracker module" it $(chars "IMPM")
    # shellcheck disable=SC2046
    sounddesc S3M "Scream Tracker module" s3m $(anys 44) $(chars "SCRM")
}
sounddesc() {    # FILE NAME ID MASK...
    _s=$1; _n=$2; _id=$3; shift 3
    desc "$_s" "$_n" gfopensound soun "$_id" "#?" "\$VER: $_s 47.1 (5.10.2026)" "$@"
}

# --- gfopenmodule.datatype -----------------------------------------------------
desc_gfopenmodule() {
    DESC_OUT=$1
    # 31-instrument MODs carry their kind at byte 1080: ProTracker (M.K., and
    # M!K! past 64 patterns), StarTrekker (FLT4, FLT8), FastTracker (2CHN to
    # 9CHN) and TakeTracker (10CH to 32CH). datatypes.library reads as much of
    # a file as its longest mask, so these masks run to byte 1083.
    # shellcheck disable=SC2046
    moddesc ProTracker "ProTracker module" mod "#?" $(anys 1080) $(chars "M.K.")
    # shellcheck disable=SC2046
    moddesc ProTracker-100 "ProTracker module (over 64 patterns)" mod "#?" $(anys 1080) $(chars "M!K!")
    # shellcheck disable=SC2046
    moddesc StarTrekker "StarTrekker module" mod "#?" $(anys 1080) $(chars "FLT4")
    # shellcheck disable=SC2046
    moddesc StarTrekker-8 "StarTrekker module (8 channels)" mod "#?" $(anys 1080) $(chars "FLT8")
    # shellcheck disable=SC2046
    moddesc MOD-xCHN "FastTracker MOD" mod "#?" $(anys 1081) $(chars "CHN")
    # shellcheck disable=SC2046
    moddesc MOD-xxCH "TakeTracker MOD" mod "#?" $(anys 1082) $(chars "CH")
    # The 15-instrument SoundTracker MOD has no signature: by its name only
    # (mod.name, as on the Amiga, or name.mod), and libxmp checks the header.
    # With no mask it is tried after every descriptor that has one.
    moddesc SoundTracker "SoundTracker module" mod "(mod.#?|#?.mod)"
    # shellcheck disable=SC2046
    moddesc OctaMED "MED module" mmd "#?" $(chars "MMD") ANY
    # shellcheck disable=SC2046
    moddesc MED-2 "MED 2 module" med "#?" $(chars "MED") 2
    # shellcheck disable=SC2046
    moddesc MED-3 "MED 3 module" med "#?" $(chars "MED") 3
    # shellcheck disable=SC2046
    moddesc MED-4 "MED 4 module" med "#?" $(chars "MED") 4
    # shellcheck disable=SC2046
    moddesc Oktalyzer "Oktalyzer module" okt "#?" $(chars "OKTASONG")
    # shellcheck disable=SC2046
    moddesc DigiBooster "DigiBooster module" digi "#?" $(chars "DIGI Booster module")
    # shellcheck disable=SC2046
    moddesc DigiBooster-Pro "DigiBooster Pro module" dbm "#?" $(chars "DBM0")
    # FastTracker 2, Scream Tracker 3 and Impulse Tracker: gfopensound.datatype
    # has descriptors for these too (played by libopenmpt on the Cradle).
    # AmigaOS 3.2's datatypes.library tries longer masks first and does not go
    # by priority (openmodule/DESIGN.md), so these masks are one byte longer
    # than gfopensound's (ANY: an XM, S3M or IT is never that short), and these
    # play here when both are installed.
    # shellcheck disable=SC2046
    moddesc Module-XM "FastTracker 2 module" xm "#?" $(chars "Extended Module: ") ANY
    # shellcheck disable=SC2046
    moddesc Module-S3M "Scream Tracker 3 module" s3m "#?" $(anys 44) $(chars "SCRM") ANY
    # shellcheck disable=SC2046
    moddesc Module-IT "Impulse Tracker 2 module" it "#?" $(chars "IMPM") ANY
}
moddesc() {      # FILE NAME ID PATTERN MASK...
    _m=$1; _n=$2; _id=$3; _pat=$4; shift 4
    desc "$_m" "$_n" gfopenmodule soun "$_id" "$_pat" "\$VER: $_m 47.1 (8.10.2026)" "$@"
}

# --- gfopendoc.datatype ---------------------------------------------------------
desc_gfopendoc() {
    DESC_OUT=$1
    # Office Open XML files are ZIP files: the name tells them apart.
    docdesc DOCX "Word document" docx "#?.(docx|docm|dotx)" "'P'" "'K'" 3 4
    docdesc XLSX "Excel workbook" xlsx "#?.(xlsx|xlsm|xltx)" "'P'" "'K'" 3 4
    docdesc PPTX "PowerPoint presentation" pptx "#?.(pptx|pptm|ppsx|potx)" "'P'" "'K'" 3 4
    # OpenDocument: its first entry is the mimetype, stored.
    # shellcheck disable=SC2046
    docdesc ODF OpenDocument odf "#?" "'P'" "'K'" 3 4 $(anys 26) $(chars "mimetypeapplication/vnd.oasis.opendocument.")
    # Office 97-2003 files are OLE2 compound files: the name tells them apart.
    docdesc DOC "Word 97 document" doc "#?.(doc|dot)" 0xd0 0xcf 0x11 0xe0 0xa1 0xb1 0x1a 0xe1
    docdesc XLS "Excel 97 workbook" xls "#?.(xls|xlt)" 0xd0 0xcf 0x11 0xe0 0xa1 0xb1 0x1a 0xe1
    docdesc PPT "PowerPoint 97 presentation" ppt "#?.(ppt|pps|pot)" 0xd0 0xcf 0x11 0xe0 0xa1 0xb1 0x1a 0xe1
    DT_TEXT=1 docdesc RTF "Rich Text Format" rtf "#?" "'{'" "'\\'" "'r'" "'t'" "'f'"
    docdesc WPD WordPerfect wpd "#?" 0xff "'W'" "'P'" "'C'"
    # PostScript starts "%!": "%!PS-Adobe-3.0" as a rule, a bare "%!" from
    # older programs. Text, or binary when it carries 8-bit data (a binary
    # image, Latin-1 in a C1 control's place) in its first block.
    # shellcheck disable=SC2046
    DT_TEXT=1 desc PS PostScript gfopendoc pict ps "#?" '$VER: PS 47.2 (7.11.2026)' $(chars "%!")
    # shellcheck disable=SC2046
    desc PS-Bin "PostScript (binary)" gfopendoc pict ps "#?" '$VER: PS-Bin 47.2 (7.11.2026)' $(chars "%!")
    # DOS EPS: a binary header (C5 D0 D3 C6) before the PostScript and the
    # preview; compared exactly (DT_CASE) or E5 F0 F3 E6 would do too.
    DT_CASE=1 desc EPS "EPS with preview" gfopendoc pict eps "#?" '$VER: EPS 47.2 (7.11.2026)' 0xc5 0xd0 0xd3 0xc6
    # EPUB is a ZIP whose first entry is its mimetype, as OpenDocument's.
    # shellcheck disable=SC2046
    docdesc EPUB "EPUB e-book" epub "#?" "'P'" "'K'" 3 4 $(anys 26) $(chars "mimetypeapplication/epub+zip")
    # Text with no signature: by name, ahead of the ascii datatype, when the
    # service can be there to lay it out (a services card, or a Cradle paired;
    # hooks/docservice.c). Without it the ascii datatype shows it as text.
    DT_TEXT=1 DT_PRIORITY=1 DT_CODE=$(hook docservice) desc CSV "CSV table" gfopendoc pict csv "#?.(csv|tsv)" \
        '$VER: CSV 47.2 (7.11.2026)'
    DT_TEXT=1 DT_PRIORITY=1 DT_CODE=$(hook docservice) desc Markdown Markdown gfopendoc pict mdwn "#?.(md|markdown)" \
        '$VER: Markdown 47.2 (7.11.2026)'
}
docdesc() {      # FILE NAME ID PATTERN MASK...
    _d=$1; _n=$2; _id=$3; _pat=$4; shift 4
    desc "$_d" "$_n" gfopendoc pict "$_id" "$_pat" "\$VER: $_d 47.1 (5.10.2026)" "$@"
}

# --- cdxl.datatype ------------------------------------------------------------
desc_cdxl() {
    DESC_OUT=$1
    # CDXL has no signature: a standard frame's type byte (1), by name.
    desc CDXL CDXL cdxl anim cdxl "#?.(cdxl|xl)" \
        '$VER: CDXL 47.1 (20.10.2026)' 0x01
}

# --- gfopenvideo.datatype -------------------------------------------------------
desc_gfopenvideo() {
    DESC_OUT=$1
    # APNG, played by the host like a video without sound, ahead of the PNG
    # datatypes: a PNG whose acTL chunk follows IHDR straight away (most
    # encoders), or any PNG named .apng (acTL may come later). Both masks are
    # longer than GFOpenPNG's sixteen bytes, so they are tried first.
    # shellcheck disable=SC2046
    desc APNG "Animated PNG" gfopenvideo anim apng "#?" '$VER: APNG 47.2 (7.11.2026)' \
        0x89 $(chars "PNG") 0x0d 0x0a 0x1a 0x0a $(anys 29) $(chars "acTL")
    # shellcheck disable=SC2046
    desc APNG-Name "Animated PNG (by name)" gfopenvideo anim apng "#?.apng" '$VER: APNG-Name 47.2 (7.11.2026)' \
        0x89 $(chars "PNG") 0x0d 0x0a 0x1a 0x0a 0 0 0 0x0d $(chars "IHDR") ANY
    videodesc MP4 "MPEG-4 video" mp4 "#?.(mp4|m4v|mov|3gp|3g2)" ANY ANY ANY ANY "'f'" "'t'" "'y'" "'p'"
    videodesc MOV "QuickTime movie" mov "#?.mov" ANY ANY ANY ANY "'m'" "'o'" "'o'" "'v'"
    # Matroska shares WebM's EBML header: one byte longer than WebM's mask, so
    # tried first, and the hook takes only DocType "matroska"
    # (hooks/dthooks.h, dth_matroska); WebM goes on to webm.datatype.
    DT_CODE=$(hook matroska) DT_CASE=1 desc MKV "Matroska video" gfopenvideo anim mkv "#?" \
        '$VER: MKV 47.2 (7.11.2026)' 0x1a 0x45 0xdf 0xa3 ANY
    videodesc AVI "AVI video" avi "#?" "'R'" "'I'" "'F'" "'F'" ANY ANY ANY ANY "'A'" "'V'" "'I'" "' '"
    videodesc WMV "Windows Media video" wmv "#?.(wmv|asf)" 0x30 0x26 0xb2 0x75 0x8e 0x66 0xcf 0x11
    videodesc MPEG-PS "MPEG video" mpg "#?" 0x00 0x00 0x01 0xba
    videodesc MPEG-TS "MPEG transport stream" mts "#?.(ts|m2ts|mts)" 0x47
    # BDAV (.m2ts, AVCHD's .mts): 192-byte packets, a 4-byte time code before
    # each 188-byte TS packet, so the sync byte (0x47) is at 4, 196 and 388.
    # shellcheck disable=SC2046
    DT_CASE=1 desc M2TS "MPEG transport stream (BDAV)" gfopenvideo anim mts "#?" '$VER: M2TS 47.2 (7.11.2026)' \
        $(anys 4) 0x47 $(anys 191) 0x47 $(anys 191) 0x47
    videodesc FLV "Flash video" flv "#?" "'F'" "'L'" "'V'"
    # Ogg Theora: one byte longer than gfopensound's Ogg, so tried first; the
    # hook takes an Ogg one of whose first pages starts a Theora stream
    # (hooks/dthooks.h, dth_oggtheora). gfopensound refuses films.
    DT_CODE=$(hook oggtheora) DT_CASE=1 desc Ogg-Theora "Ogg Theora video" gfopenvideo anim ogv "#?" \
        '$VER: Ogg-Theora 47.2 (7.11.2026)' "'O'" "'g'" "'g'" "'S'" 0
    # A stream: what the NETSTREAM: handler gives for a channel (or one saved
    # to disk), "#NETSTREAM" then NAME= and URL= lines; the host plays the URL.
    # Text, so it needs DT_TEXT or the ascii datatype takes it.
    # shellcheck disable=SC2046
    DT_TEXT=1 videodesc NetStream "Network stream" nets "#?" $(chars "#NETSTREAM")
}
videodesc() {    # FILE NAME ID PATTERN MASK...
    _v=$1; _n=$2; _id=$3; _pat=$4; shift 4
    desc "$_v" "$_n" gfopenvideo anim "$_id" "$_pat" "\$VER: $_v 47.1 (5.10.2026)" "$@"
}

# Every one of them, as the pack has them (the recognition test).
desc_all() {
    for _c in webp webm gfopenpicture gfopenpng gfopentga gfopengif stream gfopensound gfopenmodule \
        gfopendoc cdxl gfopenvideo; do
        "desc_$_c" "$1"
    done
}
