#ifndef GAME_EDITOR_RENDER_H
#define GAME_EDITOR_RENDER_H

/*
	Headless rendering of a map region.

	The editor draws a map through SDL, which the command line build does not
	link. This draws the same content - the tile and quad layers of every group,
	through the same parallax, clipping, layer colour and envelope evaluation the
	window uses - into a plain RGBA buffer, so a map can be pictured on a server
	with no display. It is deliberately part of the model layer: it touches no
	renderer, so the GUI build and the headless build run the identical code.
*/

#include <string>

#include "base/math.h"
#include "base/storage.h"
#include "game/map/map.h"

namespace game
{
	/*
		The colour and point helpers the canvas and the renderer share. They are
		free functions rather than CEditor methods so a render does not need an
		editor (and therefore a graphics layer) to exist.
	*/
	CColor LayerColorAt(const CMap &Map, const CLayer &Layer, float Time);
	void QuadColorsAt(const CMap &Map, const CQuad &Quad, float Time, CColor *paColors);
	void QuadPointsAt(const CMap &Map, const CQuad &Quad, float Time, base::vec2 *paPoints);

	/*
		The quad operations the canvas and the JSON interface share, so a quad
		an agent builds is built by the same arithmetic the mouse uses.

		Coordinates are 22.10 fixed point, and a point is addressed the way a
		script sees it: a float in map pixels, x to the right and y downwards.
	*/
	// one coordinate in pixels, written in fixed point
	int QuadFixedFromUnits(double Units);
	double QuadUnitsFromFixed(int Value);

	/*
		Rounds every coordinate of a quad to a step of `Step` pixels, the same
		step in both axes; the reference editor snaps a dropped quad to the 32
		unit tile grid the same way. A step of zero leaves the quad alone.
	*/
	void SnapQuad(CMap &Map, const CQuad &Quad, int Step);

	// the four corners of a quad as the map stores them, clockwise from the
	// top left: x, y, width and height in pixels
	void QuadGuessCorners(base::vec2 TopLeft, base::vec2 Size, base::vec2 *paCorners);

	// which quad of the layer covers a point, topmost last so the newest wins,
	// or -1. `Time` is the envelope clock the shape is tested at.
	int QuadIndexAt(const CMap &Map, const CLayer &Layer, float Time, base::vec2 Point);

	// the pivot a quad is turned around: the centre of its four corners, in
	// pixels
	base::vec2 QuadCentroid(const CQuad &Quad);

	struct CMapRenderOptions
	{
		// the size of the picture in pixels
		int m_Width = 800;
		int m_Height = 600;
		// the world position (tile * 32) at the middle of the picture
		base::vec2 m_Center;
		// true to centre on the map when m_Center was not given
		bool m_bCenterSet = false;
		float m_Zoom = 1.0f;
		// the envelope preview time, the same clock the canvas reads
		float m_Time = 0.0f;
		// transparent leaves the alpha channel at zero behind the map
		bool m_bTransparent = false;
		// the colour behind the map otherwise
		CColor m_Background{24, 26, 30, 255};
	};

	struct CMapRenderStats
	{
		int m_Groups = 0;
		int m_Layers = 0;
		int m_Tiles = 0;
		int m_Quads = 0;
		int m_Images = 0;
		int m_MissingImages = 0;
	};

	/*
		Renders the map into OutPixels, width * height * 4 bytes of RGBA, and
		reports what was drawn. False with Error when there is nothing to render
		or the buffer cannot be allocated.
	*/
	bool RenderMapToRgba(const CMap &Map, const CMapRenderOptions &Options,
		storage::byte_vector &OutPixels, CMapRenderStats &Stats, std::string &Error);
} // namespace game

#endif // GAME_EDITOR_RENDER_H
