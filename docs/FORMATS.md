# Map and package formats

Everything in this document was read out of the reference implementations and then
verified against real map files:

* `teeworlds` checkout: the "Teeworlds Archive Project" (0.8.0), which is the only
  implementation with datafile v5. Tags `0.6-endofline` and `0.7.6` are used for the
  older item versions and for the assets.
* `ddnet` checkout: DDRaceNetwork 20.2, 0.6 network protocol, which also reads 0.7
  maps.

## 1. The datafile container

All integers are little endian 32 bit.

```
offset 0    char  magic[4]        "DATA" (readers also accept "ATAD", big endian)
offset 4    int   version         3, 4 or 5
offset 8    int   size            filesize - 16
offset 12   int   swaplen         (filesize - data size) - 16
offset 16   int   num item types
offset 20   int   num items
offset 24   int   num raw data
offset 28   int   item size       v3/v4: bytes on disk, v5: uncompressed bytes
offset 32   int   data size       total bytes of the compressed data area
```

Then, in this order:

| section | v3 | v4 | v5 |
|---|---|---|---|
| item types, `{int type; int start; int num;}` | yes | yes | yes |
| item offsets, `int[num items]` | yes | yes | yes |
| data offsets, `int[num raw data]` | yes | yes | yes |
| data uncompressed sizes, `int[num raw data]` | **no** | yes | yes |
| compressed item section size, `int` | no | no | **yes** |
| item section | raw | raw | **one zstd frame** |
| data blocks | **raw** | **zlib** each | **zstd** each |

Item records are `{int typeAndID; int size; payload}`, `type = typeAndID >> 16`,
`id = typeAndID & 0xffff`. Items are grouped by ascending type, then in insertion
order; the offset table, the item section and the type table all share that order.
Item offsets are relative to the item section, data offsets to the data area. Item
payload sizes are always a multiple of 4.

Notes that cost time to find:

* The item section of v5 is compressed as a whole and **the size int sits at the end
  of the table area**, before the compressed frame. The data blocks start right after
  the compressed item frame, not after `item size` bytes (that is the uncompressed
  size, not a file offset).
* Version 3 stores its data blocks **uncompressed**, there is no zlib there.
* **An empty data block still occupies bytes.** A client walks the data offset table
  and refuses the whole map with `datafile: invalid file information: data offset not
  greater than previous` when an offset is not strictly greater than the one before
  it, so two blocks with no bytes between them make a map unplayable before it loads.
  The reference editor writes one empty block for every empty quad or sound layer and
  keeps zero-length blocks that preserved DDRace items point at, so this is not a
  corner case. The uncompressed size of such a block stays 0 - a reader never inflates
  it - but the stored form is the compressed frame of an empty input (a 2 byte zlib
  stream, a 9 byte zstd frame), which is what makes the offset move.
* There is no security token, no "MAP" wrapper and no varint packing in any version.
  `SHA256`/`CRC32` exist in the reference reader for archiving only.

## 2. Item types and their versions

```
0 VERSION   1 INFO      2 IMAGE     3 ENVELOPE
4 GROUP     5 LAYER     6 ENVPOINTS 7 SOUND (0.7 and later only)
```

| item | v1 | v2 | v3 | v4 |
|---|---|---|---|---|
| IMAGE | 24 B | 28 B (`m_MustBe1`) | | |
| GROUP | 28 B | 48 B (+ clipping) | 60 B (+ name) | |
| LAYER (tiles) | legacy | 60 B | 72 B (+ name) | 72 B, run length tiles |
| ENVELOPE | 48 B | 52 B (+ synchronized) | 52 B; the *points* grow to 88 B and carry bezier tangents | |

DDRaceNetwork additions:

* the tiles layer grows to **92 B**: after the name it stores five physics data
  indices `m_Tele, m_Speedup, m_Front, m_Switch, m_Tune`. There is also an 80 B
  legacy variant that has the indices but no name.
* the info item grows to **24 B** with `m_Settings`.
* `LAYERTYPE_*` gains FRONT 4, TELE 5, SPEEDUP 6, SWITCH 7, TUNE 8, SOUNDS 10, and
  the layer flags `TILESLAYERFLAG_TELE` 2, `SPEEDUP` 4, `FRONT` 8, `SWITCH` 16,
  `TUNE` 32.
