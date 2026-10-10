# Uniteed

A small, lightweight map editor for **Teeworlds 0.6, 0.7, DDRaceNetwork and 0.8**, built
around GLFW + OpenGL + [nanovg](https://github.com/memononen/nanovg). It reads map
files of datafile version 3, 4 and 5, and writes maps for each of those ecosystems
with the exact item versions their clients expect. Its own working format is the
`.unitwp` package, which keeps a map plus the editor state and, optionally, the
mapres files it needs.

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
`glfw3`, OpenGL, `zlib`, `libzstd` and (optionally) OpenSSL for the package UUIDs.
Everything else is a git submodule.

```sh
git clone --recursive <this repository>   # or: git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/uniteed                       # the editor window
./build/uniteed --help                # the command line
```

A headless build (no window, just the format tools) is available and needs neither
GLFW nor a GPU:

```sh
cmake -S . -B build -G Ninja -DUNITEED_GUI=OFF
cmake --build build
./build/uniteed --selftest
```

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

`--selftest` is the regression test of the format layer: for every bundled map it
loads the map, exports it to all four targets, reloads the result, compares the
structure and the tiles, checks that a repeated save is stable, and does the same
through a `.unitwp` package round trip.

```
$ ./build/uniteed --selftest
...
35 checks, 0 failures
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
| `Ctrl+Q` | quit |

Settings (window size, brush size, default target, last opened map) are kept in
`~/.local/share/uniteed/config.cfg`.

The palette on the right shows the 16x16 tile sheet of the selected layer's image,
so picking a tile is a click. The layer panel on the left lists the groups and
layers with visibility and lock toggles; DDRace physics layers are locked because
their payload is not editable in this version.

## Assets: 0.6 and 0.7 are not interchangeable

Every one of the 28 mapres file names exists in both 0.6 and 0.7 with **different
pixels** (most 0.7 backdrops are twice the resolution), so the images live in
per version directories:

```
data/
  versions/06/mapres/      vanilla Teeworlds 0.6 (tag 0.6-endofline, 28 files)
  versions/07/mapres/      vanilla Teeworlds 0.7.6 and 0.8 (identical bytes, 32 files)
  versions/ddnet/mapres/   DDRaceNetwork, including the _0.7 twins (54 files)
  shared/mapres/           assets that are identical in every version
  shared/editor/           the editor sprites (entities.png and friends)
  maps/06, maps/07, maps/08  bundled sample maps of each format
```

`tools/sync_assets.sh` fills these directories from the reference checkouts
(`teeworlds` tags and `ddnet`), see [docs/FORMATS.md](docs/FORMATS.md) for the exact
sources. The lookup order is the active version first, then the DDRace extras, then
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
* `--selftest`, the round trip regression test

Not in the MVP (called out so nothing is a surprise):

* editing DDRace physics payloads (teleporters, speedups, switches, tune) - the
  layers are shown, marked read only and preserved verbatim
* creating or editing envelopes and quads in the UI (they load, are preserved and
  are exported correctly)
* DDRace envelope bezier, which lives in a separate UUID item: it is preserved
  verbatim, but editing envelope points would invalidate it
* an automapper, tile thumbnails in the palette beyond the sheet, copy/paste,
  flipping a whole layer, a package file browser
* parallax preview: layers are drawn with their group offset but without the
  camera dependent parallax of the client

## License

The map format constants and structures are derived from Teeworlds (zlib licensed,
Magnus Auvinen and contributors) and DDRaceNetwork (GPLv3+). nanovg is MIT, libsugarx
is MIT with the UnLock extension, glad is generated from the Khronos registry.
