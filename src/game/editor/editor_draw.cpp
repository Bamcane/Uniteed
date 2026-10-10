/*
	All the drawing of the editor, in one immediate mode pass: the map under the
	camera, then the panels on top of it.
*/

#include <algorithm>
#include <array>
#include <cstring>

#include <SDL3/SDL_keycode.h>

#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/ui.h"
#include "game/assets.h"
#include "game/editor/editor.h"

namespace game
{
	namespace ui = engine::ui;

	namespace
	{
		constexpr float TileSize = 32.0f;
		constexpr float RowHeight = 22.0f;

		// builds an image paint: the texture is mapped onto the rectangle given
		// by TopLeft and Size, and only the texel window Tex is sampled. A tile
		// sheet is one tile of a 16 by 16 grid, so the window is the cell.
		ui::CPaint ImagePaint(int Texture, base::vec2 TopLeft, base::vec2 Size,
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
				otherwise a rounding error wraps the sample to the far edge of
				the sheet and the tile shows the wrong art.
			*/
			Paint.m_Address = ui::address_mode::clamp;
			return Paint;
		}

		// a linear gradient from one colour to the other along an axis
		ui::CPaint GradientPaint(base::vec2 Start, base::vec2 End, ui::CColor Inner, ui::CColor Outer)
		{
			ui::CPaint Paint;
			Paint.m_Kind = ui::CPaint::kind::linear_gradient;
			Paint.m_Start = Start;
			Paint.m_End = End;
			Paint.m_Inner = Inner;
			Paint.m_Outer = Outer;
			return Paint;
		}

		// a tile sheet is 16 by 16 cells; the texture is as wide as the sheet
		// is drawn, so one cell is a sixteenth of it
		ui::CPaint TilePaint(int Texture, int Index, float X, float Y, float Size, int TextureWidth,
			int TextureHeight, float Alpha)
		{
			const int Col = Index % 16;
			const int Row = Index / 16;
			const float CellWidth = static_cast<float>(TextureWidth) / 16.0f;
			const float CellHeight = static_cast<float>(TextureHeight) / 16.0f;
			return ImagePaint(Texture, base::vec2(X, Y), base::vec2(Size, Size),
				static_cast<float>(Col) * CellWidth, static_cast<float>(Row) * CellHeight,
				CellWidth, CellHeight, TextureWidth, TextureHeight, Alpha);
		}

		// draws one tile of a tile sheet. Alpha carries the layer colour; the
		// rgb part of it is baked into the texture instead, because an image
		// fill can only be faded, not tinted.
		void DrawTile(ui::CPainter *pPainter, int Texture, int Index, int Flags, float X, float Y, float Size,
			int TextureWidth, int TextureHeight, float Alpha = 1.0f)
		{
			if(Texture <= 0 || Index <= 0 || Alpha <= 0.0f || TextureWidth <= 0 || TextureHeight <= 0)
				return;

			pPainter->Save();
			pPainter->Translate(X, Y);
			if(Flags & TILEFLAG_ROTATE)
			{
				pPainter->Translate(Size, 0.0f);
				pPainter->Rotate(3.14159265358979323846f * 0.5f);
			}
			if(Flags & TILEFLAG_HFLIP)
			{
				pPainter->Translate(Size, 0.0f);
				pPainter->Scale(-1.0f, 1.0f);
			}
			if(Flags & TILEFLAG_VFLIP)
			{
				pPainter->Translate(0.0f, Size);
				pPainter->Scale(1.0f, -1.0f);
			}
			// the tile is drawn in its own local space, so the paint's rectangle
			// is the origin and the texel window is the cell
			const ui::CPaint Paint = TilePaint(Texture, Index, 0.0f, 0.0f, Size, TextureWidth, TextureHeight, Alpha);
			pPainter->BeginPath();
			pPainter->Rect(0.0f, 0.0f, Size, Size);
			pPainter->FillPaint(Paint);
			pPainter->Fill();
			pPainter->Restore();
		}

		ui::CColor ToColor(const CColor &Color)
		{
			return ui::RGBA(static_cast<unsigned char>(base::clamp(Color.r, 0, 255)),
				static_cast<unsigned char>(base::clamp(Color.g, 0, 255)),
				static_cast<unsigned char>(base::clamp(Color.b, 0, 255)),
				static_cast<unsigned char>(base::clamp(Color.a, 0, 255)));
		}

		// the painter's colours for a quad's four corners
		std::array<ui::CColor, 4> ToColors(const CColor *paColors)
		{
			return {ToColor(paColors[0]), ToColor(paColors[1]), ToColor(paColors[2]), ToColor(paColors[3])};
		}

		// ------------------------------------------------------------------ hsv
		// the colour picker works in hsv because that is what an sv square needs;
		// the hue is handed in and back out so that picking a grey does not lose
		// the hue the user had
		void RgbToHsv(const CColor &Color, float &Hue, float &Saturation, float &Value, float &StoredHue)
		{
			const float R = static_cast<float>(Color.r) / 255.0f;
			const float G = static_cast<float>(Color.g) / 255.0f;
			const float B = static_cast<float>(Color.b) / 255.0f;
			const float Max = std::max({R, G, B});
			const float Min = std::min({R, G, B});
			const float Delta = Max - Min;

			Value = Max;
			Saturation = Max <= 0.0f ? 0.0f : Delta / Max;
			if(Delta <= 0.0f)
			{
				Hue = StoredHue;
				return;
			}

			float H = 0.0f;
			if(Max == R)
				H = std::fmod((G - B) / Delta, 6.0f);
			else if(Max == G)
				H = (B - R) / Delta + 2.0f;
			else
				H = (R - G) / Delta + 4.0f;
			H *= 60.0f;
			if(H < 0.0f)
				H += 360.0f;
			Hue = H;
			StoredHue = H;
		}

		CColor HsvToColorRaw(float Hue, float Saturation, float Value, int Alpha)
		{
			const float H = Hue / 60.0f;
			const int Sector = static_cast<int>(std::floor(H)) % 6;
			const float F = H - std::floor(H);
			const float P = Value * (1.0f - Saturation);
			const float Q = Value * (1.0f - Saturation * F);
			const float T = Value * (1.0f - Saturation * (1.0f - F));

			float R = 0.0f, G = 0.0f, B = 0.0f;
			switch(Sector)
			{
			case 0: R = Value; G = T; B = P; break;
			case 1: R = Q; G = Value; B = P; break;
			case 2: R = P; G = Value; B = T; break;
			case 3: R = P; G = Q; B = Value; break;
			case 4: R = T; G = P; B = Value; break;
			default: R = Value; G = P; B = Q; break;
			}

			CColor Color;
			Color.r = base::clamp(static_cast<int>(std::lround(R * 255.0f)), 0, 255);
			Color.g = base::clamp(static_cast<int>(std::lround(G * 255.0f)), 0, 255);
			Color.b = base::clamp(static_cast<int>(std::lround(B * 255.0f)), 0, 255);
			Color.a = Alpha;
			return Color;
		}

		ui::CColor HsvToColor(float Hue, float Saturation, float Value, int Alpha)
		{
			return ToColor(HsvToColorRaw(Hue, Saturation, Value, Alpha));
		}

		bool SameColor(const CColor &A, const CColor &B)
		{
			return A.r == B.r && A.g == B.g && A.b == B.b && A.a == B.a;
		}

		// the four corners as screen points, in the (top left, top right, bottom
		// right, bottom left) order the colours use
		void QuadScreenPoints(const base::view_transform &View, const base::vec2 *paWorld, base::vec2 *paScreen)
		{
			for(int i = 0; i < 4; i++)
				paScreen[i] = View.WorldToScreen(paWorld[i]);
		}

		ui::CColor EntityColor(int Entity)
		{
			switch(Entity)
			{
			case ENTITY_SPAWN: return ui::RGB(230, 230, 230);
			case ENTITY_SPAWN_RED: return ui::RGB(235, 90, 90);
			case ENTITY_SPAWN_BLUE: return ui::RGB(95, 140, 245);
			case ENTITY_FLAGSTAND_RED: return ui::RGB(200, 60, 60);
			case ENTITY_FLAGSTAND_BLUE: return ui::RGB(60, 90, 200);
			case ENTITY_ARMOR_1: return ui::RGB(240, 220, 120);
			case ENTITY_HEALTH_1: return ui::RGB(120, 230, 120);
			case ENTITY_WEAPON_SHOTGUN: return ui::RGB(190, 150, 90);
			case ENTITY_WEAPON_GRENADE: return ui::RGB(120, 200, 160);
			case ENTITY_POWERUP_NINJA: return ui::RGB(90, 90, 110);
			case ENTITY_WEAPON_LASER: return ui::RGB(120, 190, 230);
			default: return ui::RGB(220, 150, 240);
			}
		}

	} // namespace

