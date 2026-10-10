#!/usr/bin/env bash
# Syncs the built-in assets of the reference checkouts into a per-version layout.
#
#   0.6 and 0.7 share every mapres file name but not the pixels (0.7 redrew all
#   28 of them, mostly at double resolution), so the two sets have to live in
#   different directories. Shared/ holds the files that are identical for every
#   version (the 0.6 and 0.7 editor sprites for example).
#
# usage: tools/sync_assets.sh [teeworlds-checkout] [ddnet-checkout]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tw="${1:-$HOME/code/teeworlds}"
ddnet="${2:-$HOME/code/ddnet}"

data="$here/data"
echo "uniteed data: $data"
echo "teeworlds   : $tw"
echo "ddnet       : $ddnet"

# ---------------------------------------------------------------- 0.6 assets
# vanilla Teeworlds 0.6: tag 0.6-endofline, data/mapres (28 files)
rm -rf "$data/versions/06/mapres"
mkdir -p "$data/versions/06/mapres"
git -C "$tw" archive 0.6-endofline data/mapres | tar -x -C "$data/versions/06" --strip-components=1
echo "0.6 mapres: $(find "$data/versions/06/mapres" -type f | wc -l) files"

# ---------------------------------------------------------------- 0.7 assets
# vanilla Teeworlds 0.7.6: tag 0.7.6, datasrc/mapres (32 files). 0.8 ships the
# same bytes, so this directory is also the 0.8 set.
rm -rf "$data/versions/07/mapres"
mkdir -p "$data/versions/07/mapres"
git -C "$tw" archive 0.7.6 datasrc/mapres | tar -x -C "$data/versions/07" --strip-components=1
echo "0.7 mapres: $(find "$data/versions/07/mapres" -type f | wc -l) files"

# -------------------------------------------------------------- ddnet assets
# DDRaceNetwork 20.x keeps the 0.6 art plus its own tiles, and versions the 0.7
# overrides with the _0.7 suffix (grass_main_0.7.png and friends).
rm -rf "$data/versions/ddnet/mapres"
mkdir -p "$data/versions/ddnet/mapres"
cp "$ddnet"/data/mapres/*.png "$data/versions/ddnet/mapres/"
echo "ddnet mapres: $(find "$data/versions/ddnet/mapres" -type f | wc -l) files"

# ------------------------------------------------------------ shared assets
# editor sprites that are byte identical in 0.6 and 0.7 (entities.png, the
# background and the checker board)
mkdir -p "$data/shared/editor"
tmp="$(mktemp -d)"
git -C "$tw" archive 0.6-endofline data/editor | tar -x -C "$tmp"
git -C "$tw" archive 0.7.6 datasrc/editor | tar -x -C "$tmp/ed07" --one-top-level 2>/dev/null || true
for f in entities.png background.png checker.png; do
  if [ -f "$tmp/data/editor/$f" ]; then
    cp "$tmp/data/editor/$f" "$data/shared/editor/$f"
  fi
done
rm -rf "$tmp"
echo "shared editor: $(find "$data/shared/editor" -type f | wc -l) files"

# --------------------------------------------------------------- sample maps
# small built-in maps of each format to try the loader with
mkdir -p "$data/maps/06" "$data/maps/07" "$data/maps/08"
for m in coverage.map ctf1.map dm1.map dm2.map; do
  [ -f "$ddnet/data/maps/$m" ] && cp "$ddnet/data/maps/$m" "$data/maps/06/$m"
done
# 0.6 tag maps are byte identical to the ddnet ones for the shared names
for m in ctf1.map dm1.map; do
  [ -f "$tw/data/maps/$m" ] && cp "$tw/data/maps/$m" "$data/maps/08/$m"
done
# the 0.7 converted maps of ddnet are the only ready made 0.7 samples; keep the
# smallest one so the repository stays small
smallest="$(ls -S "$ddnet"/data/maps7/*.map 2>/dev/null | tail -1 || true)"
if [ -n "$smallest" ]; then
  cp "$smallest" "$data/maps/07/$(basename "$smallest")"
fi
echo "sample maps:"
find "$data/maps" -type f -printf '  %p (%s bytes)\n'
