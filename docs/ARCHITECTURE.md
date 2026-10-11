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
                 json.h/cpp    the JSON value, parser and serializer
                 uuid.h/cpp    the shared identity generator for projects and packages
  config/        config.h/cpp, config_keys.h  typed settings in a text file
  engine/        graphics.h/cpp  painter.h/cpp  text.h/cpp  input.h/cpp
                 keycodes.h    the key symbols, so the model needs no SDL
                 chrome.h/cpp  the shape of a window that draws its own decorations
                 ui.h/cpp  ui_theme.h/cpp  file_dialog.h/cpp
                 svg.h/cpp     nanosvg behind one interface: parse, rasterize
                 vector_textures.h/cpp  the SVG textures that follow the display
                 editor_app.h/cpp  main.cpp
  game/
    assets.h/cpp           version aware resource lookup, PNG decode, tile flag analysis
    image_codec.cpp        stb_image with internal linkage
    project.h/cpp          the .unitwp project: map, editor state, rules, metadata
    map/
      mapitems.h           every item layout of 0.6/0.7/DDRaceNetwork/0.8, with static asserts
      datafile.h/cpp       CDataFileReader (v3/v4/v5) and CDataFileWriter (v4/v5)
      map.h/cpp            CMap: the editable model, the target profiles and the extension bits
      map_io.cpp           loading and saving, all version rules in one place
      envelope.h/cpp       the envelope evaluator, shared with the preview
      unitwp.h/cpp         the .unitwp package container
    rules/
      rules.h              the autorules model and the engine's contract
      rules_json.cpp       rules as JSON, and the DDRace .rules text reader
      rules_apply.cpp      the automapper algorithm
    editor/
      editor.h/cpp         state, input, actions, undo, the project and rule operations
      editor_project.cpp   projects, rules, physics layers, envelope operations
      editor_assets.cpp    the image and sound model, no renderer needed
      editor_sound.cpp     sound layers, sound sources and the preview
      editor_window.cpp    the menu bar and the toolbar, and the menus (GUI build only)
      editor_chrome.cpp    the tool box and the floating panels (GUI build only)
      editor_icons.cpp     the tool strip's vector pictograms (GUI build only)
      editor_assets_draw.cpp  the asset browser (GUI build only)
      editor_ui.h          the drawing helpers the panels share
      editor_draw.cpp      the whole interface and the map drawing
      editor_envelope.cpp  the envelope curve workspace
      editor_textures.cpp  the texture cache, window build only
      editor_entities.cpp  the entity and physics sprite sheets
      editor_files.cpp     the platform file dialogs
      admin.h/cpp          the JSON administrative interface and its session
      admin_ops.h/cpp      the method table: one entry per operation
      agent.h/cpp          the one shot subcommands and the resident --agent loop
      cli.h/cpp            the headless command line and the selftest
external/
  libsugarx/    git submodule, header only helpers (byte spans, endian, uuid, lazy table)
  stb/          git submodule, stb_image.h is the PNG decoder
  nanosvg/      git submodule, header only SVG parser and rasterizer for the icons
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

`base/i18n` carries the interface in another language. The English source text *is* the
key: a string is written where it is drawn and wrapped in `Tr()`, and a catalog under
`<data>/lang` (a JSON document of source to translation) answers for the ones it has.
Translation is deliberately **not** done inside `ui::Text()`: that function also draws the
names of the user's layers, groups and files, and a map with a layer called "Tiles" has to
keep that name. A catalog that is missing a string, and a language that is not installed,
both leave the interface readable rather than half translated. Tables of strings that live
at namespace scope cannot call `Tr()` while they are built - the catalogs are read after
that - so they stay English and are wrapped where they are drawn, and a table that a tool
has to see is marked `i18n:table` for `tools/lang.py`.

`base::Format` builds the strings the interface and the log show, and it is
`std::format`: "{}" fields, with every call checked against its arguments **when it is
compiled**. It used to be a `vsnprintf` wrapper, which is one mistyped `%d` away from a
wrong line and cannot be checked at all - and a translated format string, which is a
string no compiler ever sees, is exactly that hazard. `base::FormatRuntime` is the same
builder for a format string that only exists while the program runs; it validates the
fields as it formats and leaves the format string on screen instead of throwing in the
middle of a frame. Both are covered by `--selftest`.

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
with the fallback face. The faces come from `engine::fonts` (`font_find.h/cpp`), the
only platform specific part of the text pipeline: Windows reads the font directories
and the `Fonts` registry key, macOS the system font directories, Linux `fc-match` with
a path fallback list. Each face can be overridden from `config.cfg`.

