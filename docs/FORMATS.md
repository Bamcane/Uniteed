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
| ENVELOPE | 48 B | 52 B (+ synchronized) | points carry bezier tangents | |

DDRaceNetwork additions:

* the tiles layer grows to **92 B**: after the name it stores five physics data
  indices `m_Tele, m_Speedup, m_Front, m_Switch, m_Tune`. There is also an 80 B
  legacy variant that has the indices but no name.
* the info item grows to **24 B** with `m_Settings`.
* `LAYERTYPE_*` gains FRONT 4, TELE 5, SPEEDUP 6, SWITCH 7, TUNE 8, SOUNDS 10, and
  the layer flags `TILESLAYERFLAG_TELE` 2, `SPEEDUP` 4, `FRONT` 8, `SWITCH` 16,
  `TUNE` 32.
* unknown item types (0xfffe and below) are UUID registered items, for example the
  automapper configuration. The editor preserves them verbatim (see below).

The tile struct is always 4 bytes: `m_Index, m_Flags, m_Skip, m_Reserved`. Tile flags
are `1 = xflip, 2 = yflip, 4 = opaque, 8 = rotate`. Game layer entities are stored as
`m_Index = entity id + ENTITY_OFFSET`, `ENTITY_OFFSET = 255 - 16*4 = 191`.

Short names are packed four bytes per int with **every byte biased by +128**, most
significant byte first, and the last byte of the array is always zero.

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
| `META` | `key=value` lines: `id` (UUID), `name`, `author`, `target`, `created`, `updated`, `editor`, `source` |
| `MAP ` | the map as a datafile, serialised with the package profile |
| `EDIT` | the editor state as text, opaque to the format layer |
| `RSRC` | resource table: `int32 count`, then per resource `int32 name length, name, int32 data length, data` |
| `THUM` | an optional PNG thumbnail |

The package profile is a superset of every export target: datafile 5 (or 4 when the
build has no zstd), DDRace tilemaps with the physics data indices, sounds, every
unknown item preserved, and the envelope tangents kept in the points. Exporting the
package to a target afterwards re-applies that target's rules, so nothing is lost by
storing it in the package.

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
