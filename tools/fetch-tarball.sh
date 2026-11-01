#!/bin/sh
# fetch-tarball.sh FILE DIR: an upstream tarball that SOURCES lists, put into DIR.
# It downloads from the "from:" URL that SOURCES gives for FILE, keeps it only if its
# SHA-256 is the one SOURCES gives, and leaves a file already in DIR alone. A FILE that
# SOURCES gives no URL for (zlib, from AROS's ports cache) can't be fetched: put it
# in DIR yourself. Used by build.sh and Datatypes/build.sh when a tarball is missing,
# so a fresh clone builds with no files from anywhere else.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
file=$1 dir=$2
entry=$(awk -v f="$file" '
    NF == 0 { cur = "" }
    $1 == "file:" { cur = $2 }
    $1 == "from:" && cur == f { url = $2 }
    $1 == "sha256:" && cur == f { sha = $2 }
    END { print url " " sha }' "$HERE/SOURCES")
url=${entry% *} sha=${entry#* }
if [ -z "$sha" ] || [ "$sha" = "$entry" ]; then echo "fetch-tarball: SOURCES doesn't list $file" >&2; exit 2; fi
mkdir -p "$dir"
[ -f "$dir/$file" ] && exit 0
case "$url" in
    https://*) ;;
    *) echo "fetch-tarball: SOURCES gives no URL for $file; put it in $dir (see SOURCES)" >&2; exit 2 ;;
esac
echo "fetching $file from $url"
tmp="$dir/.$file.part"
curl -fsSL --retry 3 -o "$tmp" "$url" || { rm -f "$tmp"; echo "fetch-tarball: download of $url failed" >&2; exit 2; }
if ! echo "$sha  $tmp" | sha256sum -c - >/dev/null 2>&1; then
    rm -f "$tmp"; echo "fetch-tarball: $url isn't the file SOURCES names (SHA-256 mismatch)" >&2; exit 2
fi
mv "$tmp" "$dir/$file"