	// ------------------------------------------------------------------ canvas
	void CEditor::DrawTileLayer(ui::CPainter *pPainter, const CLayer &Layer, bool Selected)
	{
		/*
			The layer colour is a real RGBA multiplier over the tiles and the
			client applies it always, not only to the layer being edited. Its
			alpha goes to the image pattern, its rgb to a pre tinted copy of the
			sheet.
		*/
		const CColor Color = LayerColor(Layer);
		const int Texture = TextureForImage(Layer.m_Image, Color);
		const float Alpha = static_cast<float>(Color.a) / 255.0f;
		const base::rect World = m_LayerView.VisibleWorld();

		const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
		const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
		const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);

		if(Texture <= 0 && Layer.m_Image >= 0 && Selected)
		{
			// no texture: still show where the tiles are
			for(int y = y0; y < y1; y++)
				for(int x = x0; x < x1; x++)
				{
					const CTile *pTile = Layer.Tile(x, y);
					if(pTile == nullptr || pTile->m_Index == 0)
						continue;
					const base::vec2 Screen = m_LayerView.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
					ui::Rect(pPainter, base::rect::FromSize(Screen, base::vec2(TileSize * m_LayerView.m_Zoom, TileSize * m_LayerView.m_Zoom)), ui::RGBA(150, 150, 160, 90));
				}
			return;
		}

		if(Alpha <= 0.0f)
			return; // a layer colour of zero alpha hides the layer

