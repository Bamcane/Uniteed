# Driving Uniteed without a window

The editor is usable from a script. Every operation that does not need a mouse is
exposed as one JSON method, and the methods run against the *same* `CEditor` the
window drives, so a `tiles.set` from a script and a click in the canvas cannot
disagree about what an edit means. Nothing in this document is maintained by
hand: the method table in `src/game/editor/admin_ops.cpp` is the single source of
truth, `agent.describe` prints it and the command line builds its subcommands
from it, so a method that is listed is a method that runs and a method that runs
is a method that is listed.

There are two shapes of the interface, one implementation behind both:

* a **one-shot subcommand**, for a script that wants to do one thing:
  `uniteed rules.apply --group 1 --layer 0`. The process is the session, so the
  call has nothing to keep.
* a **resident session**, for an agent that wants a conversation: `uniteed
  --agent` reads one JSON request per line and writes one JSON response per
  line. The editor keeps its state between calls, so opening a project, making
  a dozen edits and exporting is a sequence of requests rather than a dozen
  process launches. This is the shape an agent should prefer.

## The two shapes

### One-shot subcommands

```
uniteed <method> [--key value ...] [--key=value ...] [--key] [--params '<object>']
```

The first argument selects the method; `--map.get` and `map.get` are the same
method, so both the plain and the option spelling work. The remaining arguments
become the `params` object:

* `--key value` sets `key` to `value`;
* `--key=value` is the same, without the space;
* a bare `--key` sets `key` to `true`;
* a value that parses as JSON is used as JSON, so `--region '[0,0,4,4]'` is an
  array and `--index 3` is a number, while `--name demo` stays a string;
* `--params '{"name":"demo"}'` merges a whole object; when a key is named twice,
  the argument that comes later wins.

The answer is one JSON document on stdout (pretty printed, so it spans lines),
and the exit status is `0` when the method succeeded and `1` when it failed. A
failure also prints `method: error` and, when the catalogue knows the parameter
list, a `parameters: ...` hint to **stderr**, so a script can read the JSON on
stdout and a human can read the reason on the terminal.

### The resident session

```
uniteed --agent [initial method and flags]
```

Anything after `--agent` that names a method is applied before the first line is
read, so `uniteed --agent project.open --path build/demo.unitwp` starts
with that project already open. If the initial request fails, the reason goes to
stderr and the process exits `1`; if it succeeds, it prints nothing to stdout,
which is why the first stdout line of such a session is the answer to the first
request on stdin.

The session then reads stdin line by line until end of input or a request that
says to quit. Each line is one request, each answer is one line on stdout.
Everything else — the startup banner, the informational log lines, a parse
error's context — goes to stderr.

```
$ ./build/uniteed --agent project.create --name demo --target 08 --width 16 --height 16
Uniteed 0.1.0-mvp agent: 78 methods, one JSON request per line
send {"method":"agent.describe"} for the list, or {"method":"quit"} to stop
{"id":1,"method":"tiles.set","ok":true,"result":{"index":1,"flags":1,...}}
```

## The protocol

One request is

```json
{"id": 1, "method": "tiles.set", "params": {"group": 1, "layer": 0, "x": 3, "y": 4, "index": 1}}
```

and the answer is

```json
{"id": 1, "method": "tiles.set", "ok": true, "result": {"index": 1, "flags": 0, "group": 1, "layer": 0, "x": 3, "y": 4}}
```

or, when the method failed,

```json
{"id": 1, "method": "tiles.set", "ok": false, "error": "tiles.set needs an \"x\" and a \"y\" inside the layer"}
```

The parts, in the order the code builds them:

* `id` is optional and is echoed back unchanged, whatever JSON value it is — a
  number, a string, an object. It is there so a client can match an answer to
  its request; the editor never interprets it.
* `method` is copied from the request. `params` is optional; anything that is
  not an object is treated as an empty object, so a method always receives a
  key-value container.
* `ok` is a boolean. On success `result` is present and always an object, even
  when the method has nothing to report (it is `{}` then). On failure `error` is
  present and `result` is absent. A method never returns both.
* A line that is not valid JSON is answered with
  `{"ok": false, "error": "json: ..."}` and a line that is valid JSON but has no
  `method` is answered with `{"ok": false, "error": "a request needs a \"method\""}`;
  neither of those two answers has an `id` or a `method`, because there was
  nothing to echo.
* As a convenience for driving the session by hand, a line that is just a JSON
  string — `"agent.ping"` — is treated as a request with that method and no
  parameters.

### The stdout and stderr contract

This is the part an agent can rely on, and it is the reason the two streams are
kept apart:

* **In `--agent` mode, stdout carries exactly one JSON document per line and
  nothing else.** The document is compact (no newlines inside it) and the stream
  is flushed after every answer, so a reader can block on one line at a time.
  No log line, banner, progress note or human hint is ever written to stdout.
* **Every log line goes to stderr.** The logger writes there, the session banner
  is on stderr, and the one-shot failure explanation and parameter hint are on
  stderr. A caller can therefore read stdout as JSON without filtering, and can
  read stderr when it wants to know what the editor was doing.
