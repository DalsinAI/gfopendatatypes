# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created gfopenimage, designs it and maintains it.

## The AmiCradle team

We are The AmiCradle Team, who build AmiCradle alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, who started the work on SacredTrees's x86 cores.
- **The threads**, each one taking a piece of the work from design to release.

## Copyright holder

Our Amiga build of the libraries (the build scripts, patches, configuration
headers, `achelpers/` and tests) and our datatypes are Copyright (c) 2026
AmiCradle Team, released under the MIT licence (`LICENSE`).

## Third-party work in this repository

The libraries' own notices are kept in `upstream/`; the sources themselves are
fetched at build time (below).

| Component | Where | Authors | Licence |
| --- | --- | --- | --- |
| zlib, libpng, libjpeg, libwebp and libvpx licence and author notices | `upstream/` | The authors named below | Their own (below) |
| Our patches to libvpx and libwebp, which carry a few lines of their code as context | `Datatypes/patches/` | The WebM Project authors; Google Inc. and the libwebp contributors | BSD 3-clause, with the WebM patent grant |

## From our other repositories

- `Datatypes/include/devices/gfopenservice.h` is a copy of DalsinAI/gfopenservice's header.
- With `AC_HELPERS=1` the build reads AC090's native helpers (`common/amiga/ac_helpers*`, `ac_magic.h`) from DalsinAI/amicradle-guest at the commit in `AMIGACHROME_GUEST_PINNED_COMMIT`.

## Fetched at build time, not committed

Pinned by file and SHA-256 in `SOURCES`.

| Component | Version | Authors | Licence |
| --- | --- | --- | --- |
| zlib (Chromium's copy) | 1.3.1 | Jean-loup Gailly and Mark Adler; The Chromium Authors | zlib licence (`upstream/zlib/LICENSE`) |
| libpng | 1.6.58 | The PNG Reference Library Authors, Cosmin Truta, Glenn Randers-Pehrson, Andreas Dilger, Guy Eric Schalnat and others (`upstream/libpng/AUTHORS`) | PNG Reference Library License v2 (`upstream/libpng/LICENSE`) |
| libjpeg | 9f | Thomas G. Lane, Guido Vollbeding and the Independent JPEG Group | IJG licence (`upstream/libjpeg/README`) |
| libwebp | 1.6.0 | Google Inc. and the libwebp contributors (`upstream/libwebp/AUTHORS`) | BSD 3-clause, with the WebM patent grant (`upstream/libwebp/COPYING`, `PATENTS`) |
| libvpx | 1.17.0 | The WebM Project authors (`upstream/libvpx/AUTHORS`) | BSD 3-clause, with the WebM patent grant (`upstream/libvpx/LICENSE`, `PATENTS`) |

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