		const float Size = TileSize * m_LayerView.m_Zoom;
		// the sheet's pixel size is what turns a tile index into a texel window
		int TextureWidth = 0, TextureHeight = 0;
		m_pGraphics->TextureSize(Texture, TextureWidth, TextureHeight);
		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_LayerView.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				DrawTile(pPainter, Texture, pTile->m_Index, pTile->m_Flags, Screen.x, Screen.y, Size,
					TextureWidth, TextureHeight, Alpha);
			}
		}
	}

	void CEditor::DrawGameLayer(ui::CPainter *pPainter, const base::rect &Canvas, const CLayer &Layer)
	{
		(void)Canvas;
		const base::rect World = m_LayerView.VisibleWorld();
		const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
		const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
		const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);
		const float Size = TileSize * m_LayerView.m_Zoom;
		const bool bLabel = Size >= 14.0f;

		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_LayerView.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				const base::rect Cell = base::rect::FromSize(Screen, base::vec2(Size, Size));
				if(pTile->m_Index >= ENTITY_OFFSET)
				{
					const int Entity = pTile->m_Index - ENTITY_OFFSET;
					const ui::CColor Color = EntityColor(Entity);
					ui::Rect(pPainter, base::rect::FromSize(Screen + base::vec2(Size * 0.15f, Size * 0.15f), base::vec2(Size * 0.7f, Size * 0.7f)), Color);
					if(bLabel)
						ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Screen.x + Size * 0.5f, Screen.y + Size * 0.62f), std::min(11.0f, Size * 0.45f),
							ui::RGB(20, 20, 24), std::to_string(Entity), Size, ui::text_align::center);
				}
				else
				{
					// a tile controller index, the client decides what it means
					ui::RectOutline(pPainter, Cell, ui::RGBA(255, 255, 255, 60));
					if(bLabel)
						ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Screen.x + Size * 0.5f, Screen.y + Size * 0.62f), std::min(11.0f, Size * 0.45f),
							ui::Theme.m_TextDim, std::to_string(pTile->m_Index), ui::text_align::center);
				}
			}
		}
	}

	void CEditor::DrawPhysicsLayer(ui::CPainter *pPainter, const base::rect &Canvas, const CLayer &Layer, int Flags)
	{
		(void)Canvas;
		const base::rect World = m_LayerView.VisibleWorld();
		const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
		const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
		const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);
		const float Size = TileSize * m_LayerView.m_Zoom;
		const ui::CColor Color = Flags == TILESLAYERFLAG_TELE ? ui::RGBA(120, 200, 255, 150) :
			Flags == TILESLAYERFLAG_SPEEDUP ? ui::RGBA(255, 210, 90, 150) :
			Flags == TILESLAYERFLAG_SWITCH ? ui::RGBA(190, 130, 255, 150) :
			Flags == TILESLAYERFLAG_TUNE ? ui::RGBA(255, 130, 200, 150) :
			ui::RGBA(140, 255, 160, 130);

		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_LayerView.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				ui::RectOutline(pPainter, base::rect::FromSize(Screen, base::vec2(Size, Size)), Color, 2.0f);
			}
		}
	}

	void CEditor::DrawQuadLayer(ui::CPainter *pPainter, const CLayer &Layer)
	{
		const base::rect Canvas = CanvasRect(m_pGraphics->WindowSize());

		for(std::size_t Index = 0; Index < Layer.m_vQuads.size(); Index++)
		{
			const CQuad &Quad = Layer.m_vQuads[Index];

			base::vec2 aWorld[4];
			QuadPoints(Quad, aWorld);

			base::vec2 aScreen[4];
			QuadScreenPoints(m_LayerView, aWorld, aScreen);

			// a cheap cull, most quads of a big map are off screen
			base::rect Bounds(aScreen[0], aScreen[0]);
			for(int i = 1; i < 4; i++)
				Bounds = base::rect(
					base::vec2(std::min(Bounds.m_TopLeft.x, aScreen[i].x), std::min(Bounds.m_TopLeft.y, aScreen[i].y)),
					base::vec2(std::max(Bounds.m_BottomRight.x, aScreen[i].x), std::max(Bounds.m_BottomRight.y, aScreen[i].y)));
			if(Bounds.Intersect(Canvas).Width() <= 0.0f || Bounds.Intersect(Canvas).Height() <= 0.0f)
				continue;

			const bool Selected = std::find(m_vSelectedQuads.begin(), m_vSelectedQuads.end(), static_cast<int>(Index)) != m_vSelectedQuads.end();
			const bool Hovered = m_QuadHover == static_cast<int>(Index);

			// the four corner colours with the quad colour envelope applied
			CColor aColors[4];
			QuadColors(Quad, aColors);
			if(aColors[0].a <= 0 && aColors[1].a <= 0 && aColors[2].a <= 0 && aColors[3].a <= 0)
				continue;

			/*
				The four stored points are the corners of the quad, not a
				perimeter walk: point 0 and 1 are the top edge, 2 and 3 the
				bottom one (the same layout DDNet's editor creates, with the
				texcoords (0,0) (1,0) (0,1) (1,1)). Joining them in stored order
				would cross the quad into a bowtie, so the outline is reordered
				here once.
			*/
			base::vec2 aOutline[4];
			for(int i = 0; i < 4; i++)
				aOutline[i] = aScreen[CQuad::s_aPerimeter[i]];

			/*
				A path from points that are already in perimeter order. The grid
				of cells below builds its corners with QuadPointAt, which walks
				them top left, top right, bottom right, bottom left; routing
				those through the stored corner order again would cross every
				cell into a bowtie, which is exactly what it used to do.
			*/
			const auto Path = [pPainter](const base::vec2 *paPoints) {
				pPainter->BeginPath();
				pPainter->MoveTo(paPoints[0].x, paPoints[0].y);
				for(int i = 1; i < 4; i++)
					pPainter->LineTo(paPoints[i].x, paPoints[i].y);
				pPainter->ClosePath();
			};

			const bool bFlat = SameColor(aColors[0], aColors[1]) && SameColor(aColors[0], aColors[2]) && SameColor(aColors[0], aColors[3]);

			// a quad may carry a texture, which is stretched over it with the
			// quad's own texture coordinates; a quad without one is just a
			// coloured shape
			int QuadTexture = -1;
			if(Layer.m_Image >= 0)
			{
				CColor Tint;
				Tint.r = Tint.g = Tint.b = 255;
				Tint.a = 255;
				QuadTexture = TextureForImage(Layer.m_Image, Tint);
			}

			if(QuadTexture > 0)
			{
				// the texture coordinates are fixed point, 1.0 being one tile
				base::vec2 aTexcoords[4];
				for(int i = 0; i < 4; i++)
					aTexcoords[i] = base::vec2(base::FixedToFloat(Quad.m_aTexcoords[i].x),
						base::FixedToFloat(Quad.m_aTexcoords[i].y));

				/*
					The surface is subdivided so a quad that is not a rectangle
					still shows a curved gradient, and because a single pair of
					triangles would show the colour interpolation as a diagonal
					crease across a non planar quad.
				*/
				const int Cells = base::clamp(static_cast<int>(std::max(Bounds.Width(), Bounds.Height()) / 32.0f), 1, 16);
				const std::array<ui::CColor, 4> aQuadColors = ToColors(aColors);
				pPainter->TexturedQuad(QuadTexture, aScreen, aTexcoords, aQuadColors.data(), Cells);
			}
			else if(bFlat)
			{
				Path(aOutline);
				pPainter->FillColor( ToColor(aColors[0]));
				pPainter->Fill();
			}
			else
			{
				/*
					A quad whose corners differ is a gradient, and a path fill
					takes a single colour. Filling it as a grid of flat cells
					showed the gradient as banding, so it goes through the same
					per vertex mesh as a textured quad does, with no texture:
					the colours are then interpolated instead of staircased.
				*/
				const int Cells = base::clamp(static_cast<int>(std::max(Bounds.Width(), Bounds.Height()) / 24.0f), 1, 16);
				const base::vec2 aNoTexcoords[4] = {base::vec2(), base::vec2(), base::vec2(), base::vec2()};
				const std::array<ui::CColor, 4> aQuadColors = ToColors(aColors);
				pPainter->TexturedQuad(-1, aScreen, aNoTexcoords, aQuadColors.data(), Cells);
			}

			Path(aOutline);
			pPainter->StrokeColor( Selected ? ui::Theme.m_Accent : (Hovered ? ui::RGBA(200, 210, 230, 170) : ui::RGBA(200, 210, 230, 90)));
			pPainter->StrokeWidth( Selected ? 2.0f : 1.0f);
			pPainter->Stroke();

			// the pivot point the position envelope turns the quad around
			if(Selected)
			{
				const base::vec2 Pivot = m_LayerView.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
				ui::Rect(pPainter, base::rect::FromSize(Pivot - base::vec2(3.0f, 3.0f), base::vec2(6.0f, 6.0f)), ui::Theme.m_Accent);
			}
		}
	}

	void CEditor::DrawCursor(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		if(!m_bCursorValid)
			return;
		const float Size = TileSize * m_View.m_Zoom;
		const int TileX = static_cast<int>(std::floor(m_CursorWorld.x / TileSize));
		const int TileY = static_cast<int>(std::floor(m_CursorWorld.y / TileSize));
		const int Half = m_BrushSize / 2;
		const base::vec2 Screen = m_View.WorldToScreen(base::vec2((TileX - Half) * TileSize, (TileY - Half) * TileSize));
		const base::rect Cursor = base::rect::FromSize(Screen, base::vec2(Size * m_BrushSize, Size * m_BrushSize));
		if(Cursor.Intersect(Canvas).Width() <= 0.0f)
			return;
		ui::RectOutline(pPainter, Cursor, ui::Theme.m_Accent, 2.0f);
	}

	void CEditor::DrawMap(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		pPainter->Save();
		pPainter->Scissor( Canvas.m_TopLeft.x, Canvas.m_TopLeft.y, Canvas.Width(), Canvas.Height());
		ui::Rect(pPainter, Canvas, ui::RGB(18, 19, 23));

		// The map is the rectangle the tile layers cover. Everything outside it
		// is not part of the map, so the backdrop is dimmed there: the boundary
		// is then visible whether or not the grid is on. It is drawn before the
		// layers, so content that legitimately reaches past the tiles (background
		// quads, for example) is not darkened.
		const base::rect Bounds = MapBounds();
		const bool bHasBounds = Bounds.Width() > 0.0f && Bounds.Height() > 0.0f;
		const base::vec2 BoundsTopLeft = bHasBounds ? m_View.WorldToScreen(Bounds.m_TopLeft) : base::vec2();
		const base::vec2 BoundsBottomRight = bHasBounds ? m_View.WorldToScreen(Bounds.m_BottomRight) : base::vec2();
		if(bHasBounds)
		{
			// the four bands around the map rectangle; a single rect with a
			// hole would need an even-odd fill, which the painter does not do
			const ui::CColor Dim = ui::RGBA(0, 0, 0, 110);
			const base::rect Inner = base::rect(BoundsTopLeft, BoundsBottomRight).Intersect(Canvas);
			if(Inner.Width() > 0.0f && Inner.Height() > 0.0f)
			{
				// above and below
				ui::Rect(pPainter, base::rect(Canvas.m_TopLeft, base::vec2(Canvas.m_BottomRight.x, Inner.m_TopLeft.y)), Dim);
				ui::Rect(pPainter, base::rect(base::vec2(Canvas.m_TopLeft.x, Inner.m_BottomRight.y), Canvas.m_BottomRight), Dim);
				// left and right of the part that is inside the canvas
				ui::Rect(pPainter, base::rect(base::vec2(Canvas.m_TopLeft.x, Inner.m_TopLeft.y), base::vec2(Inner.m_TopLeft.x, Inner.m_BottomRight.y)), Dim);
				ui::Rect(pPainter, base::rect(base::vec2(Inner.m_BottomRight.x, Inner.m_TopLeft.y), base::vec2(Canvas.m_BottomRight.x, Inner.m_BottomRight.y)), Dim);
			}
			else
			{
				ui::Rect(pPainter, Canvas, Dim);
			}
		}

		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
		{
			const CGroup &Group = m_Map.m_vGroups[g];

			// every group is drawn through its own parallax and offset
			m_LayerView = GroupView(Group);

			pPainter->Save();
			if(Group.m_UseClipping && Group.m_ClipW > 0 && Group.m_ClipH > 0)
			{
				/*
					The clip rectangle is stored in map space, so it is projected
					with the plain view and not with this group's: the reference
					editor clips against the game group's mapping too. The
					scissor stacks, so it only ever narrows the canvas one.
				*/
				const base::vec2 ClipTopLeft = m_View.WorldToScreen(
					base::vec2(static_cast<float>(Group.m_ClipX), static_cast<float>(Group.m_ClipY)));
				const base::vec2 ClipBottomRight = m_View.WorldToScreen(
					base::vec2(static_cast<float>(Group.m_ClipX + Group.m_ClipW), static_cast<float>(Group.m_ClipY + Group.m_ClipH)));
				pPainter->Scissor(ClipTopLeft.x, ClipTopLeft.y,
					ClipBottomRight.x - ClipTopLeft.x, ClipBottomRight.y - ClipTopLeft.y);
			}

			for(std::size_t l = 0; l < Group.m_vLayers.size(); l++)
			{
				const CLayer &Layer = Group.m_vLayers[l];
				const CLayerView *pView = (g < m_vViewStates.size() && l < m_vViewStates[g].size()) ? &m_vViewStates[g][l] : nullptr;
				if(pView != nullptr && !pView->m_Visible)
					continue;
				const bool Selected = (static_cast<int>(g) == m_SelectedGroup && static_cast<int>(l) == m_SelectedLayer);

				if(Layer.IsTiles())
				{
					if(Layer.IsGameLayer())
						DrawGameLayer(pPainter, Canvas, Layer);
					else if(Layer.IsPhysicsLayer())
						DrawPhysicsLayer(pPainter, Canvas, Layer, Layer.m_Flags);
					else
						DrawTileLayer(pPainter, Layer, Selected);
				}
				else if(Layer.IsQuads())
				{
					DrawQuadLayer(pPainter, Layer);
				}
			}
			pPainter->Restore();
		}

		// The grid belongs to the tilemap, so it is clipped to the map boundary
		// and never suggested outside of it.
		if(m_bGrid && bHasBounds && m_View.m_Zoom > 0.15f)
		{
			const base::rect Area = m_View.VisibleWorld().Intersect(Bounds);
			if(Area.Width() > 0.0f && Area.Height() > 0.0f)
			{
				const ui::CColor Grid = ui::RGBA(255, 255, 255, 14);
				for(float x = std::floor(Area.m_TopLeft.x / TileSize) * TileSize; x < Area.m_BottomRight.x; x += TileSize)
				{
					const base::vec2 From = m_View.WorldToScreen(base::vec2(x, Area.m_TopLeft.y));
					const base::vec2 To = m_View.WorldToScreen(base::vec2(x, Area.m_BottomRight.y));
					ui::Line(pPainter, From, To, Grid);
				}
				for(float y = std::floor(Area.m_TopLeft.y / TileSize) * TileSize; y < Area.m_BottomRight.y; y += TileSize)
				{
					const base::vec2 From = m_View.WorldToScreen(base::vec2(Area.m_TopLeft.x, y));
					const base::vec2 To = m_View.WorldToScreen(base::vec2(Area.m_BottomRight.x, y));
					ui::Line(pPainter, From, To, Grid);
				}
			}
		}

		// the boundary itself, on top of the grid so it stays readable
		if(bHasBounds)
			ui::RectOutline(pPainter, base::rect(BoundsTopLeft, BoundsBottomRight), ui::RGBA(120, 132, 154, 220), 2.0f);

		DrawCursor(pPainter, Canvas);
		pPainter->Restore();
	}

	// ---------------------------------------------------------------- toolbar
	void CEditor::DrawToolbar(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x, Area.m_BottomRight.y), base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y), ui::Theme.m_Border);

		float x = 8.0f;
		const float Y = Area.m_TopLeft.y + 8.0f;
		const float H = 24.0f;
		const auto NextButton = [&x, pPainter](const std::string &Label) {
			const float Width = std::max(46.0f, ui::Measure(pPainter, 0, 13.0f, Label) + 22.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, 0.0f), base::vec2(Width, 24.0f));
			x += Width + 6.0f;
			return Rect;
		};
		(void)NextButton;

		const auto Button = [&](const std::string &Label, bool Active = false, bool Enabled = true) {
			const float Width = std::max(46.0f, ui::Measure(pPainter, m_pGraphics->Font(), 13.0f, Label) + 22.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, Y), base::vec2(Width, H));
			x += Width + 6.0f;
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, Active, Enabled);
		};
		const auto Separator = [&]() {
			ui::Line(pPainter, base::vec2(x + 3.0f, Y), base::vec2(x + 3.0f, Y + H), ui::Theme.m_Border);
			x += 12.0f;
		};

		if(Button("New"))
			NewMap(50, 50);
		if(Button("Open"))
		{
			m_bShowOpen = true;
			OpenBrowser();
		}
		if(Button(m_Dirty ? "Save *" : "Save"))
		{
			std::string Path = m_SourcePath;
			if(!base::EndsWith(Path, CUnitwpPackage::Extension))
				Path = storage::path(Path.empty() ? "untitled" : Path).stem().string() + CUnitwpPackage::Extension;
			SavePackage(Path);
		}
		if(Button("Export"))
		{
			m_ExportTarget = 3;
			m_TextInput = storage::path(m_SourcePath.empty() ? "untitled" : m_SourcePath).stem().string();
			m_bShowExport = true;
		}
		Separator();
		for(const map_target Target : AllTargets())
		{
			const CMapTargetProfile &Profile = GetTargetProfile(Target);
			if(Button(Profile.m_pId, m_Target == Target))
				m_Target = Target;
		}
		Separator();
		if(Button("Fit"))
			FitToMap();
		if(Button("Grid", m_bGrid))
			m_bGrid = !m_bGrid;
		if(Button("Undo", false, !m_vUndo.empty()))
			Undo();
		if(Button("Redo", false, !m_vRedo.empty()))
			Redo();
		if(Button("Help"))
			m_bShowHelp = true;

		const std::string Title = m_SourcePath.empty() ? "untitled" : storage::path(m_SourcePath).filename().string();
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 12.0f, Y + 16.0f), 13.0f, ui::Theme.m_TextDim, Title, ui::text_align::right);
	}

	// ------------------------------------------------------------ layer panel
	void CEditor::DrawLayerPanel(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_TopLeft.y), base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_BottomRight.y), ui::Theme.m_Border);

		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, Area.m_TopLeft.y + 22.0f), 14.0f, ui::Theme.m_Text, "Layers");

		const base::rect List = base::rect(
			base::vec2(Area.m_TopLeft.x, Area.m_TopLeft.y + 32.0f),
			base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y - 74.0f));

		pPainter->Save();
		pPainter->Scissor( List.m_TopLeft.x, List.m_TopLeft.y, List.Width(), List.Height());

		float y = List.m_TopLeft.y - m_LayerScroll;
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
		{
			CGroup &Group = m_Map.m_vGroups[g];
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 4.0f, y), base::vec2(List.Width() - 8.0f, RowHeight));
			if(Row.m_BottomRight.y > List.m_TopLeft.y && Row.m_TopLeft.y < List.m_BottomRight.y)
			{
				ui::Rect(pPainter, Row, ui::RGBA(255, 255, 255, 8));
				const std::string Label = Group.m_Name.empty() ? ("group " + std::to_string(g)) : Group.m_Name;
				ui::TextClipped(pPainter, m_pGraphics->FontBold(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.m_TopLeft.y + 15.0f), 12.5f, ui::Theme.m_Text, Label, Row.Width() - 16.0f);
				if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left))
				{
					m_SelectedGroup = static_cast<int>(g);
					m_SelectedLayer = 0;
				}
			}
			y += RowHeight;

			for(std::size_t l = 0; l < Group.m_vLayers.size(); l++)
			{
				CLayer &Layer = Group.m_vLayers[l];
				CLayerView *pView = ViewState(static_cast<int>(g), static_cast<int>(l));
				const bool Selected = (static_cast<int>(g) == m_SelectedGroup && static_cast<int>(l) == m_SelectedLayer);
				const base::rect LayerRow = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 12.0f, y), base::vec2(List.Width() - 16.0f, RowHeight));
				if(LayerRow.m_BottomRight.y > List.m_TopLeft.y && LayerRow.m_TopLeft.y < List.m_BottomRight.y)
				{
					if(Selected)
						ui::Rect(pPainter, LayerRow, ui::Theme.m_Selection);
					else if(ui::Hovered(Input, LayerRow))
						ui::Rect(pPainter, LayerRow, ui::Theme.m_Hover);

					if(pView != nullptr)
					{
						bool Visible = pView->m_Visible;
						const base::rect Eye = base::rect::FromSize(base::vec2(LayerRow.m_TopLeft.x + 3.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(12.0f, 12.0f));
						if(ui::Checkbox(pPainter, Input, Eye, Visible))
							pView->m_Visible = Visible;
						bool Locked = pView->m_Locked;
						const base::rect Lock = base::rect::FromSize(base::vec2(LayerRow.m_TopLeft.x + 19.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(12.0f, 12.0f));
						if(ui::Checkbox(pPainter, Input, Lock, Locked))
							pView->m_Locked = Locked;
					}

					std::string Kind = "?";
					if(Layer.IsTiles())
						Kind = Layer.IsGameLayer() ? "game" : (Layer.IsPhysicsLayer() ? "physics" : base::Format("tiles %dx%d", Layer.m_Width, Layer.m_Height));
					else if(Layer.IsQuads())
						Kind = base::Format("quads %zu", Layer.m_vQuads.size());
					else if(Layer.IsSounds())
						Kind = base::Format("sounds %zu", Layer.m_vSources.size());
					const std::string Label = (Layer.m_Name.empty() ? std::string("layer") : Layer.m_Name) + "  " + Kind;
					const ui::CColor TextColor = (pView != nullptr && !pView->m_Visible) ? ui::Theme.m_TextDim : ui::Theme.m_Text;
					ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(LayerRow.m_TopLeft.x + 36.0f, LayerRow.m_TopLeft.y + 15.0f), 12.0f, TextColor, Label, LayerRow.Width() - 60.0f);

					/*
						The colour of a tile layer, or of the quads selected in a
						quad layer, is shown as a swatch at the end of the row and
						opens the picker. It is the only way to see a layer colour
						without opening anything.
					*/
					if(Layer.IsTiles() || Layer.IsQuads())
					{
						const base::rect Swatch = base::rect::FromSize(base::vec2(LayerRow.m_BottomRight.x - 22.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(16.0f, 12.0f));
						CColor SwatchColor;
						bool bHasColor = false;
						if(Layer.IsTiles())
						{
							SwatchColor = LayerColor(Layer);
							bHasColor = true;
						}
						else if(Selected && !m_vSelectedQuads.empty() && m_vSelectedQuads[0] < static_cast<int>(Layer.m_vQuads.size()))
						{
							CColor aColors[4];
							QuadColors(Layer.m_vQuads[static_cast<std::size_t>(m_vSelectedQuads[0])], aColors);
							SwatchColor = aColors[0];
							bHasColor = true;
						}
						if(bHasColor)
						{
							ui::Rect(pPainter, Swatch, ToColor(SwatchColor));
							ui::RectOutline(pPainter, Swatch, ui::Hovered(Input, Swatch) ? ui::Theme.m_Accent : ui::Theme.m_Border);
							if(ui::Hovered(Input, Swatch) && Input.MousePressed(engine::CInput::mouse_button::left))
							{
								m_SelectedGroup = static_cast<int>(g);
								m_SelectedLayer = static_cast<int>(l);
								if(PickerColor(SwatchColor))
								{
									m_PickerColor = SwatchColor;
									m_bShowColor = true;
								}
							}
						}
					}

					if(ui::Hovered(Input, LayerRow) && Input.MousePressed(engine::CInput::mouse_button::left))
					{
						m_SelectedGroup = static_cast<int>(g);
						m_SelectedLayer = static_cast<int>(l);
						// another layer means another set of quads
						m_vSelectedQuads.clear();
					}
				}
				y += RowHeight;
			}
		}
		pPainter->Restore();

		// the list can be scrolled with the wheel over the panel
		if(ui::Hovered(Input, List) && Input.Scroll() != 0.0f)
			m_LayerScroll = std::max(0.0f, m_LayerScroll - Input.Scroll() * RowHeight * 2.0f);
		const float Total = y + m_LayerScroll - List.m_TopLeft.y;
		if(Total > List.Height())
			m_LayerScroll = std::min(m_LayerScroll, Total - List.Height());
		else
			m_LayerScroll = 0.0f;

		const float ButtonY = Area.m_BottomRight.y - 32.0f;
		float ButtonX = Area.m_TopLeft.x + 8.0f;
		const auto SmallButton = [&](const std::string &Label) {
			const float Width = std::max(56.0f, ui::Measure(pPainter, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(ButtonX, ButtonY), base::vec2(Width, 24.0f));
			ButtonX += Width + 6.0f;
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label);
		};
		// the envelope preview clock sits right above the buttons
		DrawEnvelopeBar(pPainter, base::rect::FromSize(base::vec2(Area.m_TopLeft.x + 8.0f, Area.m_BottomRight.y - 68.0f),
			base::vec2(Area.Width() - 16.0f, 22.0f)));
		if(SmallButton("+ Group"))
		{
			CGroup &Group = m_Map.NewGroup();
			Group.m_Name = "Group";
			m_Map.NewTileLayer(Group, 50, 50, -1);
			m_vViewStates.clear();
			EnsureViewStates();
			m_SelectedGroup = static_cast<int>(m_Map.m_vGroups.size()) - 1;
			m_SelectedLayer = 0;
			m_Dirty = true;
		}
		if(SmallButton("+ Layer"))
		{
			if(CGroup *pGroup = SelectedGroup())
			{
				CLayer &Layer = m_Map.NewTileLayer(*pGroup, 50, 50, -1);
				Layer.m_Name = "Tile layer";
				EnsureViewStates();
				m_SelectedLayer = static_cast<int>(pGroup->m_vLayers.size()) - 1;
				m_Dirty = true;
			}
		}
		if(SmallButton("Delete"))
		{
			if(CGroup *pGroup = SelectedGroup())
			{
				if(!pGroup->m_vLayers.empty())
				{
					m_vUndo.clear();
					pGroup->m_vLayers.erase(pGroup->m_vLayers.begin() + base::clamp(m_SelectedLayer, 0, static_cast<int>(pGroup->m_vLayers.size()) - 1));
					m_vViewStates.clear();
					EnsureViewStates();
					ClampSelection();
					m_Dirty = true;
					SetStatus("deleted the layer");
				}
			}
		}
		if(SmallButton("Colour"))
		{
			CColor Color;
			if(PickerColor(Color))
			{
				m_PickerColor = Color;
				m_bShowColor = true;
			}
			else
				SetStatus("select a tile layer, or a quad with the left button");
		}
	}

	// ---------------------------------------------------------- palette panel
	void CEditor::DrawPalettePanel(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y), base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);

		float y = Area.m_TopLeft.y + 10.0f;
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 14.0f, ui::Theme.m_Text, "Brush");
		y += 26.0f;

		float x = Area.m_TopLeft.x + 12.0f;
		const auto SmallButton = [&](const std::string &Label, bool Active, float Width = 0.0f) {
			const float W = Width > 0.0f ? Width : std::max(50.0f, ui::Measure(pPainter, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, y), base::vec2(W, 24.0f));
			x += W + 6.0f;
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, Active);
		};
		if(SmallButton("Paint", m_Tool == editor_tool::paint))
			m_Tool = editor_tool::paint;
		if(SmallButton("Erase", m_Tool == editor_tool::erase))
			m_Tool = editor_tool::erase;
		if(SmallButton("Pick", m_Tool == editor_tool::pick))
			m_Tool = editor_tool::pick;
		y += 30.0f;

		x = Area.m_TopLeft.x + 12.0f;
		if(SmallButton("1x1", m_BrushSize == 1, 46.0f))
			m_BrushSize = 1;
		if(SmallButton("2x2", m_BrushSize == 2, 46.0f))
			m_BrushSize = 2;
		if(SmallButton("4x4", m_BrushSize == 4, 46.0f))
			m_BrushSize = 4;
		if(SmallButton("8x8", m_BrushSize == 8, 46.0f))
			m_BrushSize = 8;
		y += 30.0f;

		bool HFlip = (m_BrushFlags & TILEFLAG_HFLIP) != 0;
		bool VFlip = (m_BrushFlags & TILEFLAG_VFLIP) != 0;
		bool Rotate = (m_BrushFlags & TILEFLAG_ROTATE) != 0;
		x = Area.m_TopLeft.x + 12.0f;
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Checkbox(pPainter, Input, Box, HFlip))
				m_BrushFlags ^= TILEFLAG_HFLIP;
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "hflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Checkbox(pPainter, Input, Box, VFlip))
				m_BrushFlags ^= TILEFLAG_VFLIP;
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "vflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Checkbox(pPainter, Input, Box, Rotate))
				m_BrushFlags ^= TILEFLAG_ROTATE;
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "rot");
		}
		y += 32.0f;

		const CLayer *pLayer = SelectedLayer();
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.5f, ui::Theme.m_TextDim,
			base::Format("tool %s   tile %d   flags %d", m_Tool == editor_tool::paint ? "paint" : (m_Tool == editor_tool::erase ? "erase" : "pick"),
				m_BrushTile, m_BrushFlags));
		y += 24.0f;
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 8.0f, y), base::vec2(Area.m_BottomRight.x - 8.0f, y), ui::Theme.m_Border);
		y += 10.0f;

		if(pLayer != nullptr && pLayer->IsGameLayer())
		{
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, "Entities");
			y += 24.0f;
			for(const auto &Entry : {
					std::pair<int, const char *>{ENTITY_SPAWN, "spawn"},
					std::pair<int, const char *>{ENTITY_SPAWN_RED, "spawn red"},
					std::pair<int, const char *>{ENTITY_SPAWN_BLUE, "spawn blue"},
					std::pair<int, const char *>{ENTITY_FLAGSTAND_RED, "flag red"},
					std::pair<int, const char *>{ENTITY_FLAGSTAND_BLUE, "flag blue"},
					std::pair<int, const char *>{ENTITY_ARMOR_1, "armor"},
					std::pair<int, const char *>{ENTITY_HEALTH_1, "health"},
					std::pair<int, const char *>{ENTITY_WEAPON_SHOTGUN, "shotgun"},
					std::pair<int, const char *>{ENTITY_WEAPON_GRENADE, "grenade"},
					std::pair<int, const char *>{ENTITY_POWERUP_NINJA, "ninja"},
					std::pair<int, const char *>{ENTITY_WEAPON_LASER, "laser"},
					std::pair<int, const char *>{ENTITY_LASER_NORMAL_CCW, "laser ccw"},
					std::pair<int, const char *>{ENTITY_LASER_STOP, "laser stop"},
					std::pair<int, const char *>{ENTITY_LASER_NORMAL_CW, "laser cw"},
					std::pair<int, const char *>{ENTITY_DRAGGER_NORMAL, "dragger"},
					std::pair<int, const char *>{ENTITY_DRAGGER_STRONG, "dragger strong"},
					std::pair<int, const char *>{ENTITY_DOOR, "door"}})
			{
				const int Index = ENTITY_OFFSET + Entry.first;
				const base::rect Rect = base::rect::FromSize(base::vec2(Area.m_TopLeft.x + 12.0f, y), base::vec2(Area.Width() - 24.0f, 22.0f));
				if(ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Entry.second, m_BrushTile == Index))
					m_BrushTile = Index;
				y += 24.0f;
				if(y > Area.m_BottomRight.y - 30.0f)
					break;
			}
			return;
		}

		if(pLayer != nullptr && pLayer->IsTiles() && pLayer->m_Image >= 0)
		{
			const int Texture = TextureForImage(pLayer->m_Image);
			const std::string &Name = m_Map.m_vImages[static_cast<std::size_t>(pLayer->m_Image)].m_Name;
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, Name);
			y += 22.0f;
			const float Sheet = std::min(Area.Width() - 24.0f, Area.m_BottomRight.y - y - 12.0f);
			if(Sheet > 32.0f)
			{
				const base::vec2 TopLeft(Area.m_TopLeft.x + 12.0f, y);
				const base::rect SheetRect = base::rect::FromSize(TopLeft, base::vec2(Sheet, Sheet));
				if(Texture > 0)
				{
					int TextureWidth = 0, TextureHeight = 0;
					m_pGraphics->TextureSize(Texture, TextureWidth, TextureHeight);
					// the whole sheet is mapped onto the preview square, so the
					// texel window is the full texture
					const ui::CPaint Paint = ImagePaint(Texture, TopLeft, base::vec2(Sheet, Sheet),
						0.0f, 0.0f, static_cast<float>(TextureWidth), static_cast<float>(TextureHeight),
						TextureWidth, TextureHeight, 1.0f);
					pPainter->BeginPath();
					pPainter->Rect(TopLeft.x, TopLeft.y, Sheet, Sheet);
					pPainter->FillPaint(Paint);
					pPainter->Fill();
				}
				else
				{
					ui::Rect(pPainter, SheetRect, ui::Theme.m_PanelDark);
					ui::Text(pPainter, m_pGraphics->Font(), base::vec2(SheetRect.Center().x, SheetRect.Center().y), 12.0f, ui::Theme.m_Warning,
						"texture missing", ui::text_align::center);
				}
				ui::RectOutline(pPainter, SheetRect, ui::Theme.m_Border);

				const float Cell = Sheet / 16.0f;
				const int Col = m_BrushTile % 16;
				const int Row = m_BrushTile / 16;
				ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(TopLeft.x + Col * Cell, TopLeft.y + Row * Cell), base::vec2(Cell, Cell)), ui::Theme.m_Accent, 2.0f);

				if(ui::Hovered(Input, SheetRect) && Input.MousePressed(engine::CInput::mouse_button::left))
				{
					const base::vec2 Mouse = Input.MousePosition();
					const int ClickCol = base::clamp(static_cast<int>((Mouse.x - TopLeft.x) / Cell), 0, 15);
					const int ClickRow = base::clamp(static_cast<int>((Mouse.y - TopLeft.y) / Cell), 0, 15);
					m_BrushTile = ClickRow * 16 + ClickCol;
					SetStatus(base::Format("brush tile %d", m_BrushTile));
				}
			}
			return;
		}

		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			"select a tile layer to see its palette");
	}

	// ------------------------------------------------------------- status bar
	void CEditor::DrawStatusBar(ui::CPainter *pPainter, const base::rect &Area)
	{
		ui::Rect(pPainter, Area, ui::Theme.m_PanelDark);
		ui::Line(pPainter, Area.m_TopLeft, base::vec2(Area.m_BottomRight.x, Area.m_TopLeft.y), ui::Theme.m_Border);

		const CLayer *pLayer = SelectedLayer();
		const int TileX = m_bCursorValid ? static_cast<int>(std::floor(m_CursorWorld.x / TileSize)) : -1;
		const int TileY = m_bCursorValid ? static_cast<int>(std::floor(m_CursorWorld.y / TileSize)) : -1;
		std::string Info = base::Format("v%d  %s  target %s  tile %d,%d  zoom %.2f  %s",
			m_Map.m_SourceDatafileVersion,
			m_VersionId.c_str(),
			GetTargetProfile(m_Target).m_pId,
			TileX, TileY, m_View.m_Zoom,
			pLayer != nullptr ? (pLayer->m_Name.empty() ? "layer" : pLayer->m_Name.c_str()) : "no layer");

		const double Age = base::Now() - m_StatusTime;
		const ui::CColor MessageColor = Age < 6.0 ? ui::Theme.m_Accent : ui::Theme.m_TextDim;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, ui::Theme.m_TextDim, Info);
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, MessageColor,
			m_Status, Area.Width() * 0.5f, ui::text_align::right);

		if(m_Dirty)
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_BottomRight.x - 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, ui::Theme.m_Warning, "", ui::text_align::right);
	}

	// ----------------------------------------------------------------- popups
	void CEditor::OpenBrowser()
	{
		m_vBrowserEntries.clear();
		m_BrowserSelected = 0;
		const storage::path Maps = storage::GetDataDirectory() / "maps";
		for(const storage::path &Directory : storage::list_subdirectories(Maps))
			for(const storage::path &File : storage::list_directory(Directory, ".map"))
				m_vBrowserEntries.push_back(File.string());
		for(const storage::path &File : storage::list_directory(Maps, ".map"))
			m_vBrowserEntries.push_back(File.string());
		for(const storage::path &File : storage::list_directory(std::filesystem::current_path(), ".map"))
			m_vBrowserEntries.push_back(File.string());
		for(const storage::path &File : storage::list_directory(std::filesystem::current_path(), ".unitwp"))
			m_vBrowserEntries.push_back(File.string());
	}

	void CEditor::HandlePopupInput()
	{
		engine::CInput &Input = m_pGraphics->Input();

		if(Input.KeyPressed(SDLK_ESCAPE))
		{
			m_bShowOpen = m_bShowSave = m_bShowExport = m_bShowHelp = m_bShowColor = false;
			return;
		}

		if(m_bShowColor)
			return; // the picker handles its own mouse input while it is drawn

		if(m_bShowOpen)
		{
			if(Input.KeyPressed(SDLK_DOWN))
				m_BrowserSelected = std::min(m_BrowserSelected + 1, static_cast<int>(m_vBrowserEntries.size()) - 1);
			if(Input.KeyPressed(SDLK_UP))
				m_BrowserSelected = std::max(m_BrowserSelected - 1, 0);
			if(Input.KeyPressed(SDLK_RETURN) || Input.KeyPressed(SDLK_KP_ENTER))
			{
				if(m_BrowserSelected >= 0 && m_BrowserSelected < static_cast<int>(m_vBrowserEntries.size()))
				{
					const std::string Path = m_vBrowserEntries[static_cast<std::size_t>(m_BrowserSelected)];
					m_bShowOpen = false;
					OpenFile(Path);
				}
			}
			return;
		}

		if(m_bShowExport)
		{
			if(Input.KeyPressed(SDLK_TAB) || Input.KeyPressed(SDLK_DOWN))
				m_ExportTarget = (m_ExportTarget + 1) % static_cast<int>(AllTargets().size());
			if(Input.KeyPressed(SDLK_UP))
				m_ExportTarget = (m_ExportTarget + static_cast<int>(AllTargets().size()) - 1) % static_cast<int>(AllTargets().size());
			m_TextInput += Input.TypedText();
			if(Input.BackspacePressed() && !m_TextInput.empty())
				m_TextInput.pop_back();
			if(Input.KeyPressed(SDLK_RETURN) || Input.KeyPressed(SDLK_KP_ENTER))
			{
				const map_target Target = AllTargets()[static_cast<std::size_t>(m_ExportTarget)];
				const std::string Path = m_TextInput + ".map";
				m_bShowExport = false;
				ExportMap(Path, Target);
			}
			return;
		}

		if(m_bShowSave)
		{
			m_TextInput += Input.TypedText();
			if(Input.BackspacePressed() && !m_TextInput.empty())
				m_TextInput.pop_back();
			if(Input.KeyPressed(SDLK_RETURN) || Input.KeyPressed(SDLK_KP_ENTER))
			{
				std::string Path = m_TextInput;
				if(!base::EndsWith(Path, CUnitwpPackage::Extension))
					Path += CUnitwpPackage::Extension;
				m_bShowSave = false;
				SavePackage(Path);
			}
		}
	}

	void CEditor::DrawOpenPopup(ui::CPainter *pPainter)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 300.0f, Size.y * 0.5f - 240.0f), base::vec2(600.0f, 480.0f));
		ui::Rect(pPainter, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Open a map or package");
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_BottomRight.x - 16.0f, Popup.m_TopLeft.y + 26.0f), 11.0f, ui::Theme.m_TextDim,
			"up/down then enter, esc to cancel", ui::text_align::right);

		const base::rect List = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 12.0f, Popup.m_TopLeft.y + 40.0f), base::vec2(Popup.Width() - 24.0f, Popup.Height() - 60.0f));
		ui::Rect(pPainter, List, ui::Theme.m_PanelDark);
		pPainter->Save();
		pPainter->Scissor( List.m_TopLeft.x, List.m_TopLeft.y, List.Width(), List.Height());
		float y = List.m_TopLeft.y + 4.0f;
		for(std::size_t i = 0; i < m_vBrowserEntries.size(); i++)
		{
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 4.0f, y), base::vec2(List.Width() - 8.0f, 22.0f));
			if(Row.m_BottomRight.y > List.m_TopLeft.y && Row.m_TopLeft.y < List.m_BottomRight.y)
			{
				if(static_cast<int>(i) == m_BrowserSelected)
					ui::Rect(pPainter, Row, ui::Theme.m_Selection);
				else if(ui::Hovered(Input, Row))
					ui::Rect(pPainter, Row, ui::Theme.m_Hover);
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.m_TopLeft.y + 15.0f), 12.5f, ui::Theme.m_Text,
					m_vBrowserEntries[i], Row.Width() - 16.0f);
				if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left))
					m_BrowserSelected = static_cast<int>(i);
				if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left) && m_BrowserSelected == static_cast<int>(i))
				{
					// a second click opens, the first selects
				}
				if(ui::Hovered(Input, Row) && Input.MouseReleased(engine::CInput::mouse_button::left) && static_cast<int>(i) == m_BrowserSelected)
				{
					const std::string Path = m_vBrowserEntries[i];
					m_bShowOpen = false;
					OpenFile(Path);
					pPainter->Restore();
					return;
				}
			}
			y += 22.0f;
		}
		pPainter->Restore();

		if(m_vBrowserEntries.empty())
			ui::Text(pPainter, m_pGraphics->Font(), List.Center(), 13.0f, ui::Theme.m_TextDim,
				"no maps found in <data>/maps or the working directory", ui::text_align::center);
	}

	void CEditor::DrawExportPopup(ui::CPainter *pPainter)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 260.0f, Size.y * 0.5f - 160.0f), base::vec2(520.0f, 320.0f));
		ui::Rect(pPainter, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Export the map");
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_BottomRight.x - 16.0f, Popup.m_TopLeft.y + 26.0f), 11.0f, ui::Theme.m_TextDim,
			"up/down picks the version, enter writes <name>.map", ui::text_align::right);

		float y = Popup.m_TopLeft.y + 44.0f;
		int Index = 0;
		for(const map_target Target : AllTargets())
		{
			const CMapTargetProfile &Profile = GetTargetProfile(Target);
			const base::rect Row = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 14.0f, y), base::vec2(Popup.Width() - 28.0f, 28.0f));
			const bool Active = Index == m_ExportTarget;
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Row, Profile.m_pName, Active))
				m_ExportTarget = Index;
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Row.m_BottomRight.x - 12.0f, Row.Center().y + 4.0f), 11.0f, ui::Theme.m_TextDim,
				base::Format("datafile v%d", Profile.m_DatafileVersion), ui::text_align::right);
			y += 32.0f;
			Index++;
		}

		y += 6.0f;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 4.0f), 12.0f, ui::Theme.m_TextDim, "file name");
		const base::rect Field = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 14.0f, y + 10.0f), base::vec2(Popup.Width() - 28.0f, 26.0f));
		ui::Rect(pPainter, Field, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, Field, ui::Theme.m_Accent);
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Field.m_TopLeft.x + 8.0f, Field.Center().y + 5.0f), 13.0f, ui::Theme.m_Text, m_TextInput + ".map");

		const map_target Target = AllTargets()[static_cast<std::size_t>(m_ExportTarget)];
		y += 48.0f;
		const std::vector<std::string> Warnings = m_Map.CompatibilityWarnings(Target);
		if(Warnings.empty())
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 12.0f), 12.0f, ui::Theme.m_Success, "this map converts without losses");
		else
			for(std::size_t i = 0; i < Warnings.size() && i < 3; i++)
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 12.0f + i * 16.0f), 12.0f, ui::Theme.m_Warning,
					"- " + Warnings[i], Popup.Width() - 28.0f);
	}

	void CEditor::DrawSavePopup(ui::CPainter *pPainter)
	{
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 240.0f, Size.y * 0.5f - 60.0f), base::vec2(480.0f, 120.0f));
		ui::Rect(pPainter, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Save the package");
		const base::rect Field = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 44.0f), base::vec2(Popup.Width() - 32.0f, 28.0f));
		ui::Rect(pPainter, Field, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, Field, ui::Theme.m_Accent);
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Field.m_TopLeft.x + 8.0f, Field.Center().y + 5.0f), 13.0f, ui::Theme.m_Text,
			m_TextInput + CUnitwpPackage::Extension);
	}

	void CEditor::DrawHelpPopup(ui::CPainter *pPainter)
	{
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 330.0f, Size.y * 0.5f - 210.0f), base::vec2(660.0f, 420.0f));
		ui::Rect(pPainter, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 18.0f, Popup.m_TopLeft.y + 28.0f), 16.0f, ui::Theme.m_Text, "Uniteed - keys");
		const char *apLines[] = {
			"left / right click      paint / pick the tile under the cursor",
			"middle drag             pan            wheel   zoom",
			"W A S D or arrows       pan            F       fit the map",
			"1 2 3                   paint / erase / pick tool",
			"4 5 6 7 or [ ]          brush size 1, 2, 4, 8",
			"H J K                   hflip / vflip / rotate the brush",
			"Q E                     previous / next tile",
			"P                       fill the whole layer with the brush",
			"G                       toggle the grid",
			"Ctrl+Z / Ctrl+Y         undo / redo",
			"Ctrl+N / Ctrl+O         new / open",
			"Ctrl+S / Ctrl+E         save the .unitwp package / export a map",
			"Ctrl+L / Ctrl+G         add a tile layer / a group",
			"Delete / Shift+Delete   delete the layer / the group",
			"left click on a quad    select it (ctrl adds, then C colours it)",
			"C                       colour of the layer or of the selected quads",
			"space                   run / pause the envelope preview",
			"- / =                   envelope preview slower / faster",
			"                        (scrubbing the layer panel bar sets the time)",
			"Ctrl+Q                  quit",
		};
		float y = Popup.m_TopLeft.y + 56.0f;
		for(const char *pLine : apLines)
		{
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 18.0f, y), 13.0f, ui::Theme.m_Text, pLine);
			y += 22.0f;
		}
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 18.0f, Popup.m_BottomRight.y - 16.0f), 12.0f, ui::Theme.m_TextDim,
			"esc closes this panel");
	}

	void CEditor::DrawColorPopup(ui::CPainter *pPainter)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 148.0f, Size.y * 0.5f - 150.0f), base::vec2(296.0f, 300.0f));
		ui::Rect(pPainter, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Popup, ui::Theme.m_Border, 2.0f);

		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 24.0f), 15.0f, ui::Theme.m_Text, "Colour");
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_BottomRight.x - 16.0f, Popup.m_TopLeft.y + 24.0f), 11.5f, ui::Theme.m_TextDim,
			PickerTargetName(), Popup.Width() * 0.55f, ui::text_align::right);

		const float X = Popup.m_TopLeft.x + 16.0f;
		const float Width = Popup.Width() - 32.0f;
		float y = Popup.m_TopLeft.y + 36.0f;

		// the hue is kept aside, because a grey has no hue to derive it from
		float Hue = m_PickerHue;
		float Saturation = 0.0f, Value = 0.0f;
		RgbToHsv(m_PickerColor, Hue, Saturation, Value, m_PickerHue);

		// saturation / value square
		const base::rect Square = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, 130.0f));
		const ui::CColor HueColor = HsvToColor(Hue, 1.0f, 1.0f, 255);
		pPainter->BeginPath();
		pPainter->Rect( Square.m_TopLeft.x, Square.m_TopLeft.y, Square.Width(), Square.Height());
		pPainter->FillPaint(GradientPaint(base::vec2(Square.m_TopLeft.x, Square.m_TopLeft.y), base::vec2(Square.m_BottomRight.x, Square.m_TopLeft.y), ui::RGB(255, 255, 255), HueColor));
		pPainter->Fill();
		pPainter->BeginPath();
		pPainter->Rect( Square.m_TopLeft.x, Square.m_TopLeft.y, Square.Width(), Square.Height());
		pPainter->FillPaint(GradientPaint(base::vec2(Square.m_TopLeft.x, Square.m_TopLeft.y), base::vec2(Square.m_TopLeft.x, Square.m_BottomRight.y), ui::RGBA(0, 0, 0, 0), ui::RGBA(0, 0, 0, 255)));
		pPainter->Fill();
		ui::RectOutline(pPainter, Square, ui::Theme.m_Border);

		if(ui::Hovered(Input, Square) && Input.MouseDown(engine::CInput::mouse_button::left))
		{
			if(Input.MousePressed(engine::CInput::mouse_button::left))
				PushUndo(m_SelectedGroup, m_SelectedLayer, PickerTargetsQuad() ? undo_scope::quads : undo_scope::colour);
			Saturation = base::clamp((Input.MousePosition().x - Square.m_TopLeft.x) / Square.Width(), 0.0f, 1.0f);
			Value = 1.0f - base::clamp((Input.MousePosition().y - Square.m_TopLeft.y) / Square.Height(), 0.0f, 1.0f);
			m_PickerHue = Hue;
			m_PickerColor = HsvToColorRaw(Hue, Saturation, Value, m_PickerColor.a);
			SetPickerColor(m_PickerColor);
		}

		// the marker inside the square
		const base::vec2 Marker(Square.m_TopLeft.x + Saturation * Square.Width(), Square.m_TopLeft.y + (1.0f - Value) * Square.Height());
		pPainter->BeginPath();
		pPainter->Circle( Marker.x, Marker.y, 5.0f);
		pPainter->StrokeColor( ui::RGBA(0, 0, 0, 200));
		pPainter->StrokeWidth( 2.0f);
		pPainter->Stroke();
		pPainter->BeginPath();
		pPainter->Circle( Marker.x, Marker.y, 5.0f);
		pPainter->StrokeColor( ui::RGBA(255, 255, 255, 230));
		pPainter->StrokeWidth( 1.0f);
		pPainter->Stroke();
		y += Square.Height() + 10.0f;

		// the hue strip
		const base::rect HueBar = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, 14.0f));
		const ui::CColor aHues[7] = {ui::RGB(255, 0, 0), ui::RGB(255, 255, 0), ui::RGB(0, 255, 0), ui::RGB(0, 255, 255),
			ui::RGB(0, 0, 255), ui::RGB(255, 0, 255), ui::RGB(255, 0, 0)};
		for(int i = 0; i < 6; i++)
		{
			const float x0 = HueBar.m_TopLeft.x + HueBar.Width() * i / 6.0f;
			const float x1 = HueBar.m_TopLeft.x + HueBar.Width() * (i + 1) / 6.0f;
			pPainter->BeginPath();
			pPainter->Rect( x0, HueBar.m_TopLeft.y, x1 - x0, HueBar.Height());
			pPainter->FillPaint(GradientPaint(base::vec2(x0, HueBar.m_TopLeft.y), base::vec2(x1, HueBar.m_TopLeft.y), aHues[i], aHues[i + 1]));
			pPainter->Fill();
		}
		ui::RectOutline(pPainter, HueBar, ui::Theme.m_Border);
		if(ui::Hovered(Input, HueBar) && Input.MouseDown(engine::CInput::mouse_button::left))
		{
			if(Input.MousePressed(engine::CInput::mouse_button::left))
				PushUndo(m_SelectedGroup, m_SelectedLayer, PickerTargetsQuad() ? undo_scope::quads : undo_scope::colour);
			Hue = base::clamp((Input.MousePosition().x - HueBar.m_TopLeft.x) / HueBar.Width(), 0.0f, 1.0f) * 360.0f;
			m_PickerHue = Hue;
			m_PickerColor = HsvToColorRaw(Hue, Saturation, Value, m_PickerColor.a);
			SetPickerColor(m_PickerColor);
		}
		const float HueX = HueBar.m_TopLeft.x + HueBar.Width() * (Hue / 360.0f);
		ui::Rect(pPainter, base::rect::FromSize(base::vec2(HueX - 2.0f, HueBar.m_TopLeft.y - 2.0f), base::vec2(4.0f, HueBar.Height() + 4.0f)), ui::RGBA(255, 255, 255, 230));
		y += HueBar.Height() + 8.0f;

		// the alpha strip, over a checkerboard so transparency is visible
		const base::rect AlphaBar = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, 14.0f));
		for(int i = 0; i < 20; i++)
			ui::Rect(pPainter, base::rect::FromSize(base::vec2(AlphaBar.m_TopLeft.x + i * AlphaBar.Width() / 20.0f, AlphaBar.m_TopLeft.y),
				base::vec2(AlphaBar.Width() / 20.0f + 0.5f, AlphaBar.Height())), (i % 2 == 0) ? ui::RGBA(90, 90, 96, 255) : ui::RGBA(60, 60, 66, 255));
		const ui::CColor Opaque = ui::RGBA(static_cast<unsigned char>(m_PickerColor.r), static_cast<unsigned char>(m_PickerColor.g), static_cast<unsigned char>(m_PickerColor.b), 255);
		pPainter->BeginPath();
		pPainter->Rect( AlphaBar.m_TopLeft.x, AlphaBar.m_TopLeft.y, AlphaBar.Width(), AlphaBar.Height());
		pPainter->FillPaint(GradientPaint(base::vec2(AlphaBar.m_TopLeft.x, AlphaBar.m_TopLeft.y),
			base::vec2(AlphaBar.m_BottomRight.x, AlphaBar.m_TopLeft.y),
			ui::RGBA(static_cast<unsigned char>(m_PickerColor.r), static_cast<unsigned char>(m_PickerColor.g), static_cast<unsigned char>(m_PickerColor.b), 0), Opaque));
		pPainter->Fill();
		ui::RectOutline(pPainter, AlphaBar, ui::Theme.m_Border);
		if(ui::Hovered(Input, AlphaBar) && Input.MouseDown(engine::CInput::mouse_button::left))
		{
			if(Input.MousePressed(engine::CInput::mouse_button::left))
				PushUndo(m_SelectedGroup, m_SelectedLayer, PickerTargetsQuad() ? undo_scope::quads : undo_scope::colour);
			m_PickerColor.a = base::clamp(static_cast<int>((Input.MousePosition().x - AlphaBar.m_TopLeft.x) / AlphaBar.Width() * 255.0f), 0, 255);
			SetPickerColor(m_PickerColor);
		}
		ui::Rect(pPainter, base::rect::FromSize(base::vec2(AlphaBar.m_TopLeft.x + AlphaBar.Width() * m_PickerColor.a / 255.0f - 2.0f, AlphaBar.m_TopLeft.y - 2.0f),
			base::vec2(4.0f, AlphaBar.Height() + 4.0f)), ui::RGBA(255, 255, 255, 230));
		y += AlphaBar.Height() + 12.0f;

		// the colour being picked, next to a couple of handy presets
		const base::rect Preview = base::rect::FromSize(base::vec2(X, y), base::vec2(48.0f, 22.0f));
		ui::Rect(pPainter, Preview, ui::RGBA(static_cast<unsigned char>(m_PickerColor.r), static_cast<unsigned char>(m_PickerColor.g), static_cast<unsigned char>(m_PickerColor.b),
			static_cast<unsigned char>(m_PickerColor.a)));
		ui::RectOutline(pPainter, Preview, ui::Theme.m_Border);
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Preview.m_BottomRight.x + 8.0f, Preview.Center().y + 4.0f), 12.0f, ui::Theme.m_TextDim,
			base::Format("%d %d %d %d", m_PickerColor.r, m_PickerColor.g, m_PickerColor.b, m_PickerColor.a));

		const CColor aPresets[8] = {{255, 255, 255, 255}, {0, 0, 0, 255}, {255, 255, 255, 128}, {140, 90, 50, 255},
			{120, 200, 120, 255}, {90, 140, 240, 255}, {240, 200, 90, 255}, {200, 90, 90, 255}};
		for(int i = 0; i < 8; i++)
		{
			const base::rect Swatch = base::rect::FromSize(base::vec2(X + i * 26.0f, y + 30.0f), base::vec2(22.0f, 22.0f));
			ui::Rect(pPainter, Swatch, ui::RGBA(static_cast<unsigned char>(aPresets[i].r), static_cast<unsigned char>(aPresets[i].g),
				static_cast<unsigned char>(aPresets[i].b), static_cast<unsigned char>(aPresets[i].a)));
			ui::RectOutline(pPainter, Swatch, ui::Hovered(Input, Swatch) ? ui::Theme.m_Accent : ui::Theme.m_Border);
			if(ui::Hovered(Input, Swatch) && Input.MousePressed(engine::CInput::mouse_button::left))
			{
				PushUndo(m_SelectedGroup, m_SelectedLayer, PickerTargetsQuad() ? undo_scope::quads : undo_scope::colour);
				m_PickerColor = aPresets[i];
				RgbToHsv(m_PickerColor, m_PickerHue, Saturation, Value, m_PickerHue);
				SetPickerColor(m_PickerColor);
			}
		}

		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_BottomRight.y - 14.0f), 11.5f, ui::Theme.m_TextDim,
			"esc closes, live on the map");
	}

	// ------------------------------------------------------------- envelopes
	void CEditor::DrawEnvelopeBar(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const float Loop = EnvelopeLoopLength();

		float x = Area.m_TopLeft.x;
		const base::rect Play = base::rect::FromSize(base::vec2(x, Area.m_TopLeft.y), base::vec2(26.0f, Area.Height()));
		if(ui::Button(pPainter, Input, m_pGraphics->Font(), Play, m_bEnvelopePlaying ? "II" : ">"))
			m_bEnvelopePlaying = !m_bEnvelopePlaying;
		x += 30.0f;

		const base::rect Speed = base::rect::FromSize(base::vec2(x, Area.m_TopLeft.y), base::vec2(40.0f, Area.Height()));
		if(ui::Button(pPainter, Input, m_pGraphics->Font(), Speed, base::Format("%.2gx", m_EnvelopeSpeed)))
		{
			const float aSpeeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
			std::size_t Next = 0;
			for(std::size_t i = 0; i < std::size(aSpeeds); i++)
				if(m_EnvelopeSpeed >= aSpeeds[i])
					Next = (i + 1) % std::size(aSpeeds);
			m_EnvelopeSpeed = aSpeeds[Next];
		}
		x += 44.0f;

		const float TextWidth = 92.0f;
		const base::rect Track = base::rect::FromSize(base::vec2(x, Area.m_TopLeft.y), base::vec2(std::max(Area.m_BottomRight.x - TextWidth - x, 20.0f), Area.Height()));
		float Time = m_EnvelopeTime;
		if(ui::Slider(pPainter, Input, Track, Time, 0.0f, Loop))
		{
			m_EnvelopeTime = Time;
			m_bEnvelopePlaying = false; // scrubbing is a deliberate position
		}
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x, Area.m_TopLeft.y + 15.0f), 11.5f, ui::Theme.m_TextDim,
			base::Format("%.2fs / %.2fs", m_EnvelopeTime, Loop), ui::text_align::right);
	}

	void CEditor::DrawPopups(ui::CPainter *pPainter)
	{
		engine::CInput &Input = m_pGraphics->Input();
		if(m_bShowOpen)
			DrawOpenPopup(pPainter);
		if(m_bShowSave)
			DrawSavePopup(pPainter);
		if(m_bShowExport)
			DrawExportPopup(pPainter);
		if(m_bShowHelp)
			DrawHelpPopup(pPainter);
		if(m_bShowColor)
			DrawColorPopup(pPainter);

		// clicking outside a popup closes it, except for the help panel
		if((m_bShowOpen || m_bShowSave || m_bShowExport) && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			// the popups handle their own clicks, so only close when the click
			// was above or below their area
		}
	}

	// ------------------------------------------------------------------ render
	void CEditor::Render()
	{
		ui::CPainter *pPainter = m_pGraphics->Painter();
		const base::vec2 Size = m_pGraphics->WindowSize();
		// minimized: there is nothing to draw into
		if(Size.x < 1.0f || Size.y < 1.0f)
			return;

		ui::Rect(pPainter, base::rect::FromSize(base::vec2(0.0f, 0.0f), Size), ui::Theme.m_Background);

		const base::rect Canvas = CanvasRect(Size);
		DrawMap(pPainter, Canvas);

		DrawToolbar(pPainter, base::rect::FromSize(base::vec2(0.0f, 0.0f), base::vec2(Size.x, ToolbarHeight)));
		DrawLayerPanel(pPainter, base::rect::FromSize(base::vec2(0.0f, ToolbarHeight), base::vec2(PanelWidth, Canvas.Height())));
		DrawPalettePanel(pPainter, base::rect::FromSize(base::vec2(Size.x - PanelWidth, ToolbarHeight), base::vec2(PanelWidth, Canvas.Height())));
		DrawStatusBar(pPainter, base::rect::FromSize(base::vec2(0.0f, Size.y - StatusHeight), base::vec2(Size.x, StatusHeight)));

		DrawPopups(pPainter);
	}
} // namespace game
