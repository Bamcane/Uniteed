#!/bin/sh
#
# Rebuilds docs/screenshots.
#
# The screenshot mode renders one frame offscreen and exits, and every option
# is one frame of real input, so the whole set can be rebuilt on a machine with
# no display and no mouse. This script is the recipe: the images in the README
# are exactly what it writes, and a layout change means running it again rather
# than hand cropping a picture.
#
#   sh tools/screenshots.sh            # rebuild everything
#   BIN=build/headless/uniteed sh ...  # never: the screenshot mode needs a window
#
# The build has to exist first:  cmake --build build

set -e

cd "$(dirname "$0")/.."
BIN=${BIN:-build/uniteed}
OUT=${OUT:-docs/screenshots}
MAPS=${MAPS:-data/maps}
# the scratch directory the rules demo project is built in, so a rebuild never
# depends on a file a previous run happened to leave in the data directory
WORK=${WORK:-build/shots}

if [ ! -x "$BIN" ]; then
	echo "$BIN is not built yet: cmake --build build" >&2
	exit 1
fi

mkdir -p "$OUT" "$WORK"
# A screenshot path is the one thing an option can get wrong in a way that
# destroys something, because the tool writes it: check that neither output
# directory points at the bundled maps.
case "$OUT$WORK" in
*data/maps*) echo "refusing to write screenshots into the bundled maps" >&2; exit 1 ;;
esac

# The settings file lives in the application data directory, and a session that
# was used normally has one: a remembered target or a grid setting would then be
# baked into the reference pictures. Point that directory at the scratch one, so
# what is written is the state each command line asks for and the images are the
# same on every machine. The maps and the assets are found through the data
# directory next to the executable, which this does not touch.
XDG_DATA_HOME=$WORK/xdg
export XDG_DATA_HOME

shot() {
	Out=$1
	shift
	echo "  $Out"
	"$BIN" --screenshot "$Out" "$@" >/dev/null
}

echo "writing $OUT"

# ---------------------------------------------------------------- the targets
# every bundled map, opened the way a user would open it
shot "$OUT/editor-08.png" "$MAPS/08/dm1.map"
shot "$OUT/editor-06.png" "$MAPS/06/coverage.map"
shot "$OUT/editor-ddnet-06.png" "$MAPS/07/Tsunami.map"

# the tile canvas, with a region of the sheet dragged out as the brush (the
# sheet is the bright grid under the "Tiles" head, at the top of the right
# column); the target buttons of the top row are not clicked, because opening
# a 0.8 map already selects 08
shot "$OUT/editor-tiles.png" "$MAPS/08/dm1.map" \
	--layer 5:0 \
	--move 1300,330 --press 1 --move 1360,392 --release 1

# the quad canvas: a quad layer selected, with no quad picked, so the panel
# shows the layer level controls
shot "$OUT/editor-quads.png" "$MAPS/08/dm1.map" --layer 0:0 --quad -2

# the game layer: the entity palette of a DDRace map
shot "$OUT/editor-entities.png" "$MAPS/08/dm1.map" --layer 6:2

# the colour picker, opened the way a user opens it: a right click on a corner
# of a quad. --click-point asks the editor where that corner is rather than
# naming a pixel, so the picture survives a change of the layout
shot "$OUT/editor-color.png" "$MAPS/08/dm1.map" --layer 0:0 --quad 0 --click-point 0:1,3

# ------------------------------------------------------------- the workspaces
shot "$OUT/editor-envelope.png" "$MAPS/08/dm1.map" --dock envelopes:tiles --envelope 0
shot "$OUT/editor-assets.png" "$MAPS/08/dm1.map" --dock assets:layers

# the autorules picture wants a project that has rules, so one is built with
# the same agent interface a script would use
printf '%s\n' \
	'{"id":1,"method":"project.create","params":{"name":"beach","target":"08","width":40,"height":30}}' \
	'{"id":2,"method":"rules.new","params":{"name":"beach"}}' \
	'{"id":3,"method":"rules.add_rule","params":{"tile":1,"when":[{"at":[0,0],"is":["death"]}]}}' \
	'{"id":4,"method":"rules.add_rule","params":{"tile":2,"when":[{"at":[0,-1],"is":["empty"]}]}}' \
	'{"id":5,"method":"project.save","params":{"path":"'"$WORK"'/rules.unitwp"}}' \
	'{"method":"quit"}' |
	"$BIN" --agent >/dev/null
shot "$OUT/editor-rules.png" "$WORK/rules.unitwp" --dock rules:tiles

# ------------------------------------------------------------------- the rest
# the Files menu of the title bar row, open, which is also the picture of what
# the drawn title bar looks like
shot "$OUT/editor-menu.png" "$MAPS/08/dm1.map" --menu files
# no map at all is the welcome page
shot "$OUT/editor-welcome.png"
# "?" is keycode 63; the shortcut list floats over whatever is on the canvas
shot "$OUT/editor-keys.png" "$MAPS/08/dm1.map" --key 63

echo "done"
