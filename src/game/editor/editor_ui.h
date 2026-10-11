#ifndef GAME_EDITOR_EDITOR_UI_H
#define GAME_EDITOR_EDITOR_UI_H

/*
	The drawing helpers the editor's panels share.

	They live in a header rather than in editor_draw.cpp because more than one
	translation unit draws a piece of the interface: the asset browser draws the
	same kind of mapped image the palette does, and a copy of this function in
	each file is exactly the kind of thing that drifts apart.
*/

#include "base/math.h"
#include "engine/ui.h"

namespace game::editor_ui
{
	namespace ui = engine::ui;

	/*
		Builds an image paint: the texture is mapped onto the rectangle given by
		TopLeft and Size, and only the texel window Tex is sampled. A tile sheet
		is one tile of a 16 by 16 grid, so the window is the cell.
	*/
	inline ui::CPaint ImagePaint(int Texture, base::vec2 TopLeft, base::vec2 Size,
		float TexX, float TexY, float TexWidth, float TexHeight,
		int TextureWidth, int TextureHeight, float Alpha = 1.0f)
	{
		ui::CPaint Paint;
		Paint.m_Kind = ui::CPaint::kind::image;
		Paint.m_Texture = Texture;
		Paint.m_ImageTopLeft = TopLeft;
		Paint.m_ImageSize = Size;
		Paint.m_TexX = TexX;
		Paint.m_TexY = TexY;
		Paint.m_TexWidth = TexWidth;
		Paint.m_TexHeight = TexHeight;
		Paint.m_TexTextureWidth = TextureWidth;
		Paint.m_TexTextureHeight = TextureHeight;
		Paint.m_Alpha = Alpha;
		/*
			A tile is one cell of a 16 by 16 sheet, and the last column sits
			exactly at u = 1: the reference clamps a tilemap for this reason,
			otherwise a rounding error wraps the sample to the far edge of the
			sheet and the tile shows the wrong art.
		*/
		Paint.m_Address = ui::address_mode::clamp;
		return Paint;
	}
} // namespace game::editor_ui

#endif // GAME_EDITOR_EDITOR_UI_H