* The one exception to "one document per line" is the one-shot shape, which
  prints its single document pretty printed over several lines. It is still one
  document, and it is still valid JSON on stdout; only the resident session
  promises exactly one line per document.

One caveat, because it is observable today: a layer, group or envelope name
that fills or overflows its fixed field is decoded with a trailing `0x80` byte
(`src/base/string.h`), and the JSON writer does not escape it. A name of 11
bytes or more in a 12-byte field, or 31 bytes or more in a 32-byte field, can
therefore produce a line that a strict UTF-8 parser rejects. This is a bug in
the name packing, not part of the protocol, and it does not affect any other
string.

### Unknown methods

A method that is not in the table is answered

```json
{"id": 7, "method": "no.such_method", "ok": false, "error": "unknown method 'no.such_method'; call agent.describe for the list"}
```

The full catalogue, with each method's parameters, is always available in-band
through `agent.describe` or `agent.version`, so a client never has to be told
out of band what the build supports.

When the *first argument* of the process is not a method and does not start with
`-`, it is not an agent command at all: it falls through to the ordinary command
line, which prints its help. An unknown name is only an error when it arrives as
a request inside a session, or as a `--`-spelled command that the catalogue does
not know.

### Quitting

Send `{"method": "quit"}`. `exit` and `agent.quit` are accepted as the same
thing. The session answers

```json
{"ok": true, "method": "quit", "result": {}}
```

and ends with exit status `0`. Closing stdin (end of input) also ends the loop
cleanly, so a producer may simply finish its lines instead of sending a quit.

### Handshake

`agent.version` is the first request a client should make. It reports the name,
the build version, the protocol number (`1`) and the export targets this build
supports, so a client can check both directions before it starts editing.

## What the results share

Most methods return a small object of their own, but several return one of five
larger snapshots. They are described once here so the method reference can stay
terse; every key below is one the code actually writes.

### The project document (`project.get`, `project.*`, `export.package`)

| key | what it holds |
|---|---|
| `id` | the project UUID |
| `name` | the project name, which also names the export files |
| `author`, `description` | the metadata strings |
| `created`, `updated` | unix seconds as text |
| `target` | the export target the project was created with, as the string that was passed |
| `assets` | the mapres set the map is built against: `06`, `07` or a DDRace name |
| `path` | the file the project was loaded from, empty for a new one |
| `dirty` | whether there are unsaved edits |
| `projects_directory`, `export_directory` | the resolved directories (see below) |
| `allow_extensions` | the editor's "allow DDRace extensions" switch |
| `rules` | the autorules document (see below) |

On disk this is a `.unitwp` package — the same binary container `export.package`
writes and `--unpack` reads. Its chunks are `META` (key=value text: the fields above
except `path`, `dirty`, `allow_extensions`, the two directories and `rules`), `MAP`
(the map as a real datafile, package profile), `EDIT` (JSON: the camera, brush,
selection, dock layout and per-layer flags), `RULE` (JSON: the autorules document),
and optionally `RSRC` (the mapres files the map needs) and `THUM` (a thumbnail).
`strings <project>.unitwp` shows the metadata and both JSON documents, which is often
enough to check what a saved project holds without opening it.

### The map (`map.get`, `layers.*`, `groups.*`)

`map.get` returns `datafile_version`, `target`, `assets`, the four info strings
(`author`, `version`, `credits`, `license`), `has_ddnet_items`,
`allow_extensions`, `dirty`, `counts`, `groups`, `images` and `envelopes`.

`counts` holds `groups`, `layers`, `tiles`, `non_empty_tiles`, `images`,
`embedded_images`, `envelopes`, `quads` and `preserved_items` (items the editor
does not model but keeps).

A group is `index`, `name`, `offset` (`[x, y]`), `parallax` (`[x, y]`),
`clipping`, `clip` (`[x, y, width, height]`, only when clipping is on) and its
`layers`.

A layer, from `layers.get` or inside `groups`, is `group`, `index`, `name`,
`kind` (`tiles`, `game`, `tele`, `speedup`, `front`, `switch`, `tune`, `quads`,
`sounds` or `unknown`), `type`, `flags`, `detail`, `physics`, `game` and `slot`.
A tile layer adds `width`, `height`, `image`, `color` (`[r, g, b, a]`),
`color_env`, `tilemap_version` and `non_empty`, and its `tiles` array when the
caller asked for `tiles: true`. A tile is a plain index when its flags are zero
and `{"i": index, "f": flags}` otherwise, so a mostly empty layer stays
readable. A quad layer adds `image` and `quads`, each with `points` (five
`[x, y]` pairs), `colors` (four `[r, g, b, a]`), `pos_env` and `color_env`. A
sound layer adds `sound` and `sources`.

An image is `name`, `width`, `height`, `embedded` and `bytes` (the size of the
raw RGBA data when embedded). An envelope in `map.get` is `name`, `channels`,
`synchronized`, `bezier`, `length_ms` and its `points`, each with `time`,
`curve` (the numeric curve type, `0` step to `5` bezier) and `values` (four
22.10 fixed point ints).