`CInput` collects the SDL events; `engine::ui` is the tiny widget set (rects, text,
buttons, checkboxes, sliders, and the tooltip the chrome labels itself with) that draws
through the painter. It has no icons and no fixed idea of what a picture is.

Every panel labels its own controls: `CEditor::Hint(rect, title, detail)` puts the
detail in the status bar and opens the tooltip under the widget, and `Render()` draws
the tooltip *after* every panel, so a hint can never be covered by the thing it
describes. The tool strip is the one place that asks for the tip beside the button
instead of below it. This is where the interface explains itself rather than relying on
a manual: the letters on the keys, the meaning of a dock tab, what a checkbox in the
layer tree locks, and which entity a cell of the game layer's sheet is.

* `svg.cpp` is the only place that knows about nanosvg. It owns the parsing and the
  rasterizing, and it does not touch SDL, so the *headless* build compiles it too and
  `--selftest` rasterizes the bundled icons to prove the parser works on a machine
  with no window. Two details of the library are worth knowing: its XML parser
  rewrites the string it is handed, so a document keeps a mutable copy of its source,
  and the rasterizer writes unpremultiplied alpha, which is what the renderer wants.
* `vector_textures.cpp` is what makes an icon follow the display. It keeps the
  document, the key and the pixel size each texture was last rasterized at; a request
  for a size that does not match re-rasterizes and stores the result through
  `CGraphics::ReplaceTextureRGBA`, which keeps the *handle* so the panel holding it
  does not have to care. The scale is the display's density times `uniteed_ui_scale`.
* A texture handle is only valid until something throws the texture away, and one thing
  does: opening another map calls `CEditor::ResetTextures()`, which drops every texture
  the graphics cache holds. Anything that *remembers a handle* therefore has to let go
  of it there - `CVectorTextures::Forget()` and the two sprite sheet slots
  (`m_EntityTexture`, `m_PhysicsTexture`) do exactly that, and the next frame reloads or
  re-rasterizes what it needs. A cache that looks its texture up by *key* (the mapres
  sheets, the asset thumbnails) finds nothing and loads again, so it never had the
  problem. The bug this documents is real and was shipped: the tool strip and the entity
  sheet went blank on the second map of a session. `--screenshot ... --reopen other`
  is the offscreen test for it.
* The painter tints an image fill (`CPaint::m_Tint`), which is why the icon files are
  white: one texture per size serves the normal, the hover and the disabled state, and
  the theme stays the only thing that decides colour.
* The window is drawn in *logical* coordinates (`SDL_SetRenderLogicalPresentation`),
  so on a display with a higher density the layout keeps the size it was written for
  and the vector textures are what gets denser. Without that, a 2x display would draw
  the interface into a quarter of the window.

**The window draws its own title bar.** `uniteed_client_decorations` asks for a window
without the system decorations, and the editor then draws the row a title bar would have
been: the menu bar of `editor_window.cpp`, with `Files` and `Settings` at the left, the
title in the middle and the three window buttons at the right. The row cannot move the
window by itself - that is the window manager's job - so the drawing publishes the shape
of the row and the engine hands it to the platform:

* `engine/chrome.h` is the whole policy, and it has no SDL in it: a header height, the
  width of the grabbable frame, the rectangles that must stay clickable, and
  `HitTestChrome()`, which says whether a point of the window is the bar (`drag`), one of
  the eight edges (`resize_*`) or the application's (`normal`). A control wins over the
  frame, because the close button lives in the top right corner and would otherwise never
  be clickable. Because the policy is a plain function of a plain value, `--selftest`
  checks every region of it with no window at all.
* `CGraphics::SetClientChrome()` stores it and `HitTestCallback` translates it into SDL's
  own answers, which is what makes the bar draggable on X11, on Windows and on Wayland
  (SDL's Wayland backend runs the hit test on pointer motion and on the press it uses to
  start an `xdg_toplevel` move). A platform that refuses either the borderless window or
  the hit test gets its decorations back instead: a window nobody can move is worse than
  a title bar the user asked to be rid of.
