# Uniteed

A small, lightweight map editor for **Teeworlds 0.6, 0.7, DDRaceNetwork and 0.8**, built
around **SDL3** and its 2D renderer, with text shaped by HarfBuzz and rasterized by
FreeType. It reads map files of datafile version 3, 4 and 5, and writes maps for each
of those ecosystems with the exact item versions their clients expect. Its own working
format is the `.unitwp` package, which keeps a map plus the editor state and,
optionally, the mapres files it needs.

This is the MVP: the format layer is complete and verified, the editor covers
viewing, tile painting and exporting. See [What is in the MVP](#what-is-in-the-mvp)
for the honest list of what is and is not there yet.

![the editor with a Teeworlds 0.8 map](docs/screenshots/editor-08.png)

A vanilla Teeworlds 0.6 map next to a DDRaceNetwork one - same editor, different
mapres directory, which is exactly the point of the per version assets:

![a vanilla 0.6 map](docs/screenshots/editor-06.png)
![a DDRaceNetwork 0.6 map](docs/screenshots/editor-ddnet-06.png)

## Version matrix

The datafile container version and the map *item* versions are independent, which is
the whole reason the editor has explicit targets:

| target | datafile | tilemap item | image | group | envelope points | notes |
|---|---|---|---|---|---|---|
| `06` Teeworlds 0.6 | 4 (zlib) | 3, 72 B | 1, 24 B | 3, 60 B | 24 B | what the 0.6.1/0.6.5 editor writes |
| `07` Teeworlds 0.7 | 4 (zlib) | 4, 72 B, run length tiles | 2, 28 B | 3, 60 B | 24 B or 88 B (bezier) | 0.7.6 and later |
| `ddnet` DDRaceNetwork | 4 (zlib) | 3, 92 B with the physics data indices | 1, 24 B | 3, 60 B | 24 B | info item is 24 B, UUID items are preserved |
| `08` Teeworlds 0.8 | **5 (zstd)** | 4, 72 B, run length tiles | 2, 28 B | 3, 60 B | 24 B or 88 B | the Archive fork |

Reading also accepts datafile version 3 (legacy, uncompressed data blocks). Teeworlds
0.6, 0.7, DDRaceNetwork and 0.8 map files all load.

## Building

Requirements: a C++23 compiler, CMake 3.16+, and the system libraries
`sdl3` (3.4 or later), `freetype2`, `harfbuzz`, `zlib`, `libzstd` and (optionally)
OpenSSL for the package UUIDs. Everything else is a git submodule.

There is no OpenGL code of our own and no vector graphics library: SDL3 owns the
window and the renderer, and the editor draws through a small painter in
`src/engine/painter.cpp` that emits triangles. The text pipeline is FreeType for the
glyph bitmaps and HarfBuzz for shaping, which is what makes the CJK fallback work:
HarfBuzz reports the glyphs the interface font is missing and those runs are reshaped
with the fallback face.

```sh
git clone --recursive <this repository>   # or: git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/uniteed                       # the editor window
./build/uniteed --help                # the command line
```

A headless build (no window, just the format tools) is available and needs neither
SDL3 nor a GPU:

```sh
cmake -S . -B build -G Ninja -DUNITEED_GUI=OFF
cmake --build build
./build/uniteed --selftest
```

### Fonts

The editor ships no fonts of its own; it asks the system for three faces — a regular
sans serif, its bold companion and one with CJK coverage — and each platform is
asked in the way that platform expects (`src/engine/font_find.cpp`):

| platform | where the faces come from |
|---|---|
| Windows | `%WINDIR%\Fonts` and `%LOCALAPPDATA%\Microsoft\Windows\Fonts`, plus the `Fonts` registry key, which is the only record of a font installed without copying a file into the directory. Segoe UI is preferred, then Tahoma/Verdana/Arial; the CJK face is Microsoft YaHei, then JhengHei, SimSun or DengXian. |
| macOS | `/System/Library/Fonts` and `/Library/Fonts` |
| Linux | `fc-match` when fontconfig is installed, then the usual distribution directories |

On Windows the font file is read into memory rather than opened by path: FreeType's
path based entry point takes a narrow string, which Windows reads in the active code
page, so a font under a non ASCII directory (`C:\Users\张伟\...\Fonts\...`) would not
open.

Any of the three can be overridden in `config.cfg` — `<data home>/uniteed/config.cfg`,
which is `%LOCALAPPDATA%\uniteed\config.cfg` on Windows and
`~/.local/share/uniteed/config.cfg` on Linux:

```
uniteed_font       C:\Users\me\Fonts\Inter-Regular.ttf
uniteed_font_bold  C:\Users\me\Fonts\Inter-Bold.ttf
uniteed_font_cjk   C:\Windows\Fonts\msyh.ttc
```

An override that does not exist is reported as a warning and the system lookup runs
instead, so a typo never leaves the editor without a font.

## Command line

```
uniteed [map]                       open the editor, optionally with a map
uniteed --gui [map]                 the same
uniteed --new out.map [--target id] [--size WxH]
uniteed --dump map                  print groups, layers, images, envelopes
uniteed --convert in out.map --target id
uniteed --pack in out.unitwp [--embed]
uniteed --unpack in.unitwp out.map --target id
uniteed --targets                    print the version matrix
uniteed --selftest [map ...]         round trip every bundled map through every target
uniteed --screenshot out.png [map]   render one frame offscreen and exit
```

`--target` takes `06`, `07`, `ddnet` or `08`. `--pack --embed` puts the mapres PNGs
the map references into the package so it can travel on its own.

`tools/verify_map.py` is an independent checker: it re-implements the container
layout in Python and validates a map from the outside (table consistency, every data
block, every tile layer expanding to exactly `w*h` tiles, quad and sound element
counts, envelope ranges). It shares no code with the editor, and it passes on the
bundled reference maps as well as on everything the editor writes.

`--selftest` is the regression test of the format layer: for every bundled map it
loads the map, exports it to all four targets, reloads the result, compares the
structure and the tiles, checks that a repeated save is stable, and does the same
through a `.unitwp` package round trip.

```
$ ./build/uniteed --selftest
...
35 checks, 0 failures

$ python3 tools/verify_map.py build/tmp/dm1_08.map data/maps/06/coverage.map
ok   build/tmp/dm1_08.map: datafile v5 35 items, 24 data blocks, 6 tile layers, 5 envelopes / 11 points
ok   data/maps/06/coverage.map: datafile v4 19 items, 17 data blocks, 8 tile layers, 0 envelopes / 0 points
```

## The editor

| key | action |
|---|---|
| left / right click | paint / pick the tile under the cursor |
| middle drag | pan |
| wheel | zoom |
| `W A S D`, arrows | pan |
| `F` | fit the map to the window |
| `G` | toggle the tile grid |
| `1 2 3` | paint, erase, pick tool |
| `4 5 6 7`, `[` `]` | brush size 1, 2, 4, 8 |
| `H J K` | hflip, vflip, rotate the brush |
| `Q E` | previous, next tile index |
| `P` | fill the whole layer with the brush |
| `Ctrl+Z`, `Ctrl+Y` | undo, redo (32 steps, per layer) |
| `Ctrl+N`, `Ctrl+O` | new map, open |
| `Ctrl+S` | save the `.unitwp` package |
| `Ctrl+E` | export, choose the target version |
| `Ctrl+L`, `Ctrl+G` | add a tile layer, add a group |
| `Delete`, `Shift+Delete` | delete the layer, delete the group |
| left click on a quad | select it, `Ctrl` adds to the selection |
| `C` | colour picker for the selected tile layer, or for the quads |
| `Space` | run / pause the envelope preview |
| `-`, `=` | envelope preview slower / faster |
| `Ctrl+Q` | quit |

Settings (window size, brush size, default target, last opened map) are kept in
`~/.local/share/uniteed/config.cfg`.

The palette on the right shows the 16x16 tile sheet of the selected layer's image,
so picking a tile is a click. The layer panel on the left lists the groups and
layers with visibility and lock toggles, and shows the colour of a tile layer (or of
the selected quads) as a swatch; clicking the swatch opens the picker. DDRace physics
layers are locked because their payload is not editable in this version.

### Colours and the envelope preview

A tile layer colour is a real RGBA multiplier over the tiles, applied the way the
client applies it: always, and combined with the layer's colour envelope. Quads are
drawn with all four of their corner colours (a smooth gradient over the quad) instead
of one flat colour, and a quad's position envelope moves and turns it.

Envelopes run on the editor's own clock, in the bar at the bottom of the layer panel:
play/pause, a speed from 0.25x to 8x, and a track you can scrub to sit at an exact
time. Scrubbing pauses, so the picture is reproducible; `Space` starts it again. The
evaluation is the reference one down to the details — the time wraps at the last
point, before the first point the last value is used, the curve of the point before
the time decides the interpolation (step, slow, fast, smooth, linear and bezier with
its tangents), and the values are 22.10 fixed point.

The picker edits the selected tile layer's colour, or — when a quad layer is selected
— the colour of the selected quads, or of all quads in the layer when none is
clicked. Everything is undoable (`Ctrl+Z`) and is written out by every export target.

## Assets: 0.6 and 0.7 are not interchangeable

Every one of the 28 mapres file names exists in both 0.6 and 0.7 with **different
pixels** (most 0.7 backdrops are twice the resolution), and DDRaceNetwork adds a third
set with 26 names of its own, so the images live in per version directories:

```
data/
  versions/06/mapres/      vanilla Teeworlds 0.6 (tag 0.6-endofline, 28 files)
  versions/07/mapres/      vanilla Teeworlds 0.7.6 and 0.8 (identical bytes, 32 files)
  versions/ddnet/mapres/   DDRaceNetwork, a third set: 54 files, the _0.7 twins included
  shared/editor/           the editor sprites (entities.png and friends)
  shared/mapres/           search path for images identical in every version (empty)
  maps/06, maps/07, maps/08  bundled sample maps of each format
```

The directories were filled once from the reference checkouts (`teeworlds` tags
`0.6-endofline` and `0.7.6`, and `ddnet`) and are committed, so a build needs no
checkout of them; [data/LICENSE.txt](data/LICENSE.txt) records which tag and which
directory each set came from, and [docs/FORMATS.md](docs/FORMATS.md) covers the
formats. The lookup order is the active version first, then the DDRace extras, then
shared, then the other vanilla version as a last resort, so a 0.6 map really gets the
0.6 tiles and a 0.7 map the 0.7 ones.

## The `.unitwp` package

A `.map` file can only hold the map. A package adds what an editor wants around it:

```
magic "UNITWP\0\1"   version   flags   then chunks: { id[4], size, data }
  META   key=value text: package id (a UUID), name, author, export target, timestamps
  MAP    the map as a datafile, using a lossless profile (DDRace tilemaps, every
         item preserved, envelope tangents kept)
  EDIT   the editor state (text, owned by the editor)
  RSRC   embedded resources, for example mapres/grass_main.png
  THUM   an optional PNG thumbnail
```

The map inside a package is normalised when it is loaded, so a package can be
exported to any target afterwards without going back to the original file.

## What is in the MVP

Working:

* datafile v3/v4/v5 reading, v4/v5 writing, zlib and zstd
* all four target profiles with the correct item versions, run length tiles, DDRace
  physics layers, the name packing with the +128 bias of the reference
* DDRace UUID items (automapper and friends) are preserved across a load and save,
  including the data blocks they point at
* embedded images (raw RGBA) and external mapres images, quads, envelopes,
  sounds, clipping groups
* the `.unitwp` package with embedded resources
* the editor: camera, grid, tile painting with brush size and flips, layer panel
  with visibility and locks, tile palette, undo/redo, open and export popups
* the colours: the tile layer colour as a real RGBA multiplier, all four quad corner
  colours as a gradient, a colour picker for both, and an envelope preview clock to
  watch colour and position envelopes (see
  [Colours and the envelope preview](#colours-and-the-envelope-preview))
* `--selftest`, the round trip regression test

Not in the MVP (called out so nothing is a surprise):

* editing DDRace physics payloads (teleporters, speedups, switches, tune) - the
  layers are shown, marked read only and preserved verbatim
* moving, creating or deleting quads, and editing envelope points: both load, are
  preserved, are exported correctly, and a quad's colour can be changed, but its
  points cannot be dragged yet
* DDRace envelope bezier, which lives in a separate UUID item: it is preserved
  verbatim, but editing envelope points would invalidate it
* an automapper, tile thumbnails in the palette beyond the sheet, copy/paste,
  flipping a whole layer, a package file browser
* parallax preview: layers are drawn with their group offset but without the
  camera dependent parallax of the client

## License

Uniteed is [MIT licensed](LICENSE), Copyright (c) 2026 Bamcane. That covers the code
and the documentation: `src/`, `tools/`, `docs/`, the build files and this README.

**The assets under `data/` are not covered by the MIT license.** They stay under the
licenses of the projects they come from, and everything under `data/` is released
under **CC-BY-SA 3.0**, exactly like Teeworlds its own `data` and `datasrc`
directories. There is no artwork by the Uniteed project in there — it is the Teeworlds
0.6, Teeworlds 0.7.6 and DDRaceNetwork mapres sets plus a few of their maps, collected
per version so the editor has the right pixels for every format. The authors, the
exact source tag and directory of each set, and the per map credits are listed in
[data/LICENSE.txt](data/LICENSE.txt). The screenshots earlier in this file are renders
of that art, so they are CC-BY-SA 3.0 as well.

Third party code keeps its own license, one directory per library: `external/libsugarx`
(MIT) and `external/stb` (`stb_image.h`, public domain / MIT, used only as the PNG
decoder). SDL3, FreeType, HarfBuzz, zlib, libzstd and OpenSSL are linked from the
system and not distributed here. The project layout, the `storage` module and the typed config
module follow [TWICE](https://github.com/TeeMidnight/TWICE) (MIT, Copyright (c)
2025-2026 TeeMidnight); they are written anew, but the design is its.

The map format constants and structures are derived from Teeworlds and DDRaceNetwork,
**both zlib licensed**: Teeworlds is Copyright (C) 2007-2025 Magnus Auvinen and
contributors (plus the Teeworlds Archive Project additions, 2025-2026), and the DDNet
checkout adds DDRace Copyright (C) 2010-2011 Shereef Marzouk and DDNet Copyright (C)
Dennis Felsing. The format handling is an independent reimplementation written against
their documented file formats and item layouts, so the maps this editor writes are
accepted by their clients.