### The selection (`select.get`, and the `selection` member of any edit)

`group`, `layer`, then `kind`, `name` and `slot` when that layer exists, then
`target`, `allow_extensions` (the switch), `extensions_allowed` (whether DDRace
extensions are currently permitted), `mode` (`tiles`, `quads` or `none`),
`brush_tile`, `brush_flags`, `brush_size`, `grid`, `zoom`, `envelope_time` and
`selected_quads`. Most editing methods return this object under `selection`
alongside `changed` (the method name that made the change) and `dirty`, so a
caller can apply a batch and read the resulting state back without a second
call.

### The autorules document (`rules.get`, `rules.set`)

```json
{
  "autorules": 1,
  "name": "death to hookable",
  "description": "…",
  "tileset": "grass_main",
  "configs": [
    {
      "name": "config 1",
      "region": [0, 0, 0, 0],
      "runs": [
        {
          "name": "run 1",
          "read": 6, "target": -1, "copy": true, "reference": 0,
          "rules": [
            {
              "tile": 1,
              "name": "rule 1",
              "flags": ["hflip"],
              "when": [{"at": [0, 0], "is": ["death"]}],
              "modulo": [{"every": [3, 1], "offset": [0, 0]}],
              "random": 1.0, "skip_empty": false, "skip_full": false
            }
          ]
        }
      ]
    }
  ]
}
```

`region` is `[x, y, width, height]` and an all-zero width and height means the
whole layer. A run names the engine slot it `read`s and the slot it writes to
(`target`, with `-1` meaning the layer the configuration is applied to), and
`copy` takes the snapshot that keeps two runs independent. `reference` is the
DDRace automap reference: `0` keeps every tile of the read layer, `1` to `9`
keep only the tiles of that role. A tile is a number or a role name, and the
roles are `game`, `hookable`, `death`, `unhookable`, `freeze`, `unfreeze`,
`deep_freeze`, `deep_unfreeze`, `live_freeze` and `live_unfreeze`. `when` is a
list of position conditions: `at` is an offset and either `is` or `not` lists
the tiles that must, or must not, be there. `modulo` fires only on every
`every[0]`th column and `every[1]`th row, measured from `offset`. `random` is a
probability, `skip_empty` and `skip_full` are the source-cell filters, and
`default: false` marks a rule that is not the fallback.

The engine slots are the engine's own layer addresses, not list positions:

| slot | layer |
|---|---|
| `0` | the game layer |
| `1` | telegun (tele) |
| `2` | speedup |
| `3` | front |
| `4` | switch |
| `5` | tune |
| `6`, `7`, … | the first, second, … plain tile layer |

### The envelope (`envelopes.get`)

`index`, `name`, `channels`, `synchronized` and `points`. A point is `time`,
`curve`, `values` (four ints) and `in_tangent` / `out_tangent`, each an array of
four `[dx, dy]` pairs (one per channel). The curve type is the numeric constant:
`0` step, `1` linear, `2` slow, `3` fast, `4` smooth, `5` bezier.

## The method reference

Every method is one row of the table in `src/game/editor/admin_ops.cpp`. A
parameter that is written with a default can be left out; "required" means the
method fails without it. Where a parameter defaults to "the selection", the
current `group`/`layer` from `select.get` is used.

### agent

| method | parameters and defaults | result |
|---|---|---|
| `agent.version` — the name, version and export targets of this build | none | `name`, `version`, `protocol` (`1`), `targets` (the five export ids) |
| `agent.describe` — every method, its parameters and what it does | none | `methods` (a list of `method`, `params`, `description`) and `formats` (the request and response shapes) |
| `agent.ping` — a round trip, so a client can check the session is alive | none | `pong` (`true`) |

### project

| method | parameters and defaults | result |
|---|---|---|
| `project.get` — the project: its metadata, its rules and where its files live | none | the project document described above |
| `project.create` — a new project with the standard game and tiles groups | `name` (`untitled`), `target` (`08`), `width` and `height` (both `50`, raised to at least `4`), `author` (`Uniteed`), `allow_extensions` (optional; the switch keeps its value when the key is absent) | `changed`, `selection`, `dirty`, `project` |
| `project.open` — open a project file, or a name in the projects directory | `path` (required): a `.unitwp` file, a directory, or a bare project name | `changed`, `selection`, `dirty`, `project` |
| `project.save` — write the project; without a path, under the projects directory | `name` (optional, and applied before the path is built), `target` (optional, changes the export target first), `path` (default `<projects>/<name>.unitwp`). Without a `path` in a session that has not read anything yet, the existing project is opened first and this session is saved on top of it, so a one-shot command cannot erase a map | `changed`, `selection`, `dirty`, `path`, `project` |
| `project.list` — the projects in the application data's projects directory | none | `directory` and `projects`, newest first, each entry `id`, `name`, `author`, `target`, `assets`, `updated`, `path` |
| `project.set_meta` — the project and map metadata | `name`, `author`, `description`, `version`, `credits`, `license` (all optional) | `changed`, `selection`, `dirty` |
| `project.set_target` — the export target | `target` (required: `06`, `07`, `ddnet06`, `ddnet07` or `08`) | `changed`, `selection`, `dirty`, `target` (the canonical id) |