* the on-disk item type `0xffff` is DDRaceNetwork's extension table
  (`ITEMTYPE_EX`), which maps a UUID to the small id an item uses. The UUID
  registered items themselves sit just below it (`0xfffe`, `0xfffd`, …), for
  example the automapper configuration and the separate envelope bezier points.
  The editor preserves every type it does not model verbatim (see below).

The tile struct is always 4 bytes: `m_Index, m_Flags, m_Skip, m_Reserved`. Tile flags
are `1 = xflip, 2 = yflip, 4 = opaque, 8 = rotate`. Game layer entities are stored as
`m_Index = entity id + ENTITY_OFFSET`, `ENTITY_OFFSET = 255 - 16*4 = 191`.

Short names are packed four bytes per int with **every byte biased by +128**, most
significant byte first, and the last byte of the array is always zero.

### The five export targets

The datafile container version and the map *item* versions are independent,
which is why each row of the matrix in `map.cpp` states both. `AllTargets()`
returns the five export targets, in this order:

| id | datafile | tilemap | image | group | envelope points | info | features |
|---|---|---|---|---|---|---|---|
| `06` Teeworlds 0.6 | 4 (zlib) | 3, 72 B | 1, 24 B | 3, 60 B | 24 B or 88 B | 20 B | bezier envelopes |
| `07` Teeworlds 0.7 | 4 (zlib) | 4, 72 B, run length | 2, 28 B | 3, 60 B | 24 B or 88 B | 20 B | sounds, bezier envelopes |
| `ddnet06` DDRaceNetwork 0.6 | 4 (zlib) | 3, 92 B, physics data indices | 1, 24 B | 3, 60 B | 24 B or 88 B | 24 B | physics, DDRace entities, sounds, bezier envelopes, extras |
| `ddnet07` DDRaceNetwork 0.7 | 4 (zlib) | 4, 92 B, physics data indices, run length | 2, 28 B | 3, 60 B | 24 B or 88 B | 24 B | same as `ddnet06` |
| `08` Teeworlds 0.8 | 5 (zstd) | 4, 72 B, run length | 2, 28 B | 3, 60 B | 24 B or 88 B | 20 B | sounds, bezier envelopes |

`ddnet` on its own resolves to `ddnet06` (`TargetFromId`), because that is the
flavour every existing DDRace map uses. The two DDRace targets are the same
format with one difference that matters to an editor: `ddnet06` writes tilemap
item version 3, which stores `width * height` plain tiles, while `ddnet07`
writes version 4, which keeps the same layout but compresses the tile data with
run lengths (section 3). The 0.7 flavour also uses image version 2, as Teeworlds
0.7 does. What the two share is the rest: the five physics data indices in the
92 B tilemap item, the DDRace game entities, the 24 B info item, the UUID items
the editor does not model, the sound item, and the Teeworlds bezier envelope
points.

The package profile (`unitwp`, section 5) is not an export target: it is the
superset `GetPackageTarget()` returns, and `AllTargets()` deliberately leaves it
out.

### Envelope curves are written the Teeworlds way

All five export targets carry the `ext_bezier_envelopes` bit, so a curve is
byte-for-byte the same in every one of them. A map with at least one bezier
point is written with the envelope *item* at version 3 and the single ENVPOINTS
item holding 88 B points: the 24 B base point (`m_Time`, `m_Curvetype`,
`m_aValues`) followed by the 64 B `CEnvPointBezier` (four channels of
`m_aInTangentdx`, `m_aInTangentdy`, `m_aOutTangentdx` and `m_aOutTangentdy`).
The envelope item body is still 52 B; the version number is what tells a reader
that the points are 88 B rather than 24 B. A map whose points are all step,
linear, slow, fast or smooth is written at version 2 with 24 B points, which is
the layout the reference reader expects when no point is bezier.

DDRaceNetwork stores the same tangents differently: it leaves the envelope item
at version 2 and puts a separate `mapitemtype-envpoints-bezier@ddnet.tw` UUID
item next to the 24 B points. Uniteed never writes that item, for either DDRace
target, so the curve lives in the envelope points and every other target can
read the same map.