* The two rows are laid out from `MenuBarHeight` and `ToolbarHeight`, and `ChromeHeight`
  is what the canvas, the tool box and the dockers start below. Only the menu bar drags
  the window, so the toolbar under it keeps every pixel for its buttons.
* **The remembered geometry is decoration independent.** What the settings store is the
  size of everything below the title bar plus whether the window was maximized. With a
  drawn bar the menu bar lives inside the window where the system bar used to be outside
  it, so `RunEditor()` adds `game::MenuBarHeight` to the stored height when it creates
  the window and takes it off again when it saves - otherwise switching the drawn bar on
  would quietly cost the canvas 28 pixels, and switching it off would grow the window.
  The size that is saved is `CGraphics::RestingSize()`, the last size the window had
  while it was neither maximized nor minimized: a maximized window reports the size the
  window manager gave it, and storing that would open the next session with a huge
  *windowed* window. `SDL_WINDOW_MAXIMIZED` is read back with `SetMaximized()` on the way
  in, so a session that ended maximized opens that way. `--quit-after N` is the test
  hook: it drives a windowed session to its end, which is where the settings are written.

An open menu is an overlay, and an immediate mode interface has no z order: the widget
behind a menu would answer the same click. `CInput::SetMouseBlocked()` is therefore set
for the frame while a menu is open (from `CEditor::Render()`, before anything else is
drawn), `ui::Hovered()` answers false everywhere while it is set, and the two things that
must still react - the menu bar and the menu itself - test the cursor position directly.
`HandleDockInput()` checks the flag as well, because it reads the mouse without going
through `ui::Hovered`. The offscreen test for it is a click on the `New map` row, which
lies over the `06` target button of the toolbar: the target has to read `08` afterwards,
and `--menu files` is how a picture of an open menu is taken.

The name field of the layer tree (`CEditor::BeginRename`) is the same idea for the
keyboard rather than the mouse: `Update()` skips the canvas and every shortcut while one
is open, so a layer called "grid" is not also a grid toggle. The rename is editor state
rather than drawing state, which is what lets `--selftest` drive it a frame at a time
with `Input().OnText()` and the keys that end it.

`engine::svg` and the editor's `game/editor/editor_icons.cpp` are the two halves of it:
the first turns documents into pixels, the second knows which document belongs to which
tool and registers them from `data/icons` when the editor starts.

**A game layer is a picture made of pictures.** Its cells are indices into the *entity
sheet*, not tile indices: cell 1 is a tile controller, cell 192 the spawn, and the client
draws both by copying the cell's column and row out of the same 16 by 16 image. The
canvas therefore uses the stored value *as* the cell (`DrawSprite`, never
`index - ENTITY_OFFSET`) and draws every non-empty cell from the sheet, entity or
controller; the entity offset only decides which of them have a name to show in a
tooltip. Printing the low cells as their index was the earlier behaviour, and it made a
layer that is mostly controllers (dm1 has 32 entities and 1535 controller cells) look
like a spreadsheet. The index still appears as a number in two places, both deliberate:
in the *fallback* for a build with no sheet installed, so a cell is never invisible, and
as the brush readout under the cursor (`5 hflip`), which is about the value being painted
rather than about how the cell looks.

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

**game/rules** is the rule engine and has no dependency on the window either. It
is the DDRaceNetwork automapper algorithm with the layer addressing of `map.h`
instead of the two fixed layers, and it is deliberately ignorant of `CTile`: it
is handed a plane of indices and a plane of flags per layer, which is what makes
it testable on its own and what keeps a rule run from reaching into the model.

**game/editor** is the application: camera, brush, selection, undo, the file
operations and, in `editor_draw.cpp`, the immediate mode interface and the map
renderer. It is split so that the **model** — `editor.cpp`, `editor_project.cpp`
— links into the headless build with `m_pGraphics` left null, while everything
that needs a renderer (`editor_draw.cpp`, `editor_envelope.cpp`,
`editor_textures.cpp`) is window only. `editor_entities.cpp` and
`editor_files.cpp` are compiled either way and stub themselves out without a
window, which is what lets the command line and the agent interface drive the
same `CEditor` the window does.

`admin_ops.cpp` holds one table: a row per administrative method with its name, a
one line parameter list, a description and the function that runs it. The
dispatcher looks a method up in that table, `agent.describe` prints it, and the
command line builds its subcommands from it, so a new operation is added in one
place and cannot go stale. `agent.cpp` is the thin layer over it: a one shot
subcommand parses `--key value` into a parameter object, and `--agent` reads one
JSON request per line from stdin and writes one JSON response per line to
stdout. Every log line goes to stderr for that reason — stdout is a document.