### files

| method | parameters and defaults | result |
|---|---|---|
| `file.open` — open a map, a `.unitwp` package or a project | `path` (required) | `changed`, `selection`, `dirty` |
| `rules.import` — read a rules file: a Uniteed rules JSON or a DDRace `.rules` text | `path` (required) | `changed`, `selection`, `dirty`, `rules` |

### map

| method | parameters and defaults | result |
|---|---|---|
| `map.get` — the whole map | `tiles` (`false`; with `true`, every tile of every layer) | the map snapshot described above |
| `map.set_info` — the map info strings | `author`, `version`, `credits`, `license` (all optional) | `changed`, `selection`, `dirty` |
| `map.set_extensions` — the DDRace extensions switch, and the warnings it produces | `allow` (`true` when the key is absent or not a boolean) | `changed`, `selection`, `dirty`, `allow_extensions`, `warnings` |
| `map.validate` — check the map for the mistakes a client would refuse | none | `ok`, `problems` (a list of strings), `warnings` |

`map.validate` reports a map with no groups, a map with no game layer, a tile
layer whose tile array does not match its `width * height`, an image or colour
envelope index that does not exist, tile layers that disagree about the map
size, and envelope points out of time order. `problems` is empty and `ok` is
`true` when none of those hold; the two are independent, so a map can validate
cleanly and still carry compatibility warnings.

### groups

| method | parameters and defaults | result |
|---|---|---|
| `groups.list` — the groups with their parallax, offset and layer count | none | `groups` (each `index`, `name`, `parallax`, `offset`, `clipping`, `layers`) and `selection` |
| `groups.add` — a new group | `name` (`Group`) | `changed`, `selection`, `dirty` |
| `groups.set` — a group's properties | `group` (required index), `name`, `parallax` (`[x, y]`, each `100`), `offset` (`[x, y]`, each `0`), `clipping` (bool), `clip` (`[x, y, width, height]`) | `changed`, `selection`, `dirty` |
| `groups.delete` — remove a group and its layers | `group` (required index) | `changed`, `selection`, `dirty` |

### layers

