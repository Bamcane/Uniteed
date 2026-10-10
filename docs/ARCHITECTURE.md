# Architecture

Uniteed follows the shape of the two reference projects the request named: the module
split and the data-file core come from **TWICE** (`src/base`, `src/game/map`, a
`CDataFile` reader plus an editable map model), and the application shell comes from
**SandboxCmd** (`src/engine` with a small immediate mode UI and a `src/base/storage`
module in the `storage` namespace). The window and the renderer are SDL3's, the text
is FreeType plus HarfBuzz and the drawing goes through a painter of our own, but the
module split and the immediate mode style are still SandboxCmd's.

```
src/
  base/          define.h logger.h math.h endian.h string.h time.h storage.h/cpp png.h/cpp
  config/        config.h/cpp, config_keys.h  typed settings in a text file
  engine/        graphics.h/cpp  painter.h/cpp  text.h/cpp  input.h/cpp
                 ui.h/cpp  editor_app.h/cpp  main.cpp
  game/
    assets.h/cpp           version aware mapres lookup, PNG decode, tile flag analysis
    image_codec.cpp        stb_image with internal linkage
    map/
      mapitems.h           every item layout of 0.6/0.7/DDRaceNetwork/0.8, with static asserts
      datafile.h/cpp       CDataFileReader (v3/v4/v5) and CDataFileWriter (v4/v5)
      map.h/cpp            CMap: the editable model and the target profiles
      map_io.cpp           loading and saving, all version rules in one place
      unitwp.h/cpp         the .unitwp package container
    editor/
      editor.h/cpp         state, input, actions, undo
      editor_draw.cpp      the whole interface and the map drawing
      cli.h/cpp            the headless command line and the selftest
external/
  libsugarx/    git submodule, header only helpers (byte spans, endian, uuid, lazy table)
  stb/          git submodule, stb_image.h is the PNG decoder
```

## Layers

**config** is TWICE's settings module in miniature: a typed `config::key<T>` with a
default, an unordered map of strings behind it and a `key value` text file in the
application data directory. The editor stores the window size, the brush, the default
target and the last opened map there.

**base** has no dependency on the rest. `storage` is a `std::filesystem` module in the
`storage` namespace, `read()` returns `std::optional<std::vector<std::byte>>`, the
`filewriter` class creates its parent directories and throws on failure - the TWICE
layout. On top of that it carries the ordered resource search path that Teeworlds
expresses with `IStorage`: `SetResourcePaths` plus `FindResource`, used to look up
mapres files with the active version first.

`base/endian.h` and `base/string.h` delegate to libsugarx (`sugar_endian.h`,
`sugar_types.h`) and add only the unaligned peeks and the `StrToInts`/`IntsToStr`
pair with the +128 bias the map format uses.

**engine** owns the window. `CGraphics` wraps the SDL3 window, the SDL3 renderer and
the texture cache (a `libsugarx::lazy_flat_table` keyed by texture name, holding an
index into a table of `SDL_Texture`). It refers to a texture by a small positive int,
because a pointer cannot be carried through an int on a 64 bit machine.

Drawing goes through `painter::CPainter` (`painter.h/cpp`), a small immediate mode
vector painter on top of `SDL_RenderGeometry`. It keeps the vocabulary the interface
was written against - build a path, fill or stroke it with a colour or a paint, push
and pop the transform and the clip - so the editor says what it wants rather than
which triangles to emit. Two details matter:

* SDL3's renderer clips only its own rect calls, never geometry, so the painter keeps
  its own scissor stack and clamps every primitive it emits.
* A path point carries both its screen position and its position in the space the
  path was built in. The triangulator needs the first; an image fill needs the second,
  because a tile is drawn translated and its texel window would otherwise be measured
  from the wrong origin.

Text is `CTextEngine` (`text.h/cpp`): HarfBuzz shapes a UTF-8 string into glyphs and
FreeType rasterizes them into one shared 8 bit coverage atlas, which the painter
uploads as white with the coverage in the alpha channel. Shaping is done in runs of
codepoints that resolve to the same face, which is what makes the CJK fallback work -
HarfBuzz reports the glyphs the interface font is missing and those runs are reshaped
with the fallback face. The face is picked from the file of the system default sans
serif found through `fc-match`, with a path fallback list.

`CInput` collects the SDL events; `engine::ui` is the tiny widget set (rects, text,
buttons, checkboxes, sliders) that draws through the painter.

**game/map** is the format layer and has no dependency on the window. It is the part
that is tested by `--selftest` and it builds in the headless configuration.

* `datafile.cpp` reads versions 3, 4 and 5 and writes 4 and 5. It is a faithful port
  of the reference reader/writer, including the table layout, the case where a v5
  item section is stored as one zstd frame, and the grouping of items by type.
* `map_io.cpp` holds every version rule: reading item fields by offset with bounds
  checks (an old map stores a shorter body than the current struct), expanding run
  length tiles, mirroring DDRace physics tile indices, and writing the item versions
  of the chosen target.
* `map.cpp` defines the target profiles and the model helpers, `unitwp.cpp` the
  package container.

**game/editor** is the application: camera, brush, selection, undo, the file
operations and, in `editor_draw.cpp`, the immediate mode interface and the map
renderer. `cli.cpp` is the command line, which is also the test harness.

## Data flow

```
.map (v3/v4/v5)  --CDataFileReader-->  CMap (normalised)  --CDataFileWriter-->  .map (v4/v5)
                                            ^   |
                       .unitwp  <-----------+   +-------->  .unitwp
                       (MAP chunk, package profile)
```

Loading normalises: tiles are always a plain `w*h` array without run lengths, images
always carry their raw RGBA, envelope points always carry their tangents, and DDRace
physics payloads are kept as raw bytes next to the mirrored tile indices. Saving
re-applies the conventions of the target. That direction is one way on purpose: the
model is the union, the profiles are the projections.

The editor never talks to `CDataFileReader`/`Writer` directly; it goes through
`CMap::LoadFromFile` / `SaveToFile`. The package adds the editor state, the metadata
and the optional resources around the same model.

## Adding a new target version

1. add an entry to `g_aProfiles` in `map.cpp` with the item versions and the flags
   (`m_DdnetTilemap`, `m_RunLengthTiles`, `m_BezierEnvelopes`, ...),
2. add it to `AllTargets()` so the toolbar and `--target` see it,
3. if it needs a new item layout, add the struct to `mapitems.h` with a
   `static_assert` on its size and read/write it by offset in `map_io.cpp`.

`--selftest` then covers it automatically: it exports every bundled map to every
target, reloads and compares.