`cli.cpp` is the rest of the command line: the format operations and the
selftest, which is also the test harness.

**base/json** and **base/uuid** have no dependency on the rest. The JSON value
is a variant of the six types with an insertion ordered object, so a project file
dumps deterministically; parsing reports the byte offset and a nesting limit
rather than recursing without bound.

### What the canvas edits

There is no explicit mode flag. What the canvas does follows from the selected
layer, through `CEditor::Mode()`: a tile layer gives `edit_mode::tiles` and the tile
brush, a quad layer gives `edit_mode::quads` and the point handles, a locked or
unknown layer gives `edit_mode::none`. The toolbar, the layer panel and the right
panel all branch on that one function, which is why only the controls that apply to
the selection are ever on screen. What follows from that is a short list of rules, and
every one of them has bitten this code already:

* **hit tests use the same points that are drawn.** `QuadPoints()` applies the
  position envelope, and both `DrawQuadLayer()` and `UpdateQuadHover()` go through it,
  so a quad that an envelope has moved is picked where it appears. `PointInQuad()`
  walks an outline, so the corners are handed to it in `CQuad::s_aPerimeter` order -
  the stored order traces a bowtie and would test the wrong half of the shape.
* **a carve-out is applied back through the same transform it was drawn with.** The
  corner handles show the envelope's effect, so dragging one subtracts the envelope's
  current offset and rotation before writing the stored point, or the handle would
  drift away from the cursor.
* **a press selects, a drag edits.** On a quad layer a plain drag moves the whole
  selection and only `Shift` bends a single point, so a click that was meant to select
  cannot deform the shape; on a tile layer the pick tool drags a marquee and the brush
  is replaced on release, not while the region is still being chosen.
* **the button follows the layer.** The right button takes the brush off a tile layer,
  and on a quad layer it colours instead: `OpenQuadColorPickerAt()` resolves the point
  under the cursor to the corner it names, so the picker is aimed by the click rather
  than by a selection in the panel. `QuadHandleAt()` is the one place that decides
  which point was hit, and `QuadPointScreen()` is its inverse, which is what lets a
  scripted run or `--selftest` put the cursor on a corner without knowing the layout.
* **a popup is one rectangle and one owner.** `ColorPopupRect()` is where the picker is
  drawn and it is also what the dismissal is tested against: it is placed beside the
  point the picker was opened from - flipped to the other side of it near an edge of the
  canvas, so the corner being coloured is never covered - and `HandleColorPopupInput()`
  closes the popup on a press outside it. That function is deliberately free of the
  painter: `Update()` and `UpdateHeadless()` run it in the same place, so a windowless
  frame exercises the same dismissal the window does, and Escape is only the other way
  out rather than the only one.

The tile brush is a **stamp**, `m_vBrush` with its own size and a tile index and flags
per cell, not a single index. A 1x1 stamp is the plain tile brush, the palette and the
map pick write larger ones, `Space` and the right button take one the size of the
current brush, and `P` tiles the stamp over the layer. Painting and the cursor preview
share `BrushOrigin()`, so the ghost under the cursor is exactly the block that a click
writes. The canvas only outlines quads of the layer being edited: a selection is a list
of indices into that layer, so applying it to the others would light up unrelated quads.

### The workspace

The interface is a dock layout rather than three fixed columns. Each side is a
`dock_panel` with its own width, a collapsed flag and which tab it shows, and
`CanvasRect()` is derived from the two widths, so the canvas, the hit testing and
the splitters cannot disagree about where a panel is. Two things follow from that:

* **the layout is state, so it belongs to the project.** `SaveProject()` writes the
  dock tabs, the widths and the camera into the project's `editor` member, and
  `LoadProject()` restores them, which is why a project reopens as it was left.
* **the workspaces are drawings, not modes.** `DrawEnvelopeWorkspace()` and
  `DrawRulesWorkspace()` are called from whichever dock shows them, in the same
  immediate mode pass as everything else, so a workspace can be moved to the other
  side without any state following it.

A workspace changes the model through the same operations the administrative
interface calls (`ApplyRules`, `SetEnvelopePointTime`, ...), which is what keeps a
click and a JSON method from meaning different things.

### The envelope workspace