| method | parameters and defaults | result |
|---|---|---|
| `layers.list` — every layer of every group, without its tiles | none | `layers`, each with its `group` and `index` |
| `layers.get` — one layer | `group` and `layer` (the selection), `tiles` (`false`) | one layer object; with `tiles: true` it carries the tile array |
| `layers.add` — a layer: tiles, game, tele, speedup, front, switch, tune, quads or sounds | `group` (the selected group, which must exist), `kind` (`tiles`), `name` (the kind), `width` and `height` (the last tile layer's size, or `50` by `50`), `image` (`-1`), `sound` (`-1`) | `changed`, `selection`, `dirty`, `group`, `layer` |
| `layers.set` — a layer's properties | `group`, `layer` (the selection), `name`, `image`, `detail` (bool), `flags` (a raw int), `color_env`, `pos_env` | `changed`, `selection`, `dirty` |
| `layers.delete` — remove a layer | `group`, `layer` (the selection) | `changed`, `selection`, `dirty` |
| `layers.reorder` — move a layer inside its group | `group` (the selection), `from` and `to` (required, inside the group) | `changed`, `selection`, `dirty` |

The `kind` names accepted by `layers.add` are `tiles` (or `tile`), `game`,
`tele` (or `teleport`), `speedup`, `front`, `switch`, `tune`, `quads` (or
`quad`) and `sounds` (or `sound`). A physics kind is refused unless the target
is a DDRace one or the extensions switch is on. `layers.set` with `pos_env` sets
the position envelope of *every* quad in the layer; `color_env` sets the layer's
own colour envelope.

### tiles

| method | parameters and defaults | result |
|---|---|---|
| `tiles.get` — one tile, or the whole layer when x and y are absent | `group`, `layer` (the selection), `x` and `y` (both `-1`) | with `x` and `y`: `index` and `flags`; without: the whole layer with its tile array |
| `tiles.set` — one tile | `group`, `layer`, `x` and `y` (required, inside the layer), `index` (`0`, `0` to `255`), `flags` (`0`: a mask int, or a list of `hflip`/`xflip`, `vflip`/`yflip`, `rotate`, `opaque`) | `index`, `flags`, `group`, `layer`, `x`, `y` |
| `tiles.fill` — a rectangle of one tile | `group`, `layer`, `region` (`[x, y, width, height]`; absent or short means the whole layer), `index` (`0`), `flags` (`0`) | `written`, `region` (`[x0, y0, x1, y1]`, inclusive) |
| `tiles.set_region` — a rectangle from the tile encoding `layers.get` returns | `group`, `layer`, `x` (`0`), `y` (`0`), `width` (the tile count), `height` (`1`), `tiles` (required: indices, or `{"i": index, "f": flags}` objects) | `written` |
| `tiles.resize` — resize the tile layers of the map | `width` and `height` (required, positive), `group` (`-1`, every group), `layer` (`-1`, every layer) | `resized`, `width`, `height` |

A tile's flags are only written on a plain tile layer: a game layer cell keeps
its flags, because the compiler reads them. `tiles.set` on a game layer with an
entity index is how an entity is moved in raw form; `game.set_entity` is the
named way.

### game

| method | parameters and defaults | result |
|---|---|---|
| `game.list_entities` — the entity names a game layer understands | none | `entities` (each `index`, `name`, `ddnet_only`) and `allowed_in_this_target` |
| `game.list_entities_in_map` — every entity already placed, with its position | none | `entities` (each `group`, `layer`, `x`, `y`, `entity`, `flags`) |
| `game.set_entity` — place an entity on the game layer by name or index | `x` and `y` (required, inside the layer), `entity` (required: a name or an index), `rotation` (`0`, masked to six bits), `group` and `layer` (the selection; the game layer is found when the selected one is not it) | `group`, `layer`, `x`, `y`, `entity`, `raw` |

The entity names are `spawn`, `spawn_red`, `spawn_blue`, `flag_red`,
`flag_blue`, `armor`, `health`, `shotgun`, `grenade`, `ninja`, `laser`,
`laser_fast_ccw`, `laser_normal_ccw`, `laser_slow_ccw`, `laser_stop`,
`laser_slow_cw`, `laser_normal_cw`, `laser_fast_cw`, `laser_short`,
`laser_medium`, `laser_long`, `laser_c_slow`, `laser_c_normal`, `laser_c_fast`,
`laser_o_slow`, `laser_o_normal`, `laser_o_fast`, `plasma_e`, `plasma_f`,
`plasma`, `plasma_u`, `crazy_shotgun_ex`, `crazy_shotgun`, `armor_shotgun`,
`armor_grenade`, `armor_ninja`, `armor_laser`, `dragger_weak`, `dragger`,
`dragger_strong`, `dragger_weak_nw`, `dragger_nw`, `dragger_strong_nw` and
`door`. The first twelve are the vanilla set; the rest are marked `ddnet_only`
and need a DDRace target or the extensions switch. `game.set_entity` also reads
an undocumented `keep_flags` boolean: when it is present, the tile's existing
flags are left alone instead of being replaced from `rotation`.

### physics

| method | parameters and defaults | result |
|---|---|---|
| `physics.add` — add an empty DDRace physics layer | `kind` (required: `tele`, `speedup`, `front`, `switch` or `tune`) | `changed`, `selection`, `dirty` (a layer that already exists leaves the map alone and reports it in the status) |
| `physics.get` — the payload of a physics layer, one cell or all of it | `group`, `layer` (the selection), `x` and `y` (both `-1`) | `kind`, `element_size`, `count`, then `fields` for one cell or `bytes` for the whole payload |
| `physics.set` — write one cell of a physics payload | `group`, `layer`, `x` and `y` (required, inside the layer), `fields` (required array of bytes) | `changed`, `selection`, `dirty` |

The element size is the DDRace struct for the kind: `2` bytes for tele, `6` for
speedup, `4` for front, `4` for switch and `2` for tune. Writing the payload
also mirrors the visible tile index out of the type byte, so the picture follows
the data.

### envelopes

| method | parameters and defaults | result |
|---|---|---|
| `envelopes.list` — every envelope, and which one the selection uses | none | `envelopes` (each `index`, `name`, `channels`, `points`, `bezier`, `length_ms`), `colour_envelope_of_selection`, `position_envelope_of_selection` |
| `envelopes.get` — one envelope and its points, with their bezier tangents | `envelope` (required index) | `index`, `name`, `channels`, `synchronized`, `points` |
| `envelopes.create` — a new envelope | `channels` (`4`, clamped to `1` to `4`), `name` (optional) | `changed`, `selection`, `dirty`, `envelope` |
| `envelopes.set` — an envelope's properties | `envelope` (required), `name`, `channels` (`1` to `4`), `synchronized` (bool) | `changed`, `selection`, `dirty` |
| `envelopes.attach` — point a layer or a quad at an envelope | `envelope` (required), `to` (`colour`; `position`/`pos` for a quad, `sound`/`sound_level` for a sound layer), `group` and `layer` (the selection), `quad` (the selected quad, or the first) | `changed`, `selection`, `dirty` |
| `envelopes.add_point` — a new point | `envelope` (required), `time` (`0`, at least `0`), `curve` (`1` linear, clamped to `0` to `5`), `values` (four ints, default `0`) | `changed`, `selection`, `dirty`, `point` |
| `envelopes.set_point` — one point | `envelope` and `point` (required), `time`, `curve`, `values`, `in_tangent`, `out_tangent` | `changed`, `selection`, `dirty`, `point` |
| `envelopes.delete_point` — remove a point | `envelope` and `point` (required) | `changed`, `selection`, `dirty` |

A new envelope starts with one linear point at time `0`, so it evaluates as a
constant. `envelopes.set_point` with a new `time` re-sorts the points and
returns the index the point ended up at. The last point cannot be deleted: an
envelope always keeps one. `envelopes.attach` also reads an undocumented
`offset` int, which sets the sound layer's envelope offset when `to` is a sound
kind.

### rules

| method | parameters and defaults | result |
|---|---|---|
| `rules.get` — the project's autorules document | none | the autorules document described above |
| `rules.set` — replace the autorules document | the document itself as the parameter object | `changed`, `selection`, `dirty`, `rules` |
| `rules.new` — a fresh rule set with one empty configuration | `name` (optional) | `changed`, `selection`, `dirty`, `rules` |
| `rules.add_config` — a new configuration | `name` (optional) | `changed`, `selection`, `dirty`, `rules` |
| `rules.add_run` — a new run in a configuration | `config` (`0`) | `changed`, `selection`, `dirty`, `rules` |
| `rules.add_rule` — a new index rule, with its conditions, in one call | `config` (`0`), `run` (`0`), `tile` (the brush tile), `flags`, `name`, `when`, `modulo`, `random` (`1.0`), `skip_empty`, `skip_full` | `changed`, `selection`, `dirty`, `rules` |
| `rules.apply` — run the rules over a tile layer | `config` (the current one), `target` (an engine slot; derived from `group`/`layer` when absent), `group` and `layer` (the selection), `all` (`false`), `seed` (`0`) | `changed`, `selection`, `dirty`, `target_slot`, `seed` (the seed actually used), `status` |
| `rules.export` — write the rules to a file | `path` (default `<projects>/<name>.rules.json`), `format` (`json`) | `path`, `rules` |
| `export.package` — write a `.unitwp` package with the mapres files it needs | `path` (default `<export>/<target>/<name>.<target>.unitwp`), `project` (a project to open first, which is what a one-shot invocation needs) | `path`, `status` |

`rules.new` makes one configuration named `config 1` with one run named
`run 1`, reading slot `6`. `rules.add_rule` writes the tile's flags, name,
conditions, modulo rules and filters onto the rule it just added in the run named
by `run`, so `config`/`run` say where the rule goes. `rules.apply` with
`all: true` runs every configuration in order and ignores `config`. A `seed` of
`0` makes the engine pick one and the result reports it in `seed`, so a random
run can be repeated by applying the same seed again. `rules.export` writes the
JSON document only: the DDRace `.rules` text is read by `rules.import` but not
written yet, and asking for `format: "ddnet"` or `"text"` is refused.

### images and sounds

| method | parameters and defaults | result |
|---|---|---|
| `images.list` | none | `images` (each `index`, `name`, `width`, `height`, `embedded`, `bytes`, `references`) and `unused` (the index of an image no layer uses, or `-1`) |
| `images.add` | `path`, `mapres`, `name` (`image`), `width`, `height`, `rgba_base64` | `changed`, `selection`, `dirty`, `index`, `images` (the new count). A `path` is decoded (PNG, JPEG, TGA, BMP, GIF); a `mapres` name is taken from the version's directory and stays **external**; `rgba_base64` plus `width`/`height` adds an embedded image from raw pixels. A name the map already uses replaces that image |
| `images.set` | `index` (required), `name`, `path` (replace the pixels), `embed`, `external` | `changed`, `selection`, `dirty`, `index`, `status` |
| `images.export` | `index` (required), `path` (default `<export>/<name>.png`) | `path` |
| `images.remove` | `index` (required) | `changed`, `selection`, `dirty`, `images`. Every layer that referenced a higher index is renumbered; a layer that used the removed one is left with no image |
| `sounds.list` | none | `sounds` (each `index`, `name`, `external`, `bytes`, `references`, `length_seconds`) and `unused` |
| `sounds.add` | `path`, `resource`, `name` (`sound`), `opus_base64` | `changed`, `selection`, `dirty`, `index`, `name`, `length_seconds`. A file or `base64` is added **embedded** (a map is where a sound's bytes live); `resource` looks up `audio/<name>.opus` |
| `sounds.set` | `index` (required), `name`, `embed`, `external` | `changed`, `selection`, `dirty` |
| `sounds.export` | `index` (required), `path` (default `<export>/<name>.opus`) | `path` |
| `sounds.remove` | `index` (required) | `changed`, `selection`, `dirty`. Sound layers above the removed index are renumbered |
| `sounds.preview` | `index`, `stop` | `changed`, `selection`, `dirty`. Plays through the editor's audio device; a build without opusfile says "this build has no sound preview" |
| `sound_layers.add` | `index`, `name` | `changed`, `selection`, `dirty`, `group`, `layer`. A sound layer names **one** sound for the whole layer |
| `sound_layers.set_sound` | `index`, `name` | `changed`, `selection`, `dirty`; the selected layer has to be a sound layer |
| `sound_sources.add` | `at` (`[x, y]`, default `[0, 0]`) | `changed`, `selection`, `dirty`, `source` (its index), `position` |
| `sound_sources.remove` | `source` (required) | `changed`, `selection`, `dirty` |

`length_seconds` is the real duration where opusfile is linked (the windowed build) and
a worst-case estimate from the file size where it is not. `images.add` with `mapres`
stays external on purpose: naming a mapres file is what keeps a map small, and `embed`
is the separate decision to carry the pixels.

### export

| method | parameters and defaults | result |
|---|---|---|
| `export.map` — write the map for a target | `target` (the current target), `path` (default `<export>/<target>/<name>.<target>.map`), `project` (open that project first, which a one-shot invocation needs) | `path`, `target`, `profile`, `datafile_version`, `status`, `warnings` |
| `export.all` — write the map for every target, or for the named ones | `targets` (an array of ids; every export target when absent). The comparison is against the canonical id, so write `ddnet06`, not `ddnet`; `project` as above | `exports`, each `target`, `path` and `ok` (with `error` when it failed) |

### selection

| method | parameters and defaults | result |
|---|---|---|
| `select.get` — the selection, the brush, the target and the extensions gate | none | the selection snapshot described above |
| `select.set` — select a layer | `group`, `layer` (the current selection) | `changed`, `selection` |
| `select.brush` — the tile brush | `tile`, `size` (`1`), `flags` | `changed`, `selection` |
| `select.quad` — the quad selection of a quad layer | `quads` (an array that replaces the selection) or `quad` (one index) | `changed`, `selection` |
| `select.view` — the camera | `zoom`, `center` (`[x, y]`), `grid` (bool), `fit` (bool; fits the map to the view) | `changed`, `selection` |

### history

| method | parameters and defaults | result |
|---|---|---|
| `undo` — undo the last edit | none | `changed`, `selection`, `dirty`, `status` (fails when there is nothing to undo) |
| `redo` — redo the last undo | none | `changed`, `selection`, `dirty`, `status` (fails when there is nothing to redo) |

## Worked examples

### Create a project, paint a tile and export it

The initial request after `--agent` is applied silently, so the first answer on
stdout is the `tiles.set`:

```
$ export UNITEED_EXPORT_DIR=/tmp/demo-export
$ printf '%s\n' \
    '{"id":1,"method":"tiles.set","params":{"group":1,"layer":0,"x":3,"y":4,"index":1,"flags":["hflip"]}}' \
    '{"id":2,"method":"export.map","params":{"target":"08"}}' \
    '{"method":"quit"}' \
  | ./build/uniteed --agent project.create --name demo --target 08 --width 16 --height 16
{"id":1,"method":"tiles.set","ok":true,"result":{"index":1,"flags":1,"group":1,"layer":0,"x":3,"y":4}}
{"id":2,"method":"export.map","ok":true,"result":{"path":"/tmp/demo-export/08/demo.08.map","target":"08","profile":"Teeworlds 0.8","datafile_version":5,"status":"exported demo.08.map for Teeworlds 0.8 (datafile v5)","warnings":[]}}
{"ok":true,"method":"quit","result":{}}
```

`flags: ["hflip"]` becomes the bit mask `1`, and the response says where the
tile landed rather than only that it worked.

### Apply an autorule

A rule reads one layer and writes into another. Here the plain tile layer (slot
`6`) is read and written in place, but the snapshot the run takes first keeps
the read from seeing its own output; the rule fires where the source tile at
the same cell is a death tile and writes a hookable tile with `hflip`:

```
$ printf '%s\n' \
    '{"id":1,"method":"tiles.set","params":{"group":1,"layer":0,"x":1,"y":1,"index":2}}' \
    '{"id":2,"method":"rules.new","params":{"name":"death to hookable"}}' \
    '{"id":3,"method":"rules.add_rule","params":{"tile":1,"flags":["hflip"],"when":[{"at":[0,0],"is":["death"]}]}}' \
    '{"id":4,"method":"rules.apply","params":{"target":6,"seed":7}}' \
    '{"id":5,"method":"tiles.get","params":{"group":1,"layer":0,"x":1,"y":1}}' \
    '{"method":"quit"}' \
  | ./build/uniteed --agent project.create --name ruledemo --target 08 --width 16 --height 16
{"id":1,"method":"tiles.set","ok":true,"result":{"index":2,"flags":0,"group":1,"layer":0,"x":1,"y":1}}
{"id":2,"method":"rules.new","ok":true,"result":{"changed":"rules.new","selection":{…},"dirty":true,"rules":{…}}}
{"id":3,"method":"rules.add_rule","ok":true,"result":{"changed":"rules.add_rule","selection":{…},"dirty":true,"rules":{…}}}
{"id":4,"method":"rules.apply","ok":true,"result":{"changed":"rules.apply","selection":{…},"dirty":true,"target_slot":6,"status":"autorules wrote 1 tiles over 1 runs in Tile layer 1"}}
{"id":5,"method":"tiles.get","ok":true,"result":{"index":1,"flags":1}}
{"ok":true,"method":"quit","result":{}}
```

The `rules` object in the two middle answers is the full autorules document
shown earlier; it is the same shape `rules.get` returns. The status string is
where the count of written tiles is reported.

## The catalogue

`uniteed --agent-describe` prints the whole method table as plain text: for
every method its name on one line, then the parameter line (omitted when the
method takes none) and the description, both indented by four spaces. It is the
human-readable view of the same data `agent.describe` returns as JSON, and the
two cannot disagree because both walk `ops::Table()`.

```
$ ./build/uniteed --agent-describe
Uniteed agent methods

Each one is a subcommand: uniteed <method> [--key value ...]
Or send them to a resident session: uniteed --agent

agent.version
    the name, version and export targets of this build
...
```

Three spellings reach it, and they differ only in the format:

| command | what it prints |
|---|---|
| `uniteed --agent-describe` | the plain text catalogue on stdout, exit `0` |
| `uniteed agent.describe` or `uniteed admin.describe` | the same plain text catalogue |
| `uniteed agent.list` or `uniteed admin.list` | the same catalogue as one JSON array, pretty printed |

Inside a session, the request `{"method": "agent.describe"}` returns the
catalogue as the `methods` array of an ordinary result, along with the two
`formats` strings. `agent.version` is the smaller handshake and is usually the
better first call.

## Where the files go

Everything the editor writes by itself lives under the application data
directory, which is `%LOCALAPPDATA%\uniteed` on Windows and normally
`~/.local/share/uniteed` on Linux (the platform's own data-home rule decides):

```
~/.local/share/uniteed/
  config.cfg                       the editor settings
  projects/                        what project.save writes
    demo.unitwp                    the project: map, session and autorules
    demo.rules.json                what rules.export writes
  export/                          what export.map and export.all write
    06/demo.06.map
    07/demo.07.map
    ddnet06/demo.ddnet06.map
    ddnet07/demo.ddnet07.map
    08/demo.08.map
```

The two directories are created on first use. The projects directory is where a
mapmaker's work lives — projects, packages and exported rule sets — and the
export directory is where finished maps go; the exports are grouped by target so
the same project can be exported for every client without the files colliding.
The name in both is the project name passed through a sanitiser that replaces
the bytes a filesystem may refuse and falls back to `untitled` when nothing is
left.

Both locations can be pointed elsewhere, which is what a test or a portable
install wants:

| variable | effect |
|---|---|
| `UNITEED_PROJECTS_DIR` | replaces the projects directory; an empty value is ignored |
| `UNITEED_EXPORT_DIR` | replaces the export directory; an empty value is ignored |

The overrides take effect in the one-shot subcommands and in a resident session
alike, and every response that mentions a path reports the resolved absolute
path, so an agent never has to guess where a file landed. Two more environment
variables exist for other purposes: `UNITEED_DATA_DIR` overrides the resource
root (the mapres and editor sprites) and `UNITEED_LOG` sets the log level
(`trace`, `info`, `warn`, `error`).

## What an agent cannot do yet

The list is honest and short, and everything on it is a missing method rather
than a hidden limitation:

* **Quads cannot be edited.** There is no method to create, delete, move or
  re-colour a quad. `layers.get` reports a quad layer's quads and
  `envelopes.attach` can point at one, but a quad layer added with `layers.add`
  stays empty, and the five points and four corner colours are read-only through
  the interface.
* **Rules can be added or replaced but not deleted one at a time.** There is no
  `rules.delete_rule`, `rules.delete_run` or `rules.delete_config`; removing one
  means writing the whole document back with `rules.set`.
* **The DDRace `.rules` text is read but not written.** `rules.import` accepts
  it, `rules.export` writes JSON only, and a request for the text format is
  refused.
* **A physics layer can be written into a vanilla target, but not made useful
  there.** With `allow_extensions: true`, `export.map` to `06`, `07` or `08`
  keeps the physics layers and warns that a vanilla client ignores them; without
  the switch they are dropped. What no method can do is make a 0.6/0.7/0.8
  client act on teleporters it does not know about.
* **Undo and redo move one step at a time** and the history is not exposed, so
  an agent cannot inspect or roll back to a named point in it.
* **There is no rendering or playback.** An agent cannot produce a PNG (that is
  the separate `--screenshot` mode of the command line, not a method) and cannot
  play audio.
* **One-shot calls do not share state.** Each `uniteed <method>` process is a
  fresh session; persistence is `project.save` plus `project.open`, or one
  resident `--agent` session. Three commands cover the common case without a
  session: `project.save` without a `path` opens the project under that name and
  writes the session on top of it, and `export.map`, `export.all` and
  `export.package` all take a `project` parameter that opens it first. Anything
  more elaborate wants `--agent`.

Two edges are worth knowing about before relying on them:

* Preserved items are only preserved for a target with the `ext_extras` bit:
  `ddnet06`, `ddnet07` and the package. Exporting to `06`, `07` or `08`
  deliberately leaves them out and says so in `warnings`, because those formats
  do not define them. `06` additionally has no sound item type.
* A project written by a newer build keeps the keys of its editor state that
  this build does not know: they are carried through `project.save` untouched,
  but there is no method to read them.