A DDRaceNetwork client reads such a map correctly. Its
`CMapBasedEnvelopePointAccess` (ddnet `src/game/map/render_map.cpp`) looks at the
envelope items first: when any of them is version 3 or higher it selects the
88 B points and ignores the UUID item, and only when every envelope is version 1
or 2 does it look for the separate UUID item. The curve a DDRace client plays is
therefore the curve a Teeworlds 0.7 or 0.8 client plays, and when the DDRace
editor re-saves the map it converts it back to its own convention — envelope
version 2 plus the UUID bytes item — so the tangents survive a round trip
through the DDRace editor even though the on-disk shape changes.

A UUID item found in an imported map is preserved verbatim (section 6), but
preserving the item is not the same as reading its tangents into the model:
Uniteed reads 88 B points only when the envelope item says version 3, so a
DDRace-native map (version 2 plus the UUID item) loads with zero tangents.
Exporting it marks the envelopes version 3 when any point is bezier, and a
DDRace client then prefers those zero tangents over the preserved UUID item. The
bundled DDRace maps use no bezier points, so the selftest does not exercise this.

### What the targets gate

`map_extension` (`map.h`) is the bit set that says which DDRace features a target
can carry. It is what the editor's "allow DDRace extensions" switch is compared
against, and it is the reason the export never has to guess:

| feature | bit | `06` | `07` | `ddnet06` | `ddnet07` | `08` | package |
|---|---|---|---|---|---|---|---|
| physics layers (tele, speedup, front, switch, tune) | `ext_physics` | – | – | yes | yes | – | yes |
| DDRace game entities | `ext_game_entities` | – | – | yes | yes | – | yes |
| sounds | `ext_sounds` | – | yes | yes | yes | yes | yes |
| bezier envelope points | `ext_bezier_envelopes` | yes | yes | yes | yes | yes | yes |
| UUID items the editor does not model | `ext_extras` | – | – | yes | yes | – | yes |

The switch and the bits work together, and the export does exactly this:

| case | physics layer | DDRace entity in the game layer |
|---|---|---|
| target has the bit (`ddnet06`, `ddnet07`) | written as the 92 B tilemap item with its payload; the switch is irrelevant | written; the switch is irrelevant |
| target lacks the bit, switch off | skipped by the writer, and the warning says it was dropped and names the switch | written, because it is only a tile index; a warning says the target does not define it and it will show as an unknown tile |
| target lacks the bit, switch on | written, and the warning says a vanilla client ignores it | written; the same warning is emitted |

The switch does not add a bit to a profile: it widens what the writer will emit
(`Allows(ext_physics)` in `CMap::SaveToBytes` is the profile bit *or* the switch)
and changes the wording of the compatibility warnings. The editor gates *making*
the feature on the same switch, so a physics layer cannot be added to a vanilla
target while it is off, and a DDRace only entity placed then is refused with the
reason.

Inside the model the switch matters in three places (`editor.h`,
`editor_project.cpp`): `GameEntitiesAllowed()` is `switch || target has
ext_game_entities`, `game.set_entity` refuses a DDRace only entity when it is
false, and `layers.add` refuses a `tele`/`speedup`/`front`/`switch`/`tune` layer.
The map also records the switch (`m_bAllowExtensions`), so an export can report
what it dropped.

### Colours and envelopes

A colour in a map item (`CColor`) is **four ints**, not four bytes: that is what makes
the tiles layer 60 B in version 2 (12 header + 4 version + 12 size/flags + 16 colour +
8 colour envelope + 8 image/data), and it is why the tile data index of a 92 B DDRace
item sits at byte 56. Reading it as four bytes gives plausible looking but wrong
values.

Envelope points are 22.10 fixed point like everything else, so 1024 is 1.0: a colour
envelope holds 1024,0,0 for red and a position envelope holds 32*1024 for one tile.
A tile layer's colour is multiplied with its colour envelope; a quad's four corner
colours are multiplied with the quad's colour envelope, and a quad's position envelope
adds an offset in world units and a rotation in degrees around `m_aPoints[4]`.

Evaluation, matching the reference implementation the client and the upstream editor
share:

* the time wraps at the last point's time, so an envelope loops by itself
* before the first point, and for a single point, the last (only) point's value is used
* the curve type of the point *before* the time decides the interpolation:
  `LINEAR` 1, `SLOW` (a³) 2, `FAST` (1-(1-a)³) 3, `SMOOTH` (hermite) 4,
  `BEZIER` 5, and `STEP` (a = 0) 0
* a bezier solves x(t) = time per channel with the point's out tangent and the next
  point's in tangent, both clamped to the time span so the curve stays monotonic

## 3. Tile run length coding (tilemap item version >= 4)

The tile blob is a sequence of runs over the flattened layer (`i = 0 .. w*h-1`). Each
stored tile repeats `m_Skip + 1` times, at most 256 times, and a new run starts when
the index or the flags change. Versions 2 and 3 store `w*h` plain tiles and require
`m_Skip == 0`.

This is only the on-disk form. The runtime "skip" the clients compute in
`CLayers::InitTilemapSkip` (skipping empty tiles per row) is never persisted.

## 4. Version detection is not the datafile version

`CMapItemVersion::m_Version` is always 1 in every version, so it cannot be used to
tell 0.6 from 0.7. The differences that matter are the *item* versions above. The
editor therefore does not guess on export: the user picks a target. On import it
only guesses which mapres directory fits, from the tilemap version and the presence
of DDRace items.

## 5. .unitwp, the Uniteed package

```
magic      "UNITWP\0\1"   8 bytes
version    int32          1
flags      int32          reserved
chunks     { char id[4]; int32 size; bytes }
```

| chunk | content |
|---|---|
| `META` | `key=value` lines: `id` (UUID), `name`, `author`, `description`, `target`, `assets`, `created`, `updated`, `editor`, `source` |
| `MAP ` | the map as a datafile, serialised with the package profile |
| `EDIT` | the editor session as JSON text: camera, brush, selection, dock layout, per-layer flags. Opaque to the format layer, so `project.cpp` can grow it without a container change |
| `RULE` | the autorules document as JSON text (see section 8), absent when the project has none |
| `RSRC` | resource table: `int32 count`, then per resource `int32 name length, name, int32 data length, data` |
| `THUM` | an optional PNG thumbnail |

A project is this container and nothing else: the same file is what `Ctrl+S`
writes, what `--pack` writes and what `export.package` writes. Only the chunks
differ — a package made by `--pack` has `META` and `MAP `, a saved project adds
`EDIT` and `RULE`. A reader must ignore a chunk it does not know, which is why
`THUM` can be added later without a version bump. `CUnitwpPackage::FindChunk`
reads one chunk without decoding the map, which is what `project.list` uses to
fill a picker from a directory of large projects.

The package profile is a superset of every export target: datafile 5, DDRace
tilemaps with the physics data indices, sounds, every unknown item preserved, and
the envelope tangents kept in the points. Exporting the package to a target
afterwards re-applies that target's rules, so nothing is lost by storing it in the
package. Datafile 5 is not optional: `map.cpp` gives the profile version 5
unconditionally, and the writer refuses version 5 when the build has no zstd
(`CMakeLists.txt` compiles the support in or out), so a build without zstd cannot
write the package or the `08` target at all rather than falling back to datafile 4.

## 6. Preserving items the editor does not model

A DDRaceNetwork map contains items this editor has no layout for (the automapper
configuration, the separate envelope bezier item, anything newer). Their bodies are
copied verbatim and written back out. They reference the data blob table by index,
so those indices have to survive:

* on load, every data block that any preserved item can reach is kept. The check
  scans the item bodies for int values that are valid data indices, which is how the
  reference formats store them. Blocks that nothing references become empty
  placeholders up to the last referenced index, so no index shifts.
* on save, those blocks are written first, in order, and only then the blocks the
  editor generates.

The first save may therefore renumber nothing but drop unreachable blocks; from the
second save on the layout is fixed, which is what `--selftest` checks.

"Block that nothing references becomes empty placeholder" is where a subtle corruption
used to come from: a preserved item can point at a run of zero-length blocks (the
Connector map of the LastDay workspace has one such run of three), and writing those
as *no bytes at all* repeats a data offset, which a client rejects with `data offset
not greater than previous`. Every block now occupies at least its empty compressed
frame, so the offsets stay strictly increasing; `--selftest` asserts that property on
each target's own export before it compares anything else.

