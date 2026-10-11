#ifndef GAME_EDITOR_EDITOR_ICONS_H
#define GAME_EDITOR_EDITOR_ICONS_H

/*
	The pictograms the chrome draws.

	A tool strip of single letters is only readable to whoever already knows
	the keys, so every tool and every view toggle has a drawing instead. The
	drawings are SVG documents under data/icons: one file per icon, parsed once
	and rasterized by the vector texture cache at whatever pixel size the
	display needs, so the strip is as crisp on a dense display as on an
	ordinary one.

	The files are white on purpose. The colour of an icon is the theme's - it
	changes with the hover, the active and the disabled state - and the painter
	tints the texture with it, which is why one texture per size is enough for
	all three states.

	An icon is a texture, so this is window build only, like the panels that
	draw it.
*/

#if UNITEED_GUI

#include "base/math.h"
#include "base/storage.h"
#include "engine/ui.h"
#include "engine/vector_textures.h"

namespace game::editor_icons
{
	namespace ui = engine::ui;

	enum class icon
	{
		pan,      // move the view
		brush,    // paint what the brush carries
		bucket,   // flood a region
		eraser,   // paint empty tiles
		marquee,  // drag a region of the map into the brush
		frame,    // fit the map in the window
		grid,     // the tile grid
		keyboard, // the shortcut list
		// the dockers, which are what the toolbar switches on and off
		toolbox,  // the tool strip itself
		layers,   // the layer tree
		tiles,    // the tile or entity sheet
		assets,   // the images and sounds of the map
		envelope, // the curve workspace
		rules,    // the autorules workspace
	};

	// every icon of the strip, in the order the files are registered
	extern const icon g_aAllIcons[];
	extern const int g_IconCount;

	// "pan", the name a texture is cached under
	const char *Name(icon Icon);
	// "pan.svg", the file it is read from
	const char *FileName(icon Icon);

	/*
		Registers every icon from a directory, once, when the editor starts. A
		missing file is reported and that icon simply does not draw, which is
		what a broken installation should look like rather than a crash.
	*/
	void RegisterAll(engine::CVectorTextures &Textures, const storage::path &Directory);

	/*
		Draws the icon centred in Area, in colour Color. Inset is how much of
		the button the drawing leaves free around itself, and it is the size
		the texture is rasterized for, so a bigger interface really does get a
		bigger - not a stretched - drawing.
	*/
	void Draw(engine::CVectorTextures &Textures, ui::CPainter *pPainter, icon Icon, const base::rect &Area,
		ui::CColor Color, float Inset = 6.0f);

	/*
		The icon as a button: the frame ui::Button draws, with the pictogram
		where its label would be. It is here rather than in the widget set
		because the icon set is the editor's, and both the tool strip and the
		window bar want the same widget.
	*/
	bool Button(engine::CVectorTextures &Textures, ui::CPainter *pPainter, const engine::CInput &Input,
		const base::rect &Area, icon Icon, bool bActive = false, bool bEnabled = true, float Inset = 6.0f);
} // namespace game::editor_icons

#endif // UNITEED_GUI
#endif // GAME_EDITOR_EDITOR_ICONS_H