`editor_envelope.cpp` is a curve editor: the x axis is time, the y axis the fixed
point value of a channel pair, and every keyframe is a handle on the trace. The
trace is sampled through `EvaluateEnvelope`, so what is drawn is what the client
plays, and the y range is fitted to the envelope and snapped to a power of two
because a colour envelope lives in 0..1024 while a position envelope reaches tens
of thousands — one fixed range cannot show both.

Dragging a handle writes `SetEnvelopePointTime` and `SetEnvelopePointValue`, which
keep the points in time order; the point list under the curve is where the curve
type is set, including bezier.

### The rules workspace

`editor_draw.cpp`'s `DrawRulesWorkspace` is a browser on the left and an inspector
on the right, because a rule set is a three level tree (configurations, runs, index
rules) and the shape of the whole document has to stay visible while one rule is
edited. Every field writes straight into the rule set, so `Apply` can never run
something the user cannot see. The engine it drives is `game/rules`, and the layer
it runs against is addressed by the engine slot of `rules.h`, which
`CEditor::SlotForLayer` computes from the group and layer indices.

### The tile and physics layers

A DDRace physics layer is drawn with its own sprite sheet, chosen by the layer's
flag, and the tile it shows is *derived* from the payload rather than stored: the
loader mirrors the type byte out of the payload, and `physics.set` mirrors it back,
so the picture and the data cannot drift apart. Whether the DDRace game entities
are offered at all is `CEditor::GameEntitiesAllowed()`, which asks the target's
`ext_game_entities` bit and the editor's `allow DDRace extensions` switch.

The screenshot mode (`--screenshot` with `--layer`, `--quad`, `--dock`,
`--envelope`, `--move`, `--click`, `--key` and friends) drives the same input path
the window does, one event per frame, which is how the interface is verified
without a display. `--dock` and `--envelope` exist because the two new workspaces
are otherwise only reachable with a mouse. `--click-point Q:P` is the same click
aimed at a point of a quad: the corner of a map quad is a world position, so the
pixel it lands on is asked for while the script runs instead of being written down
where a layout change would invalidate it.

## Data flow

```
.map (v3/v4/v5)  --CDataFileReader-->  CMap (normalised)  --CDataFileWriter-->  .map (v4/v5)
                                            ^   |                     ^
                       .unitwp  <-----------+   +-------->  .unitwp   |
                       (MAP chunk, package profile)                   |
                                            |                          |
                      .unitwp project <----+  (MAP + EDIT + RULE chunks)  rules apply
```

A project *is* a `.unitwp` package: the map goes in through the *same* writer the
export uses, with the package profile, so a load and a save cannot disagree about the
format, and the same file can be handed to `--unpack`. The `EDIT` chunk holds JSON
that is opaque to the format layer — it is the editor's own state — and the `RULE`
chunk is a document that only `game/rules` reads.

Loading normalises: tiles are always a plain `w*h` array without run lengths, images
always carry their raw RGBA, envelope points always carry their tangents, and DDRace
physics payloads are kept as raw bytes next to the mirrored tile indices. Saving
re-applies the conventions of the target. That direction is one way on purpose: the
model is the union, the profiles are the projections.

The editor never talks to `CDataFileReader`/`Writer` directly; it goes through
`CMap::LoadFromFile` / `SaveToFile`. The package adds the editor state, the metadata
and the optional resources around the same model.

## Adding a new target version

1. add an entry to `g_aProfiles` in `map.cpp` with the item versions and the
   extension bits (`ext_physics`, `ext_game_entities`, `ext_sounds`,
   `ext_bezier_envelopes`, `ext_extras`) and `m_RunLengthTiles`,
2. add it to `AllTargets()` so the toolbar, the export popup, `--target` and the
   agent interface see it,
3. if it needs a new item layout, add the struct to `mapitems.h` with a
   `static_assert` on its size and read/write it by offset in `map_io.cpp`.

An **extension bit** is how a feature is gated: the writer asks
`Profile.HasExtension(ext_physics)` rather than comparing targets, the editor
asks the same question when it decides whether to offer a control, and
`CompatibilityWarnings()` asks it to say what an export will cost. A new feature
therefore adds one bit and a handful of queries, not a matrix of target checks.

`--selftest` then covers it automatically: it exports every bundled map to every
target, reloads and compares, and it carries its own checks for the rule engine
and for the Teeworlds style bezier envelope export.