One limit is worth stating because it is observable: the preserved items are
written only for a profile with the `ext_extras` bit, so exporting to `06`, `07`
or `08` drops them on purpose, with the warning `CompatibilityWarnings` reports.
Type `0xffff` (DDRaceNetwork's `ITEMTYPE_EX` extension table) is written like any
other: the bundled `data/maps/07/Tsunami.map` carries two such items, and an
export to `ddnet06` comes back with all 21. `--selftest` compares `m_vExtras` by
type, id and body, so a future regression there fails the test instead of hiding.

## 7. Where the assets come from

```sh
git -C <teeworlds> archive 0.6-endofline data/mapres    | tar -x   # 0.6, 28 files
git -C <teeworlds> archive 0.7.6        datasrc/mapres  | tar -x   # 0.7, 32 files
cp <ddnet>/data/mapres/*.png ...                                   # DDRace, 54 files
```

0.8 ships the 0.7 images byte for byte, so `versions/07` serves both. Every one of
the 28 shared file names differs between 0.6 and 0.7 (most 0.7 backdrops are twice
the resolution), which is why they cannot share a directory. DDRaceNetwork keeps both
in one directory using the `_0.7` suffix (`grass_main.png` is its 0.6 art,
`grass_main_0.7.png` the 0.7 one), and those files are used by name exactly as the
maps reference them.

## 8. Autorules, the JSON rule document

The autorules a project carries in its `RULE` chunk are a JSON document rather
than DDRaceNetwork's line based `.rules` text, because a document can be diffed,
generated and edited by a script, and because it has somewhere to put a rule's
name (`rules_json.cpp` also *reads* the DDNet text format, so the bundled rule
files stay usable; writing it back is not implemented).

```json
{
  "autorules": 1,
  "name": "grass on death",
  "description": "...",
  "tileset": "grass_main",
  "configs": [
    {
      "name": "main",
      "region": [0, 0, 0, 0],
      "runs": [
        {
          "name": "run 1",
          "read": 6, "target": -1, "copy": true, "reference": 0,
          "rules": [
            {
              "tile": "hookable", "flags": ["hflip"], "name": "grass",
              "when":   [ {"at": [1, 0], "not": [0]} ],
              "modulo": [ {"every": [3, 1], "offset": [0, 0]} ],
              "random": 1.0, "skip_empty": false, "skip_full": false
            }
          ]
        }
      ]
    }
  ]
}
```

* a **configuration** is applied to one square of a layer. `region` is
  `[x, y, width, height]`, and `0 0 0 0` means the whole layer.
* a **run** names the layer it reads (`read`) and, when it does not write in
  place, where the result goes (`target`; `-1` is the layer the configuration was
  applied to). `copy` takes a snapshot of the read layer first, so a later run of
  the same configuration still sees the map as it was before the first run wrote
  to it. `reference` is DDRaceNetwork's automap reference: `0` keeps every tile
  of the read layer, `1..9` keep only the tiles of that role and clear the rest in
  the snapshot. It only has an effect on the first run of a configuration.
* the layer slots are the engine layer addresses: `0` is the game layer, `1..5`
  are tele, speedup, front, switch and tune, and `6`, `7`, … are the plain tile
  layers in map order.
* a **rule** writes `tile` with `flags`. Either may be a number or a name:
  `"tile": "death"` and `"tile": 2` are the same, and the roles are `game_layer`,
  `hookable`, `death`, `unhookable`, `freeze`, `unfreeze`, `deep_freeze`,
  `deep_unfreeze`, `live_freeze`, `live_unfreeze`. `flags` accepts `hflip`,
  `vflip` and `rotate`, or the raw bitmask under `flag_bits`.
* `when` is a list of position rules: `at` is an offset, and `is` / `not` is a
  list of tiles the cell at that offset must, or must not, be. `modulo` is a list
  of `every`/`offset` pairs, so `{"every": [3, 1]}` means every third column.
  `random` is the probability the rule fires, and `skip_empty` / `skip_full` skip
  source cells that are empty or carry no room.

The engine is deterministic for a given seed: the same rule set, the same input
layer and the same seed produce the same tiles. `rules.apply` reports the seed it
used in its `seed` result, so a run with `seed: 0` (let the engine choose) can be
repeated exactly by passing that value back.
