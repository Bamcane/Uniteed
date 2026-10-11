/*
	All the drawing of the editor, in one immediate mode pass: the map under the
	camera, then the panels on top of it.
*/

#include <algorithm>
#include <array>
#include <cstring>

#include "engine/keycodes.h"

#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/graphics.h"
#include "engine/ui.h"
#include "game/assets.h"
#include "game/editor/editor.h"
#include "game/editor/editor_ui.h"

namespace game
{
	namespace ui = engine::ui;
	// the shared drawing helpers (editor_ui.h), used unqualified the way they
	// were when they lived in this file
	using editor_ui::ImagePaint;

	namespace
	{
		constexpr float TileSize = 32.0f;
		constexpr float RowHeight = 22.0f;
		/*
			The layer panel's action row can use two lines, because that docker
			is meant to work in a narrow column. The clock above it and the list
			below the header are both laid out against that worst case, so
			nothing moves when the row wraps.
		*/
		constexpr float DockActionRows = 2.0f;
		constexpr float DockActionRowHeight = 28.0f;

		// the quad handles: a fixed screen size, because a point has to stay
		// grabbable at every zoom level
		constexpr float HandleGrabRadius = 9.0f;
		constexpr float HandleSize = 9.0f;
		constexpr float PivotSize = 11.0f;
		constexpr float HandleBorder = 1.5f;

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

		/*
			An editor sprite sheet is a 16 by 16 grid of 64 pixel cells, and a
			cell is addressed the way the reference client addresses it: the
			index's column and row. Entity indices and physics payload types use
			the same numbering, which is why one helper covers both.
		*/
		ui::CPaint SpritePaint(int Texture, int Index, float X, float Y, float Size, int TextureWidth, int TextureHeight, float Alpha)
		{
			const int Col = Index % 16;
			const int Row = Index / 16;
			const float Cell = static_cast<float>(TextureWidth) / 16.0f;
			return ImagePaint(Texture, base::vec2(X, Y), base::vec2(Size, Size),
				static_cast<float>(Col) * Cell, static_cast<float>(Row) * Cell,
				Cell, Cell, TextureWidth, TextureHeight, Alpha);
		}

		/*
			Draws a 16 by 16 sprite sheet into a square: the mapres sheet of a
			tile layer, the entity sheet of a game layer, the sheet of a physics
			layer. The whole texture is mapped onto the square, which is what
			makes a cell exactly a sixteenth of it, and a sheet that is not
			installed says so instead of drawing nothing.
		*/
		void DrawSpriteSheet(ui::CPainter *pPainter, int Font, int Texture, const base::rect &Sheet,
			int TextureWidth, int TextureHeight)
		{
			ui::Rect(pPainter, Sheet, ui::Theme.m_PanelDark);
			if(Texture > 0 && TextureWidth > 0 && TextureHeight > 0)
			{
				const ui::CPaint Paint = ImagePaint(Texture, Sheet.m_TopLeft,
					base::vec2(Sheet.Width(), Sheet.Height()), 0.0f, 0.0f,
					static_cast<float>(TextureWidth), static_cast<float>(TextureHeight),
					TextureWidth, TextureHeight, 1.0f);
				pPainter->BeginPath();
				pPainter->Rect(Sheet.m_TopLeft.x, Sheet.m_TopLeft.y, Sheet.Width(), Sheet.Height());
				pPainter->FillPaint(Paint);
				pPainter->Fill();
			}
			else
			{
				ui::Text(pPainter, Font, Sheet.Center(), 12.0f, ui::Theme.m_Warning, "texture missing",
					ui::text_align::center);
			}
			ui::RectOutline(pPainter, Sheet, ui::Theme.m_Border);
		}

		/*
			One cell of an editor sprite sheet: the entity sheet of a game
			layer, the sheet of a physics layer. The index is the cell itself -
			the column and the row the reference client reads out of the sheet -
			so what the editor draws is the picture the client will draw, for an
			entity and for a tile controller alike. Alpha fades it, which is what
			the brush ghost over the map wants.
		*/
		void DrawSprite(ui::CPainter *pPainter, int Texture, int Index, float X, float Y, float Size, float Alpha = 1.0f)
		{
			if(Texture <= 0 || Size <= 0.0f || Alpha <= 0.0f || Index < 0)
				return;
			// the sheet is 1024 square, which is what the 16 by 16 grid needs
			const ui::CPaint Paint = SpritePaint(Texture, Index, X, Y, Size, 1024, 1024, Alpha);
			pPainter->BeginPath();
			pPainter->Rect(X, Y, Size, Size);
			pPainter->FillPaint(Paint);
			pPainter->Fill();
		}

		/*
			The name field of a row in the layer tree: what has been typed so
			far, with a caret behind it, drawn where the row's label would be so
			the row does not move while it is being renamed.
		*/
		void DrawNameField(ui::CPainter *pPainter, int Font, const base::rect &Row, float Left, const std::string &Text,
			bool bAllSelected)
		{
			const base::rect Field = base::rect::FromSize(base::vec2(Row.m_TopLeft.x + Left, Row.m_TopLeft.y + 2.0f),
				base::vec2(std::max(Row.Width() - Left - 6.0f, 20.0f), Row.Height() - 4.0f));
			ui::Rect(pPainter, Field, ui::Theme.m_PanelDark);
			ui::RectOutline(pPainter, Field, ui::Theme.m_Accent);
			// the whole name is selected until something is typed over it
			if(bAllSelected && !Text.empty())
			{
				const float Width = std::min(ui::Measure(pPainter, Font, 12.0f, Text) + 2.0f, Field.Width() - 12.0f);
				ui::Rect(pPainter, base::rect::FromSize(base::vec2(Field.m_TopLeft.x + 5.0f, Field.m_TopLeft.y + 4.0f),
					base::vec2(Width, Field.Height() - 8.0f)), ui::Theme.m_Selection);
			}
			ui::TextClipped(pPainter, Font, base::vec2(Field.m_TopLeft.x + 5.0f, Field.Center().y + 4.5f), 12.0f,
				ui::Theme.m_Text, Text, Field.Width() - 14.0f);
			// the caret follows the text and stays inside the field when the
			// name is wider than it
			const float CaretX = std::min(Field.m_TopLeft.x + 6.0f + ui::Measure(pPainter, Font, 12.0f, Text),
				Field.m_BottomRight.x - 4.0f);
			if(!bAllSelected)
				ui::Line(pPainter, base::vec2(CaretX, Field.m_TopLeft.y + 4.0f),
					base::vec2(CaretX, Field.m_BottomRight.y - 4.0f), ui::Theme.m_Accent);
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

		// ------------------------------------------------------------ handles
		// One point of a quad: a filled square in the colour the corner carries,
		// so the four colours of a gradient are visible on the map itself.
		void DrawHandle(ui::CPainter *pPainter, base::vec2 Center, ui::CColor Color, bool bActive, bool bDragging)
		{
			const base::rect Box = base::rect::FromSize(Center - base::vec2(HandleSize, HandleSize) * 0.5f,
				base::vec2(HandleSize, HandleSize));
			ui::Rect(pPainter, Box, Color);
			ui::RectOutline(pPainter, Box, bDragging ? ui::RGB(255, 255, 255) :
				(bActive ? ui::Theme.m_Accent : ui::RGBA(20, 22, 26, 220)),
				(bActive || bDragging) ? 2.5f : HandleBorder);
		}

		// The fifth point of a quad: the pivot the position envelope turns the
		// quad around. A diamond, so it is never mistaken for a corner.
		void DrawPivot(ui::CPainter *pPainter, base::vec2 Center, bool bDragging)
		{
			const float R = PivotSize * 0.5f;
			pPainter->BeginPath();
			pPainter->MoveTo(Center.x, Center.y - R);
			pPainter->LineTo(Center.x + R, Center.y);
			pPainter->LineTo(Center.x, Center.y + R);
			pPainter->LineTo(Center.x - R, Center.y);
			pPainter->ClosePath();
			pPainter->FillColor(ui::RGBA(255, 236, 150, 255));
			pPainter->Fill();
			pPainter->StrokeColor(bDragging ? ui::RGB(255, 255, 255) : ui::RGBA(20, 22, 26, 220));
			pPainter->StrokeWidth(bDragging ? 2.5f : HandleBorder);
			pPainter->Stroke();
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
				/*
					A game layer cell is an index into the entity sheet, and that
					is true of the tile controllers as much as of the entities:
					cell 1 is the grey controller tile, cell 192 the spawn, and
					the client draws both by copying the cell's column and row
					out of the same sheet. The index is therefore used *as* the
					cell - subtracting the entity offset here picked the wrong
					sprite, and drawing the low cells as a number made a layer
					that is mostly controllers look like a spreadsheet. The
					number survives only as the fallback for a build with no
					sheet installed, so a cell is never invisible.
				*/
				const int Texture = EntityTexture();
				if(Texture > 0)
					DrawSprite(pPainter, Texture, pTile->m_Index, Screen.x, Screen.y, Size);
				else
				{
					const bool bEntity = pTile->m_Index >= ENTITY_OFFSET;
					ui::Rect(pPainter, base::rect::FromSize(Screen + base::vec2(Size * 0.15f, Size * 0.15f),
						base::vec2(Size * 0.7f, Size * 0.7f)),
						bEntity ? EntityColor(pTile->m_Index - ENTITY_OFFSET) : ui::Theme.m_TextDim);
					if(bLabel)
						ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Screen.x + Size * 0.5f, Screen.y + Size * 0.62f), std::min(11.0f, Size * 0.45f),
							ui::RGB(20, 20, 24), std::to_string(static_cast<int>(pTile->m_Index)), Size, ui::text_align::center);
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

		/*
			DDRace ships one sprite sheet per physics layer, and the payload's
			type byte is the index into it — the same 16 by 16 grid as the entity
			sheet. The coloured outline stays underneath so the layer still reads
			when the sheet is missing or the cell is empty.
		*/
		const int Texture = PhysicsTexture(Layer);
		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_LayerView.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				if(Texture > 0)
					DrawSprite(pPainter, Texture, pTile->m_Index, Screen.x, Screen.y, Size);
				else
					ui::RectOutline(pPainter, base::rect::FromSize(Screen, base::vec2(Size, Size)), Color, 2.0f);
			}
		}
	}

	void CEditor::DrawQuadLayer(ui::CPainter *pPainter, const CLayer &Layer)
	{
		const base::rect Canvas = CanvasRect(m_pGraphics->WindowSize());

		// Only the selected layer shows its handles, and only inside the group
		// that owns it, so the layer is looked up among the selected group's.
		bool bSelected = false;
		if(const CGroup *pGroup = SelectedGroup())
			for(std::size_t l = 0; l < pGroup->m_vLayers.size(); l++)
				if(&pGroup->m_vLayers[l] == &Layer && static_cast<int>(l) == m_SelectedLayer)
					bSelected = true;
		const bool bQuadEditor = bSelected && Mode() == edit_mode::quads;

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

			/*
				The selection is a list of indices into the selected layer, so
				it only means anything there: without that guard every other
				quad layer lit its own quad N up with the selection, which is
				what put boxes from other layers on the map.
			*/
			const bool Selected = bQuadEditor &&
				std::find(m_vSelectedQuads.begin(), m_vSelectedQuads.end(), static_cast<int>(Index)) != m_vSelectedQuads.end();
			const bool Hovered = bQuadEditor && m_QuadHover == static_cast<int>(Index);

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

			/*
				Only the layer being edited shows outlines. The other quad
				layers still draw their fill, but not a box around every quad,
				so selecting one quad cannot cover the map in the boundaries of
				the layers underneath it.
			*/
			if(bQuadEditor)
			{
				Path(aOutline);
				pPainter->StrokeColor( Selected ? ui::Theme.m_Accent : (Hovered ? ui::RGBA(200, 210, 230, 170) : ui::RGBA(200, 210, 230, 90)));
				pPainter->StrokeWidth( Selected ? 2.0f : 1.0f);
				pPainter->Stroke();
			}

			// the pivot is only marked while the layer is being edited: it is
			// white by default and would otherwise show as a dot on every map
			// that has quads
			if(bQuadEditor && Selected)
			{
				const base::vec2 Pivot = m_LayerView.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
				DrawPivot(pPainter, Pivot, false);
			}
		}
	}

	/*
		The five points of the selected quads. The four corners show the colour
		they carry, so a gradient is readable without opening the picker, and
		the corner the colour picker is aimed at has a brighter border. The
		pivot is a diamond, which is the marker the reference editor uses for
		the point the position envelope turns around.
	*/
	void CEditor::DrawQuadHandles(ui::CPainter *pPainter)
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads() || m_vSelectedQuads.empty())
			return;

		// every point of a quad that is being carried around reads as dragged,
		// so a move is visible while it happens
		const auto IsMoving = [this](int Index) {
			if(!m_bQuadMoving)
				return false;
			return std::any_of(m_vQuadMove.begin(), m_vQuadMove.end(),
				[Index](const CQuadMoveStart &Start) { return Start.m_Quad == Index; });
		};

		for(const int Index : m_vSelectedQuads)
		{
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				continue;
			const CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
			const bool bMoving = IsMoving(Index);

			CColor aColors[4];
			QuadColors(Quad, aColors);

			base::vec2 aWorld[4];
			QuadPoints(Quad, aWorld);
			for(int i = 0; i < 4; i++)
			{
				const base::vec2 Point = m_LayerView.WorldToScreen(aWorld[i]);
				const bool bActive = m_QuadColorTarget < 4 && m_QuadColorTarget == i;
				const bool bDragging = bMoving || (m_DragQuad == Index && m_DragPoint == i);
				DrawHandle(pPainter, Point, ToColor(aColors[i]), bActive, bDragging);
			}

			const base::vec2 Pivot = m_LayerView.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
			DrawPivot(pPainter, Pivot, bMoving || (m_DragQuad == Index && m_DragPoint == 4));
		}
	}

	/*
		The tile cursor shows the brush itself, not just a box. Every cell of
		the stamp is drawn as a ghost with its own flips and rotation, so the
		tile in hand and the angle it is turned to are visible before it is
		painted, and a region reads as one block. The cell under the cursor is
		outlined, which is where the stamp is anchored.
	*/
	void CEditor::DrawCursor(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		if(!m_bCursorValid)
			return;
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles())
			return;

		const float Size = TileSize * m_View.m_Zoom;
		const int TileX = static_cast<int>(std::floor(m_CursorWorld.x / TileSize));
		const int TileY = static_cast<int>(std::floor(m_CursorWorld.y / TileSize));
		const base::ivec2 Origin = BrushOrigin(m_CursorWorld);
		const base::vec2 Screen = m_View.WorldToScreen(base::vec2(Origin.x * TileSize, Origin.y * TileSize));
		const base::rect Cursor = base::rect::FromSize(Screen, base::vec2(Size * m_BrushW, Size * m_BrushH));
		if(Cursor.Intersect(Canvas).Width() <= 0.0f || Cursor.Intersect(Canvas).Height() <= 0.0f)
			return;

		const bool bGame = pLayer->IsGameLayer();
		const bool bPhysics = pLayer->IsPhysicsLayer();
		// the ghost is the layer's own colour, faded so the map under it stays
		// readable
		const CColor Color = LayerColor(*pLayer);
		const float Alpha = base::clamp(static_cast<float>(Color.a) / 255.0f, 0.0f, 1.0f) * 0.75f;
		int Texture = -1, TextureWidth = 0, TextureHeight = 0;
		if(!bGame && !bPhysics && pLayer->m_Image >= 0)
		{
			Texture = TextureForImage(pLayer->m_Image, Color);
			m_pGraphics->TextureSize(Texture, TextureWidth, TextureHeight);
		}

		if(!bPhysics)
		{
			for(int y = 0; y < m_BrushH; y++)
			{
				for(int x = 0; x < m_BrushW; x++)
				{
					const CBrushTile &Cell = BrushAt(x, y);
					const base::vec2 CellScreen = Screen + base::vec2(Size * static_cast<float>(x), Size * static_cast<float>(y));
					if(bGame)
					{
						/*
							The ghost of a game layer cell is the sheet's own
							picture, faded: a cell below the entity offset is a
							tile controller, and the layer draws it from the same
							sheet as an entity. The coloured marker is only what
							is left when no sheet is installed.
						*/
						const int Sheet = Cell.m_Index > 0 ? EntityTexture() : -1;
						if(Sheet > 0)
							DrawSprite(pPainter, Sheet, Cell.m_Index, CellScreen.x, CellScreen.y, Size, Alpha);
						else if(Cell.m_Index >= ENTITY_OFFSET)
							ui::Rect(pPainter, base::rect::FromSize(CellScreen + base::vec2(Size * 0.15f, Size * 0.15f),
								base::vec2(Size * 0.7f, Size * 0.7f)), EntityColor(Cell.m_Index - ENTITY_OFFSET));
					}
					else if(Cell.m_Index > 0)
						DrawTile(pPainter, Texture, Cell.m_Index, Cell.m_Flags, CellScreen.x, CellScreen.y, Size,
							TextureWidth, TextureHeight, Alpha);
				}
			}
		}

		// a region shows its cells, and the whole stamp its outline
		if(m_BrushW > 1 || m_BrushH > 1)
		{
			const ui::CColor Grid = ui::RGBA(255, 255, 255, 40);
			for(int x = 1; x < m_BrushW; x++)
				ui::Line(pPainter, base::vec2(Screen.x + Size * x, Screen.y),
					base::vec2(Screen.x + Size * x, Screen.y + Size * m_BrushH), Grid);
			for(int y = 1; y < m_BrushH; y++)
				ui::Line(pPainter, base::vec2(Screen.x, Screen.y + Size * y),
					base::vec2(Screen.x + Size * m_BrushW, Screen.y + Size * y), Grid);
		}
		ui::RectOutline(pPainter, Cursor, ui::Theme.m_Accent, 2.0f);

		// the cell under the cursor: the anchor of the stamp, and the cell a
		// pick would take
		const base::rect Anchor = base::rect::FromSize(
			m_View.WorldToScreen(base::vec2(TileX * TileSize, TileY * TileSize)), base::vec2(Size, Size));
		ui::RectOutline(pPainter, Anchor, ui::RGBA(255, 255, 255, 140));

		// and the tile under the cursor, angle and flips spelled out
		if(Size >= 22.0f && !bPhysics)
		{
			const CBrushTile &Cell = BrushAt(TileX - Origin.x, TileY - Origin.y);
			std::string Label = std::to_string(Cell.m_Index);
			if(Cell.m_Flags & TILEFLAG_HFLIP)
				Label += "  hflip";
			if(Cell.m_Flags & TILEFLAG_VFLIP)
				Label += "  vflip";
			if(Cell.m_Flags & TILEFLAG_ROTATE)
				Label += "  rotate";
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Cursor.Center().x, Cursor.m_BottomRight.y + 15.0f),
				11.5f, ui::Theme.m_Accent, Label, ui::text_align::center);
		}
	}

	// the marquee of a pick drag: the region that will become the brush
	void CEditor::DrawPickMarquee(ui::CPainter *pPainter)
	{
		if(!m_bPickDragging)
			return;
		int X0 = 0, Y0 = 0, X1 = -1, Y1 = -1;
		if(!PickRect(X0, Y0, X1, Y1))
			return;
		const base::rect Rect(m_View.WorldToScreen(base::vec2(X0 * TileSize, Y0 * TileSize)),
			m_View.WorldToScreen(base::vec2((X1 + 1) * TileSize, (Y1 + 1) * TileSize)));
		ui::Rect(pPainter, Rect, ui::WithAlpha(ui::Theme.m_Accent, 0.18f));
		ui::RectOutline(pPainter, Rect, ui::Theme.m_Accent, 2.0f);
	}

	void CEditor::DrawQuadCursor(ui::CPainter *pPainter)
	{
		// the hover of the layer's quads: a hint that a click selects, while
		// over a handle the same hint turns into the drag marker
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads() || !m_bCursorValid)
			return;
		if(m_QuadHover >= 0 && m_QuadHover < static_cast<int>(pLayer->m_vQuads.size()) && QuadHandleAt(m_pGraphics->Input().MousePosition()) < 0)
		{
			base::vec2 aWorld[4];
			QuadPoints(pLayer->m_vQuads[static_cast<std::size_t>(m_QuadHover)], aWorld);
			base::vec2 aScreen[4];
			QuadScreenPoints(m_LayerView, aWorld, aScreen);
			pPainter->BeginPath();
			pPainter->MoveTo(aScreen[CQuad::s_aPerimeter[0]].x, aScreen[CQuad::s_aPerimeter[0]].y);
			for(int i = 1; i < 4; i++)
				pPainter->LineTo(aScreen[CQuad::s_aPerimeter[i]].x, aScreen[CQuad::s_aPerimeter[i]].y);
			pPainter->ClosePath();
			pPainter->StrokeColor( ui::RGBA(200, 210, 230, 200));
			pPainter->StrokeWidth( 1.5f);
			pPainter->Stroke();
		}
	}

	void CEditor::DrawMap(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		pPainter->Save();
		pPainter->Scissor( Canvas.m_TopLeft.x, Canvas.m_TopLeft.y, Canvas.Width(), Canvas.Height());
		/*
			The workspace is a step darker than the docks, not a step darker than
			the dark: a canvas that is almost black swallows the map boundary,
			and at the zoom where a whole large map is visible the boundary and
			the dimming outside it have to stay readable.
		*/
		ui::Rect(pPainter, Canvas, ui::Theme.m_Workspace);

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
			const ui::CColor Dim = ui::RGBA(0, 0, 0, 90);
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

		/*
			The canvas shows one cursor, the one that belongs to what is being
			edited: the tile brush on a tile layer, the quad handles on a quad
			layer. Drawing both at once was what made the selection look like it
			did not matter.
		*/
		if(Mode() == edit_mode::quads)
		{
			m_LayerView = SelectionView();
			DrawQuadHandles(pPainter);
			DrawQuadCursor(pPainter);
		}
		else if(Mode() == edit_mode::tiles)
		{
			DrawCursor(pPainter, Canvas);
			DrawPickMarquee(pPainter);
		}

		pPainter->Restore();
	}

	// ------------------------------------------------------------ layer panel
	void CEditor::DrawLayerPanel(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_TopLeft.y), base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_BottomRight.y), ui::Theme.m_Border);

		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, Area.m_TopLeft.y + 22.0f), 14.0f, ui::Theme.m_Text, "Layers");

		/*
			The tree stops above the transport bar and the action row, and the
			action row is measured at its worst case (two lines) so a wrapped
			row can never land on top of the last group.
		*/
		const base::rect List = base::rect(
			base::vec2(Area.m_TopLeft.x, Area.m_TopLeft.y + 32.0f),
			base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y - 42.0f - DockActionRows * DockActionRowHeight));

		pPainter->Save();
		pPainter->Scissor( List.m_TopLeft.x, List.m_TopLeft.y, List.Width(), List.Height());

		float y = List.m_TopLeft.y - m_LayerScroll;
		// the row the open name field sits in, so a click anywhere else can
		// keep what was typed
		base::rect RenameRow;
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
		{
			CGroup &Group = m_Map.m_vGroups[g];
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 4.0f, y), base::vec2(List.Width() - 8.0f, RowHeight));
			if(Row.m_BottomRight.y > List.m_TopLeft.y && Row.m_TopLeft.y < List.m_BottomRight.y)
			{
				ui::Rect(pPainter, Row, ui::RGBA(255, 255, 255, 8));
				const std::string Label = Group.m_Name.empty() ? ("group " + std::to_string(g)) : Group.m_Name;
				if(Renaming() && m_RenameGroup == static_cast<int>(g) && m_RenameLayer < 0)
				{
					RenameRow = Row;
					DrawNameField(pPainter, m_pGraphics->FontBold(), Row, 8.0f, m_RenameText, m_bRenameAll);
				}
				else
					ui::TextClipped(pPainter, m_pGraphics->FontBold(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.m_TopLeft.y + 15.0f), 12.5f, ui::Theme.m_Text, Label, Row.Width() - 16.0f);
				if(ui::Hovered(Input, Row))
					Hint(Row, Label, base::Format("group %d: parallax %d%%/%d%%, %zu layers - click to select it, double click to rename it",
						static_cast<int>(g), Group.m_ParallaxX, Group.m_ParallaxY, Group.m_vLayers.size()));
				if(ui::Hovered(Input, Row) && Input.MouseDoubleClicked(engine::CInput::mouse_button::left))
					BeginRename(static_cast<int>(g), -1);
				else if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left))
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
						if(ui::Hovered(Input, Eye))
							Hint(Eye, "visible", Visible ? "this layer is drawn; click to hide it" : "this layer is hidden; click to draw it");
						if(ui::Checkbox(pPainter, Input, Eye, Visible))
							pView->m_Visible = Visible;
						bool Locked = pView->m_Locked;
						const base::rect Lock = base::rect::FromSize(base::vec2(LayerRow.m_TopLeft.x + 19.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(12.0f, 12.0f));
						if(ui::Hovered(Input, Lock))
							Hint(Lock, "locked", Locked ? "this layer cannot be edited; click to unlock it" : "click to lock this layer against edits");
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
					if(Renaming() && m_RenameGroup == static_cast<int>(g) && m_RenameLayer == static_cast<int>(l))
					{
						RenameRow = LayerRow;
						DrawNameField(pPainter, m_pGraphics->Font(), LayerRow, 36.0f, m_RenameText, m_bRenameAll);
					}
					else
						ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(LayerRow.m_TopLeft.x + 36.0f, LayerRow.m_TopLeft.y + 15.0f), 12.0f, TextColor, Label, LayerRow.Width() - 60.0f);

					/*
						The colour of a tile layer is shown as a swatch at the end
						of the row and opens the picker. For a quad layer there are
						four corner colours, so the swatch turns into the corner
						that the picker is aimed at and the quad section below the
						list holds the four of them.
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
						else if(Selected && !Layer.m_vQuads.empty())
						{
							const int QuadIndex = m_vSelectedQuads.empty() ? 0 : m_vSelectedQuads[0];
							if(QuadIndex >= 0 && QuadIndex < static_cast<int>(Layer.m_vQuads.size()))
							{
								CColor aColors[4];
								QuadColors(Layer.m_vQuads[static_cast<std::size_t>(QuadIndex)], aColors);
								SwatchColor = aColors[m_QuadColorTarget < 4 ? m_QuadColorTarget : 0];
								bHasColor = true;
							}
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

					if(ui::Hovered(Input, LayerRow))
					{
						const std::string Detail = Layer.IsTiles() ?
							(Layer.IsGameLayer() ? "a game layer: painted with entities from the sheet" :
								(Layer.IsPhysicsLayer() ? "a DDRace physics layer: its payloads are read only" :
								"a tile layer: painted with the tile brush")) :
							(Layer.IsQuads() ? "a quad layer: its points are dragged on the map" :
							(Layer.IsSounds() ? "a sound layer: its sources are placed on the map" :
							"a layer this build cannot edit"));
						Hint(LayerRow, Layer.m_Name.empty() ? "layer" : Layer.m_Name, Detail + " - double click to rename it");
					}
					// a double click on the row renames the layer, which is what
					// the same gesture does to a file everywhere else
					if(ui::Hovered(Input, LayerRow) && Input.MouseDoubleClicked(engine::CInput::mouse_button::left))
						BeginRename(static_cast<int>(g), static_cast<int>(l));
					else if(ui::Hovered(Input, LayerRow) && Input.MousePressed(engine::CInput::mouse_button::left))
					{
						// another layer is another kind of canvas: the quad
						// selection and the corner the picker edits only make
						// sense for the quads they came from
						if(m_SelectedGroup != static_cast<int>(g) || m_SelectedLayer != static_cast<int>(l))
						{
							m_SelectedGroup = static_cast<int>(g);
							m_SelectedLayer = static_cast<int>(l);
							ClearQuadSelection();
							SetQuadColorTarget(AllQuadCorners);
							m_PaletteScroll = 0.0f;
							SetStatus(base::Format("editing '%s' (%s)", Layer.m_Name.empty() ? "layer" : Layer.m_Name.c_str(),
								Layer.IsQuads() ? "quads: drag the points, C for a corner colour" :
								(Layer.IsTiles() ? (Layer.IsPhysicsLayer() ? "physics layer, read only" : "tiles: Space takes the tile under the cursor") : "not editable")));
						}
					}
				}
				y += RowHeight;
			}
		}
		pPainter->Restore();

		/*
			A click outside the open name field keeps what was typed, the way an
			inline rename does everywhere else; Escape is the way out for whoever
			changed their mind. The click still does whatever it was going to do,
			so picking another layer both renames and selects.
		*/
		if(Renaming() && Input.MousePressed(engine::CInput::mouse_button::left) &&
			!Input.MouseDoubleClicked(engine::CInput::mouse_button::left) &&
			!RenameRow.Contains(Input.MousePosition()))
			CommitRename();

		// the list can be scrolled with the wheel over the panel
		if(ui::Hovered(Input, List) && Input.Scroll() != 0.0f)
			m_LayerScroll = std::max(0.0f, m_LayerScroll - Input.Scroll() * RowHeight * 2.0f);
		const float Total = y + m_LayerScroll - List.m_TopLeft.y;
		if(Total > List.Height())
			m_LayerScroll = std::min(m_LayerScroll, Total - List.Height());
		else
			m_LayerScroll = 0.0f;

		/*
			The action row. It wraps onto a second line when the dock is narrow,
			because the layer panel is the one docker that is meant to work in a
			short, narrow column (it is Krita's layer stack), and a button that
			hangs off the edge is worse than a second row.
		*/
		const float ButtonRow = DockActionRowHeight;
		float ButtonY = Area.m_BottomRight.y - 32.0f;
		float ButtonX = Area.m_TopLeft.x + 8.0f;
		const auto SmallButton = [&](const std::string &Label, const std::string &Detail, float MinWidth = 56.0f,
			bool Enabled = true) {
			const float Width = std::max(MinWidth, ui::Measure(pPainter, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			if(ButtonX + Width > Area.m_BottomRight.x - 8.0f && ButtonX > Area.m_TopLeft.x + 8.0f)
			{
				ButtonX = Area.m_TopLeft.x + 8.0f;
				ButtonY -= ButtonRow;
			}
			const base::rect Rect = base::rect::FromSize(base::vec2(ButtonX, ButtonY), base::vec2(Width, 24.0f));
			ButtonX += Width + 6.0f;
			if(ui::Hovered(Input, Rect))
				Hint(Rect, Label, Enabled ? Detail : "this needs a layer that can be edited");
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, false, Enabled);
		};
		/*
			The clock sits above the action row. The row can wrap onto a second
			line in a narrow docker, so the space it needs is reserved up front
			rather than discovered while drawing.
		*/
		const float ActionHeight = DockActionRows * ButtonRow;
		DrawEnvelopeBar(pPainter, base::rect::FromSize(base::vec2(Area.m_TopLeft.x + 8.0f, Area.m_BottomRight.y - 36.0f - ActionHeight),
			base::vec2(Area.Width() - 16.0f, 22.0f)));
		if(SmallButton("+ Group", "add a group to the map (Ctrl+G)", 62.0f))
			AddGroup();
		// a group holds tile layers and quad layers, so both are one click away
		if(SmallButton("+ Tile", "add a tile layer to the selected group (Ctrl+L)", 48.0f))
			AddLayer(false);
		if(SmallButton("+ Quad", "add a quad layer to the selected group (Shift+Ctrl+L)", 52.0f))
			AddLayer(true);
		// deleting the last layer would leave a group nothing can be added to
		// and nothing can be selected in, so the button says when it will not
		const CGroup *pGroup = SelectedGroup();
		const bool bCanDelete = pGroup != nullptr && pGroup->m_vLayers.size() > 1;
		if(SmallButton("Delete", "delete the selected layer (Delete, Shift+Delete removes the group)", 52.0f, bCanDelete))
			DeleteSelected(false);
		if(SmallButton("Colour", "the colour of the selected tile layer or quad (C)", bCanDelete ? 52.0f : 56.0f))
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
	/*
		The right panel follows the selection. A tile layer gets the tile brush,
		a quad layer gets the quad controls; showing the brush over a quad layer
		was most of why the interface read as if the selection did not matter.
	*/
	void CEditor::DrawPalettePanel(ui::CPainter *pPainter, const base::rect &Area)
	{
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y), base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);

		const CLayer *pLayer = SelectedLayer();
		float y = Area.m_TopLeft.y + 10.0f;

		if(Mode() == edit_mode::quads)
		{
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 14.0f, ui::Theme.m_Text, "Quads");
			y += 24.0f;
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_TextDim,
				pLayer != nullptr && !pLayer->m_Name.empty() ? pLayer->m_Name : "layer", Area.Width() - 24.0f);
			y += 20.0f;
			DrawQuadSection(pPainter, Area, y);
			return;
		}

		if(Mode() == edit_mode::tiles && pLayer != nullptr && pLayer->IsTiles())
		{
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 14.0f, ui::Theme.m_Text, "Tiles");
			y += 24.0f;
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_TextDim,
				pLayer->m_Name.empty() ? "layer" : pLayer->m_Name, Area.Width() - 24.0f);
			y += 20.0f;
			DrawTileSection(pPainter, Area, y);
			return;
		}

		// nothing selected that edits: say what to select instead of showing a
		// panel full of dead controls
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 14.0f, ui::Theme.m_Text, "Nothing selected");
		y += 30.0f;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			"pick a layer in the Layers docker");
		y += 22.0f;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			"tile layers are painted,");
		y += 18.0f;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			"quad layers are point edited");
	}

	/*
		The tile brush: the tool, the size, the flips, the tile index and the
		sheet. Space takes the tile under the cursor, which is the one way to
		read a tile off the map and paint with it again.
	*/
	void CEditor::DrawTileSection(ui::CPainter *pPainter, const base::rect &Area, float &y)
	{
		engine::CInput &Input = m_pGraphics->Input();

		float x = Area.m_TopLeft.x + 12.0f;
		const auto SmallButton = [&](const std::string &Label, bool Active, const std::string &Detail,
			float Width = 0.0f) {
			const float W = Width > 0.0f ? Width : std::max(50.0f, ui::Measure(pPainter, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, y), base::vec2(W, 24.0f));
			x += W + 6.0f;
			if(ui::Hovered(Input, Rect))
				Hint(Rect, Label, Detail);
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, Active);
		};
		if(SmallButton("Paint", m_Tool == editor_tool::paint, "brush (B): paint what the brush carries"))
			SetTool(editor_tool::paint);
		if(SmallButton("Erase", m_Tool == editor_tool::erase, "eraser (E): paint empty tiles, E again goes back"))
			SetTool(editor_tool::erase);
		if(SmallButton("Pick", m_Tool == editor_tool::pick, "pick (P): drag a region of the map into the brush"))
			SetTool(editor_tool::pick);
		y += 30.0f;

		x = Area.m_TopLeft.x + 12.0f;
		if(SmallButton("1x1", BrushIsUniform(1), "a brush of one tile (4)", 46.0f))
			SetBrushUniform(1);
		if(SmallButton("2x2", BrushIsUniform(2), "a brush of 2 by 2 tiles (5)", 46.0f))
			SetBrushUniform(2);
		if(SmallButton("4x4", BrushIsUniform(4), "a brush of 4 by 4 tiles (6)", 46.0f))
			SetBrushUniform(4);
		if(SmallButton("8x8", BrushIsUniform(8), "a brush of 8 by 8 tiles (7)", 46.0f))
			SetBrushUniform(8);
		y += 30.0f;

		bool HFlip = (m_BrushFlags & TILEFLAG_HFLIP) != 0;
		bool VFlip = (m_BrushFlags & TILEFLAG_VFLIP) != 0;
		bool Rotate = (m_BrushFlags & TILEFLAG_ROTATE) != 0;
		x = Area.m_TopLeft.x + 12.0f;
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Hovered(Input, base::rect::FromSize(base::vec2(x, y), base::vec2(62.0f, 22.0f))))
				Hint(Box, "hflip  (H)", "mirror every cell of the brush horizontally");
			if(ui::Checkbox(pPainter, Input, Box, HFlip))
				ToggleBrushFlags(TILEFLAG_HFLIP);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "hflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Hovered(Input, base::rect::FromSize(base::vec2(x, y), base::vec2(62.0f, 22.0f))))
				Hint(Box, "vflip  (J)", "mirror every cell of the brush vertically");
			if(ui::Checkbox(pPainter, Input, Box, VFlip))
				ToggleBrushFlags(TILEFLAG_VFLIP);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "vflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Hovered(Input, base::rect::FromSize(base::vec2(x, y), base::vec2(52.0f, 22.0f))))
				Hint(Box, "rotate  (K)", "turn every cell of the brush a quarter turn");
			if(ui::Checkbox(pPainter, Input, Box, Rotate))
				ToggleBrushFlags(TILEFLAG_ROTATE);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "rot");
		}
		y += 30.0f;

		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.5f, ui::Theme.m_Text,
			m_BrushW == 1 && m_BrushH == 1 ? base::Format("tile %d   flags %d", m_BrushTile, m_BrushFlags) :
			base::Format("brush %dx%d   tile %d   flags %d", m_BrushW, m_BrushH, m_BrushTile, m_BrushFlags));
		y += 22.0f;
		// the two shortcuts a tile layer has that nothing else can do
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 11.5f, ui::Theme.m_TextDim,
			"Space: take a brush this size off the map");
		y += 18.0f;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 11.5f, ui::Theme.m_TextDim,
			"drag the sheet: pick a region");
		y += 22.0f;
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 8.0f, y), base::vec2(Area.m_BottomRight.x - 8.0f, y), ui::Theme.m_Border);
		y += 10.0f;

		const CLayer *pLayer = SelectedLayer();
		if(pLayer != nullptr && pLayer->IsGameLayer())
		{
			/*
				The game layer is edited from its own sprite sheet, the same way
				a tile layer is: the cell of the sheet *is* the value the layer
				stores, so picking a sprite by its picture is the whole palette.
				The cell above the offset is not an entity - the client draws
				whatever art is there - which is why nothing is hidden.
			*/
			const int Texture = EntityTexture();
			int SheetWidth = 1024, SheetHeight = 1024;
			if(Texture > 0)
				m_pGraphics->TextureSize(Texture, SheetWidth, SheetHeight);

			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f,
				ui::Theme.m_Text, "Entities");
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 12.0f, y + 12.0f), 11.0f,
				ui::Theme.m_TextDim, "click a sprite", ui::text_align::right);
			y += 22.0f;

			const float Sheet = std::min(Area.Width() - 24.0f, Area.m_BottomRight.y - y - 12.0f);
			if(Sheet > 32.0f)
			{
				const base::vec2 TopLeft(Area.m_TopLeft.x + 12.0f, y);
				const base::rect SheetRect = base::rect::FromSize(TopLeft, base::vec2(Sheet, Sheet));
				DrawSpriteSheet(pPainter, m_pGraphics->Font(), Texture, SheetRect, SheetWidth, SheetHeight);

				const float Cell = Sheet / 16.0f;
				// the cell under the cursor, named, before anything is clicked
				if(ui::Hovered(Input, SheetRect))
				{
					const int Col = base::clamp(static_cast<int>((Input.MousePosition().x - TopLeft.x) / Cell), 0, 15);
					const int Row = base::clamp(static_cast<int>((Input.MousePosition().y - TopLeft.y) / Cell), 0, 15);
					const int Index = Row * 16 + Col;
					const base::rect CellRect = base::rect::FromSize(base::vec2(TopLeft.x + Col * Cell, TopLeft.y + Row * Cell),
						base::vec2(Cell, Cell));
					if(const char *pName = Index >= ENTITY_OFFSET ? EntityName(Index - ENTITY_OFFSET) : nullptr)
						Hint(CellRect, pName, base::Format("%s is cell %d of the sheet: click to paint it", pName, Index));
					else if(Index >= ENTITY_OFFSET)
						Hint(CellRect, base::Format("entity %d", Index - ENTITY_OFFSET),
							"an entity this build has no name for; the client draws it as it is");
					else
						Hint(CellRect, base::Format("cell %d", Index),
							"a tile controller of the game layer, not an entity: entities start at cell 191");
					ui::RectOutline(pPainter, CellRect, ui::Theme.m_Border, 1.5f);
					if(Input.MousePressed(engine::CInput::mouse_button::left))
					{
						SetBrushTile(Index);
						SetTool(editor_tool::paint);
						SetStatus(base::Format("painting with cell %d of the entity sheet", Index));
					}
				}

				// the brush, which is what a drag on the map will place
				const int Col = base::clamp(m_BrushTile % 16, 0, 15);
				const int Row = base::clamp(m_BrushTile / 16, 0, 15);
				const int BlockW = std::min(m_BrushW, 16 - Col);
				const int BlockH = std::min(m_BrushH, 16 - Row);
				ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(TopLeft.x + Col * Cell, TopLeft.y + Row * Cell),
					base::vec2(Cell * BlockW, Cell * BlockH)), ui::Theme.m_Accent, 2.0f);
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
				int TextureWidth = 0, TextureHeight = 0;
				if(Texture > 0)
					m_pGraphics->TextureSize(Texture, TextureWidth, TextureHeight);
				DrawSpriteSheet(pPainter, m_pGraphics->Font(), Texture, SheetRect, TextureWidth, TextureHeight);

				const float Cell = Sheet / 16.0f;
				const auto CellAt = [&](base::vec2 Mouse, int &Col, int &Row) {
					Col = base::clamp(static_cast<int>((Mouse.x - TopLeft.x) / Cell), 0, 15);
					Row = base::clamp(static_cast<int>((Mouse.y - TopLeft.y) / Cell), 0, 15);
				};

				/*
					Dragging over the sheet takes a rectangle, not one tile: the
					region becomes the brush and the map paints the whole block
					at once. A click without moving is the 1x1 case of the same
					thing. The drag may leave the square, so the cell is clamped
					and the selection follows the edge instead of stopping.
				*/
				if(ui::Hovered(Input, SheetRect) && Input.MousePressed(engine::CInput::mouse_button::left))
				{
					CellAt(Input.MousePosition(), m_PaletteStartCell.x, m_PaletteStartCell.y);
					m_PaletteEndCell = m_PaletteStartCell;
					m_bPaletteSelecting = true;
				}
				if(m_bPaletteSelecting && Input.MouseDown(engine::CInput::mouse_button::left))
					CellAt(Input.MousePosition(), m_PaletteEndCell.x, m_PaletteEndCell.y);

				if(m_bPaletteSelecting)
				{
					const int Col0 = std::min(m_PaletteStartCell.x, m_PaletteEndCell.x);
					const int Row0 = std::min(m_PaletteStartCell.y, m_PaletteEndCell.y);
					const int Col1 = std::max(m_PaletteStartCell.x, m_PaletteEndCell.x);
					const int Row1 = std::max(m_PaletteStartCell.y, m_PaletteEndCell.y);
					std::vector<CBrushTile> Cells;
					Cells.reserve(static_cast<std::size_t>(Col1 - Col0 + 1) * static_cast<std::size_t>(Row1 - Row0 + 1));
					for(int Row = Row0; Row <= Row1; Row++)
						for(int Col = Col0; Col <= Col1; Col++)
							Cells.push_back(CBrushTile{Row * 16 + Col, m_BrushFlags});
					if(!Cells.empty())
					{
						SetBrushRegion(Col1 - Col0 + 1, Row1 - Row0 + 1, Cells, Cells.front().m_Index);
						if(!Input.MouseDown(engine::CInput::mouse_button::left))
						{
							m_bPaletteSelecting = false;
							SetStatus(m_BrushW == 1 && m_BrushH == 1 ?
								base::Format("brush tile %d", m_BrushTile) :
								base::Format("brush %dx%d of the sheet, tile %d", m_BrushW, m_BrushH, m_BrushTile));
						}
					}
				}

				// the whole brush read as one block of the sheet
				const int Col = base::clamp(m_BrushTile % 16, 0, 15);
				const int Row = base::clamp(m_BrushTile / 16, 0, 15);
				const int BlockW = std::min(m_BrushW, 16 - Col);
				const int BlockH = std::min(m_BrushH, 16 - Row);
				ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(TopLeft.x + Col * Cell, TopLeft.y + Row * Cell),
					base::vec2(Cell * BlockW, Cell * BlockH)), ui::Theme.m_Accent, 2.0f);
			}
			return;
		}

		if(pLayer != nullptr && pLayer->IsTiles())
		{
			const bool bGame = pLayer->IsGameLayer();
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
				bGame ? "the game layer is painted with entities" :
				(pLayer->IsPhysicsLayer() ? "physics payloads are read only" :
				(pLayer->m_Image < 0 ? "this layer has no image" : "no texture for this image")));
		}
	}

	/*
		The quad controls: which quad is being edited, its four corner colours
		and the five points. A corner swatch aims the picker at that corner, so
		a gradient can be built one corner at a time instead of all four at once.
	*/
	void CEditor::DrawQuadSection(ui::CPainter *pPainter, const base::rect &Area, float &y)
	{
		engine::CInput &Input = m_pGraphics->Input();
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
			return;

		const int Selected = m_vSelectedQuads.empty() ? -1 : m_vSelectedQuads[0];
		const bool bHasQuad = !pLayer->m_vQuads.empty();

		// which quad: the buttons walk the layer, the map moves the selection
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			m_vSelectedQuads.empty() ? base::Format("no quad selected (%zu in the layer)", pLayer->m_vQuads.size()) :
				base::Format("quad %d of %zu%s", Selected, pLayer->m_vQuads.size(),
					m_vSelectedQuads.size() > 1 ? base::Format(" (%zu selected)", m_vSelectedQuads.size()).c_str() : ""));
		y += 20.0f;

		float x = Area.m_TopLeft.x + 12.0f;
		const auto RowButton = [&](const std::string &Label, float Width, const std::string &Detail) {
			const base::rect Rect = base::rect::FromSize(base::vec2(x, y), base::vec2(Width, 24.0f));
			x += Width + 6.0f;
			if(ui::Hovered(Input, Rect))
				Hint(Rect, Label, bHasQuad ? Detail : "this layer has no quads yet");
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, false, bHasQuad);
		};
		const auto SelectQuad = [&](int Index) {
			m_vSelectedQuads.assign(1, Index);
			CColor Color;
			if(PickerColor(Color))
				m_PickerColor = Color;
			SetStatus(base::Format("quad %d selected", Index));
		};
		if(RowButton("Previous", 74.0f, "select the quad before this one"))
			SelectQuad(Selected <= 0 ? static_cast<int>(pLayer->m_vQuads.size()) - 1 : Selected - 1);
		if(RowButton("Next", 54.0f, "select the quad after this one"))
			SelectQuad(Selected < 0 || Selected + 1 >= static_cast<int>(pLayer->m_vQuads.size()) ? 0 : Selected + 1);
		const base::rect NoneRect = base::rect::FromSize(base::vec2(x, y), base::vec2(40.0f, 24.0f));
		if(ui::Hovered(Input, NoneRect))
			Hint(NoneRect, "None", "drop the selection: the map then selects a quad under the cursor");
		if(ui::Button(pPainter, Input, m_pGraphics->Font(), NoneRect, "None", false, !m_vSelectedQuads.empty()))
		{
			ClearQuadSelection();
			SetStatus("no quad selected");
		}
		y += 30.0f;

		if(!bHasQuad)
		{
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
				"this quad layer is empty");
			return;
		}

		// the four corner colours
		CColor aColors[4];
		const int QuadIndex = Selected >= 0 && Selected < static_cast<int>(pLayer->m_vQuads.size()) ? Selected : 0;
		QuadColors(pLayer->m_vQuads[static_cast<std::size_t>(QuadIndex)], aColors);

		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, "Corner colours");
		y += 22.0f;
		/*
			A readout rather than a row of buttons: which corner is coloured is
			decided on the map, by right clicking the point itself, so the panel
			shows the four colours and marks the one the picker is aimed at.
			Asking for a corner here was the step that made a gradient a matter
			of hitting a swatch in a panel.
		*/
		const float SwatchWidth = (Area.Width() - 24.0f - 18.0f) * 0.25f;
		for(int i = 0; i < 4; i++)
		{
			const base::rect Swatch = base::rect::FromSize(base::vec2(Area.m_TopLeft.x + 12.0f + static_cast<float>(i) * (SwatchWidth + 6.0f), y), base::vec2(SwatchWidth, 26.0f));
			const bool bActive = m_QuadColorTarget == i;
			ui::Rect(pPainter, Swatch, ToColor(aColors[i]));
			ui::RectOutline(pPainter, Swatch, bActive ? ui::Theme.m_Accent : (ui::Hovered(Input, Swatch) ? ui::RGB(200, 205, 215) : ui::Theme.m_Border), bActive ? 2.5f : 1.0f);
			if(ui::Hovered(Input, Swatch))
				Hint(Swatch, base::Format("the %s corner", CornerName(i)),
					base::Format("colour of the %s corner: right click it on the map to colour it", CornerName(i)));
		}
		y += 30.0f;

		x = Area.m_TopLeft.x + 12.0f;
		const bool bAll = m_QuadColorTarget >= 4;
		{
			const base::rect Rect = base::rect::FromSize(base::vec2(x, y), base::vec2(70.0f, 24.0f));
			x += 76.0f;
			if(ui::Hovered(Input, Rect))
				Hint(Rect, "Colour  (C)", "open the colour picker for the corner it is aimed at");
			if(ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, "Colour"))
			{
				CColor Color;
				if(PickerColor(Color))
				{
					m_PickerColor = Color;
					m_bShowColor = true;
				}
			}
		}
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(x, y + 16.0f), 11.5f, ui::Theme.m_TextDim,
			"right click a corner on the map", Area.m_BottomRight.x - 12.0f - x);
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 40.0f), 11.5f, ui::Theme.m_TextDim,
			bAll ? "editing all four corners (C)" : base::Format("editing the %s corner (C)", CornerName(m_QuadColorTarget)), Area.Width() - 24.0f);
		y += 52.0f;

		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 8.0f, y), base::vec2(Area.m_BottomRight.x - 8.0f, y), ui::Theme.m_Border);
		y += 10.0f;

		// the five points, as numbers: dragging them on the map is the other way
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, "Points");
		y += 22.0f;
		const CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(QuadIndex)];
		base::vec2 aWorld[4];
		QuadPoints(Quad, aWorld);
		for(int i = 0; i < 4; i++)
		{
			const base::rect Swatch = base::rect::FromSize(base::vec2(Area.m_TopLeft.x + 12.0f, y + 3.0f), base::vec2(10.0f, 10.0f));
			ui::Rect(pPainter, Swatch, (m_QuadColorTarget == i) ? ui::Theme.m_Accent : ui::Theme.m_TextDim);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 28.0f, y + 12.0f), 11.5f, ui::Theme.m_Text,
				base::Format("%-13s %8.1f %8.1f", CornerName(i), aWorld[i].x, aWorld[i].y));
			y += 16.0f;
		}
		const base::vec2 Pivot = m_LayerView.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
		(void)Pivot;
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 28.0f, y + 12.0f), 11.5f, ui::Theme.m_TextDim,
			base::Format("%-13s %8.1f %8.1f", "pivot", base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
		y += 22.0f;

		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_TextDim,
			"drag a quad to move it; Shift+drag", Area.Width() - 24.0f);
		y += 16.0f;
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_TextDim,
			"moves one point (square corner,", Area.Width() - 24.0f);
		y += 16.0f;
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_TextDim,
			"diamond pivot)", Area.Width() - 24.0f);
		y += 16.0f;

		if(Quad.m_PosEnv >= 0)
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 10.0f), 11.5f, ui::Theme.m_Warning,
				base::Format("position envelope %d is applied", Quad.m_PosEnv));
	}

	// ------------------------------------------------------------- status bar
	void CEditor::DrawStatusBar(ui::CPainter *pPainter, const base::rect &Area)
	{
		ui::Rect(pPainter, Area, ui::Theme.m_PanelDark);
		ui::Line(pPainter, Area.m_TopLeft, base::vec2(Area.m_BottomRight.x, Area.m_TopLeft.y), ui::Theme.m_Border);

		const CLayer *pLayer = SelectedLayer();
		const int TileX = m_bCursorValid ? static_cast<int>(std::floor(m_CursorWorld.x / TileSize)) : -1;
		const int TileY = m_bCursorValid ? static_cast<int>(std::floor(m_CursorWorld.y / TileSize)) : -1;
		const edit_mode Mode = this->Mode();
		const char *pMode = Mode == edit_mode::tiles ? "tiles" : (Mode == edit_mode::quads ? "quads" : "read only");
		std::string Info = base::Format("v%d  %s  target %s  %s  tile %d,%d  zoom %.2f  %s",
			m_Map.m_SourceDatafileVersion,
			m_VersionId.c_str(),
			GetTargetProfile(m_Target).m_pId,
			pMode,
			TileX, TileY, m_View.m_Zoom,
			pLayer != nullptr ? (pLayer->m_Name.empty() ? "layer" : pLayer->m_Name.c_str()) : "no layer");
		if(Mode == edit_mode::quads && pLayer != nullptr)
			Info += base::Format("  quads %s", m_vSelectedQuads.empty() ? "none selected" :
				base::Format("%d selected", static_cast<int>(m_vSelectedQuads.size())).c_str());

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

		if(Input.KeyPressed(engine::keys::Escape))
		{
			m_bShowOpen = m_bShowSave = m_bShowExport = m_bShowHelp = m_bShowColor = false;
			m_Menu = menu_id::none;
			return;
		}

		if(m_bShowColor)
		{
			// the picker handles its own mouse input while it is drawn, and a
			// click away from it has already closed it by the time this runs
			return;
		}

		if(m_bShowOpen)
		{
			if(Input.KeyPressed(engine::keys::Down))
				m_BrowserSelected = std::min(m_BrowserSelected + 1, static_cast<int>(m_vBrowserEntries.size()) - 1);
			if(Input.KeyPressed(engine::keys::Up))
				m_BrowserSelected = std::max(m_BrowserSelected - 1, 0);
			if(Input.KeyPressed(engine::keys::Return) || Input.KeyPressed(engine::keys::KpEnter))
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
			if(Input.KeyPressed(engine::keys::Tab) || Input.KeyPressed(engine::keys::Down))
				m_ExportTarget = (m_ExportTarget + 1) % static_cast<int>(AllTargets().size());
			if(Input.KeyPressed(engine::keys::Up))
				m_ExportTarget = (m_ExportTarget + static_cast<int>(AllTargets().size()) - 1) % static_cast<int>(AllTargets().size());
			m_TextInput += Input.TypedText();
			if(Input.BackspacePressed() && !m_TextInput.empty())
				m_TextInput.pop_back();
			if(Input.KeyPressed(engine::keys::Return) || Input.KeyPressed(engine::keys::KpEnter))
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
			if(Input.KeyPressed(engine::keys::Return) || Input.KeyPressed(engine::keys::KpEnter))
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

	void CEditor::DrawColorPopup(ui::CPainter *pPainter)
	{
		engine::CInput &Input = m_pGraphics->Input();

		/*
			Where the popup is and how tall it is belongs to the editor, not to
			the drawing: the click that dismisses it has to be tested against
			the same rectangle that was drawn, and a windowless frame has to be
			able to do the same.
		*/
		const base::rect Popup = ColorPopupRect();
		const bool bCorners = PickerTargetsQuad();
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

		/*
			The four corners of the edited quad, live. Editing a gradient means
			switching between corners, and doing that from inside the picker is
			what makes it a corner editor rather than a shape-wide one.
		*/
		if(bCorners)
		{
			if(const CLayer *pLayer = SelectedLayer())
				if(!pLayer->m_vQuads.empty())
				{
					const int Index = m_vSelectedQuads.empty() ? 0 : m_vSelectedQuads[0];
					if(Index >= 0 && Index < static_cast<int>(pLayer->m_vQuads.size()))
					{
						CColor aColors[4];
						QuadColors(pLayer->m_vQuads[static_cast<std::size_t>(Index)], aColors);
						const float SwatchWidth = (Width - 6.0f) * 0.25f;
						for(int i = 0; i < 4; i++)
						{
							const base::rect Swatch = base::rect::FromSize(base::vec2(X + static_cast<float>(i) * (SwatchWidth + 2.0f), y), base::vec2(SwatchWidth, 16.0f));
							ui::Rect(pPainter, Swatch, ToColor(aColors[i]));
							ui::RectOutline(pPainter, Swatch, m_QuadColorTarget == i ? ui::Theme.m_Accent : ui::Theme.m_Border, m_QuadColorTarget == i ? 2.0f : 1.0f);
							if(ui::Hovered(Input, Swatch) && Input.MousePressed(engine::CInput::mouse_button::left))
							{
								// the undo snapshot stays per drag: switching
								// target mid-popup only changes where the next
								// edit lands
								SetQuadColorTarget(i);
								m_PickerColor = pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_aColors[i];
								RgbToHsv(m_PickerColor, m_PickerHue, Saturation, Value, m_PickerHue);
								SetStatus(base::Format("the picker now edits the %s corner", CornerName(i)));
							}
						}
						const base::rect All = base::rect::FromSize(base::vec2(X, y + 20.0f), base::vec2(Width, 18.0f));
						if(ui::Button(pPainter, Input, m_pGraphics->Font(), All, "all four corners", m_QuadColorTarget >= 4))
							SetQuadColorTarget(AllQuadCorners);
						y += 44.0f;
					}
				}
		}

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

		// the presets and the hint share the last row, so the hint sits beside
		// the swatches rather than behind them. It names the click first: that
		// is the way out a popup is expected to have
		const float PresetWidth = 8 * 26.0f;
		if(X + PresetWidth + 200.0f < Popup.m_BottomRight.x - 12.0f)
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(X + PresetWidth + 6.0f, y + 46.0f), 11.0f, ui::Theme.m_TextDim,
				"clicking away closes it, esc too");
		else
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_BottomRight.y - 8.0f), 11.0f, ui::Theme.m_TextDim,
				"clicking away closes it, esc too");
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
		/*
			The open menu is drawn first of the overlays: it hangs from the menu
			bar, and a popup is drawn over the middle of the window, so the two
			do not normally meet.
		*/
		DrawMenus(pPainter);
		if(m_bShowOpen)
			DrawOpenPopup(pPainter);
		if(m_bShowSave)
			DrawSavePopup(pPainter);
		if(m_bShowExport)
			DrawExportPopup(pPainter);
		if(m_bShowColor)
			DrawColorPopup(pPainter);

		// clicking outside a popup closes it, except for the help panel
		if((m_bShowOpen || m_bShowSave || m_bShowExport) && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			// the popups handle their own clicks, so only close when the click
			// was above or below their area
		}
	}

	// -------------------------------------------------------------------- docks
	/*
		The two side docks. Each one is a tab strip plus the content of the tab
		that is showing, and a splitter along its inner edge that is dragged to
		resize it. The docks are what makes this a Krita style workspace rather
		than three fixed columns: the layout is the mapmaker's.
	*/
	bool CEditor::DockTab(ui::CPainter *pPainter, const base::rect &Row, dock_panel Panel, bool Active, int Count, int Index, bool bDragging)
	{
		const std::string Label = TabLabel(Panel);
		engine::CInput &Input = this->Input();
		const float SlotWidth = Row.Width() / static_cast<float>(std::max(Count, 1));
		base::rect Tab = base::rect::FromSize(base::vec2(Row.m_TopLeft.x + SlotWidth * Index, Row.m_TopLeft.y),
			base::vec2(SlotWidth, Row.Height()));
		// a tab that is being dragged follows the cursor, so the drop target is
		// visible while the button is still held
		if(bDragging)
			Tab = base::rect::FromSize(base::vec2(Input.MousePosition().x - Tab.Width() * 0.5f, Row.m_TopLeft.y),
				base::vec2(Tab.Width(), Row.Height()));
		const bool bHover = ui::Hovered(Input, Tab);
		if(Active)
			ui::Rect(pPainter, Tab, ui::Theme.m_Panel);
		else if(bHover)
			ui::Rect(pPainter, Tab, ui::Theme.m_Hover);
		else
			ui::Rect(pPainter, Tab, ui::Theme.m_PanelDark);
		// the active tab is joined to the content below it, so the seam is
		// drawn only under the inactive ones
		if(!Active)
			ui::Line(pPainter, base::vec2(Tab.m_TopLeft.x, Tab.m_BottomRight.y - 0.5f),
				base::vec2(Tab.m_BottomRight.x, Tab.m_BottomRight.y - 0.5f), ui::Theme.m_Border);
		if(Active)
		{
			ui::Rect(pPainter, base::rect::FromSize(Tab.m_TopLeft, base::vec2(Tab.Width(), 2.0f)), ui::Theme.m_Accent);
			ui::Rect(pPainter, base::rect::FromSize(base::vec2(Tab.m_TopLeft.x, Tab.m_BottomRight.y - 1.0f),
				base::vec2(Tab.Width(), 1.0f)), ui::Theme.m_Panel);
		}
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Tab.Center().x, Tab.Center().y + 4.5f), 12.0f,
			Active ? ui::Theme.m_Text : ui::Theme.m_TextDim, Label, Tab.Width() - 8.0f, ui::text_align::center);
		if(bHover && !bDragging)
			Hint(Tab, Label, base::Format("%s - drag the tab to move it between the dockers", TabHint(Panel)));
		if(!bHover)
			return false;
		return Input.MousePressed(engine::CInput::mouse_button::left);
	}

	void CEditor::DrawDockHeaders(ui::CPainter *pPainter, const base::vec2 &Window)
	{
		engine::CInput &Input = this->Input();

		for(const dock_slot Slot : {dock_slot::right_upper, dock_slot::right_lower})
		{
			const base::rect Row = DockTabRow(Slot, Window);
			if(Row.Width() <= 1.0f)
				continue;

			// the strip itself, and the collapse button at its inner edge
			ui::Rect(pPainter, Row, ui::Theme.m_PanelDark);
			const base::rect Collapse = base::rect::FromSize(base::vec2(Row.m_BottomRight.x - Row.Height(), Row.m_TopLeft.y),
				base::vec2(Row.Height(), Row.Height()));
			const bool bCollapsed = DockCollapsed(Slot);
			ui::Text(pPainter, m_pGraphics->FontBold(), Collapse.Center() + base::vec2(0.0f, 4.5f), 13.0f, ui::Theme.m_TextDim,
				bCollapsed ? "-" : "v", ui::text_align::center);
			if(ui::Hovered(Input, Collapse))
				Hint(Collapse, bCollapsed ? "expand" : "collapse",
					bCollapsed ? "show this docker again" : "fold this docker down to its tab strip");
			if(ui::Hovered(Input, Collapse) && Input.MousePressed(engine::CInput::mouse_button::left))
				SetDockCollapsed(Slot, !bCollapsed);

			if(bCollapsed)
			{
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.Center().y + 4.0f), 12.0f,
					ui::Theme.m_TextDim, TabLabel(DockPanel(Slot)), Row.Width() - Row.Height() - 12.0f);
				if(ui::Hovered(Input, Row))
					Hint(Row, TabLabel(DockPanel(Slot)), TabHint(DockPanel(Slot)));
				continue;
			}

			/*
				A workspace lives in one docker at a time, so the *displayed* tabs
				are this docker's order minus whatever the other one shows. The
				full order is kept underneath, so moving a tab back restores the
				place it had.
			*/
			const dock_slot OtherSlot = Slot == dock_slot::right_upper ? dock_slot::right_lower : dock_slot::right_upper;
			const dock_panel Other = DockPanel(OtherSlot);
			std::vector<dock_panel> Order;
			for(const dock_panel Panel : Tabs(Slot))
				if(Panel != Other)
					Order.push_back(Panel);
			int Count = static_cast<int>(Order.size());
			// the drag is tracked by slot, so a tab can be dropped on the other
			// docker: 0 is the upper one, 1 the lower one
			const int DragSlot = Slot == dock_slot::right_upper ? 0 : 1;
			// while a tab is being carried, its own tab is still drawn, at the
			// end, so the thing under the cursor is the thing being moved
			const bool bCarrying = m_DragTab == DragSlot && m_bDraggedTabMoved;
			const int DraggedIndex = bCarrying && m_DragTabIndex >= 0 && m_DragTabIndex < Count ? m_DragTabIndex : -1;
			if(Count == 0 && !bCarrying)
				continue;
			// the tab area stops before the collapse button
			const base::rect Tabs = base::rect::FromSize(Row.m_TopLeft, base::vec2(std::max(Row.Width() - Row.Height(), 1.0f), Row.Height()));
			const dock_panel Active = DockPanel(Slot);
			for(int i = 0; i < Count; i++)
			{
				const bool bDragging = bCarrying && DraggedIndex == i;
				const bool bActive = Order[static_cast<std::size_t>(i)] == Active;
				if(DockTab(pPainter, Tabs, Order[static_cast<std::size_t>(i)], bActive, std::max(Count, 1), i, bDragging))
					SetDockTab(Slot, Order[static_cast<std::size_t>(i)]);
			}
			if(Count == 0)
				continue;
			/*
				Where a dragged tab would land. A drop is aimed at a slot rather
				than at a tab, so the indicator is a bar in the gap, which is
				what makes the drag predictable while the cursor is over the tab
				it is carrying.
			*/
			if(bCarrying)
			{
				const int Gap = base::clamp(static_cast<int>((Input.MousePosition().x - Tabs.m_TopLeft.x) /
					std::max(Tabs.Width() / static_cast<float>(std::max(Count, 1)), 1.0f)), 0, Count);
				const float SlotWidth = Tabs.Width() / static_cast<float>(std::max(Count, 1));
				const float SlotX = Tabs.m_TopLeft.x + SlotWidth * static_cast<float>(Gap);
				ui::Rect(pPainter, base::rect::FromSize(base::vec2(SlotX - 1.0f, Tabs.m_TopLeft.y),
					base::vec2(2.0f, Tabs.Height())), ui::Theme.m_Accent);
			}
		}
	}

	void CEditor::HandleDockInput(const base::vec2 &Window)
	{
		engine::CInput &Input = this->Input();
		/*
			An open menu hangs over the dockers, and the mouse is blocked while
			it is there: without this the click on a row of the menu would also
			grab a splitter or start dragging a tab underneath it.
		*/
		if(Input.MouseBlocked())
			return;
		const base::vec2 Mouse = Input.MousePosition();

		/*
			Two splitters: the column edge against the canvas, dragged sideways
			to set the dock width, and the horizontal one between the two
			dockers, dragged up and down to share the column between them.
		*/
		if(m_DragSplitter < 0 && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			const base::rect Column = DockSplitter(dock_slot::right_upper, Window);
			if(Column.Width() > 0.0f && Column.Contains(Mouse))
			{
				m_DragSplitter = 0;
				m_DragSplitterStart = Mouse.x;
				m_DragSplitterOrigin = DockWidth();
			}
			else
			{
				const base::rect Split = DockSplitterH(Window);
				if(Split.Height() > 0.0f && Split.Contains(Mouse))
				{
					m_DragSplitter = 1;
					m_DragSplitterStart = Mouse.y;
					m_DragSplitterOrigin = m_RightSplit;
				}
			}
		}
		if(m_DragSplitter >= 0)
		{
			if(Input.MouseDown(engine::CInput::mouse_button::left))
			{
				if(m_DragSplitter == 0)
				{
					// the column grows to the left, so the delta is negated
					const float Width = m_DragSplitterOrigin - (Mouse.x - m_DragSplitterStart);
					const dock_panel Panel = m_RightDock[DockCollapsed(dock_slot::right_upper) ? 1 : 0];
					m_aDocks[static_cast<std::size_t>(Panel)].m_Width =
						base::clamp(Width, MinDockWidth, std::max(MinDockWidth, Window.x * 0.6f));
				}
				else
				{
					// dragging the split up makes the lower docker taller
					const float Lower = m_DragSplitterOrigin - (Mouse.y - m_DragSplitterStart);
					const float Available = std::max(Window.y - ChromeHeight - StatusHeight, MinDockHeight * 2.0f);
					m_RightSplit = base::clamp(Lower, MinDockHeight, Available - MinDockHeight - DockSplitterHeight);
					m_bLowerCollapsed = false;
				}
			}
			else
				m_DragSplitter = -1;
		}

		// the cursor says a splitter is grabbable
		m_bSplitterHover = false;
		m_bSplitterHoverH = false;
		const base::rect Column = DockSplitter(dock_slot::right_upper, Window);
		if(Column.Width() > 0.0f && Column.Contains(Mouse))
			m_bSplitterHover = true;
		const base::rect Split = DockSplitterH(Window);
		if(Split.Height() > 0.0f && Split.Contains(Mouse))
			m_bSplitterHoverH = true;

		/*
			A tab is dragged out of a docker and into another slot. The slot is
			found from the cursor position rather than from a hit test per tab,
			because while the button is down the cursor is over the dragged tab
			itself rather than over the gap it is headed for.
		*/
		if(m_DragTab < 0 && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			for(const dock_slot Slot : {dock_slot::right_upper, dock_slot::right_lower})
			{
				const base::rect Row = DockTabRow(Slot, Window);
				if(Row.Width() <= 1.0f)
					continue;
				const base::rect Strip = base::rect::FromSize(Row.m_TopLeft,
					base::vec2(std::max(Row.Width() - Row.Height(), 1.0f), Row.Height()));
				if(!Strip.Contains(Mouse))
					continue;
				const dock_slot OtherSlot = Slot == dock_slot::right_upper ? dock_slot::right_lower : dock_slot::right_upper;
				const dock_panel Other = DockPanel(OtherSlot);
				std::vector<dock_panel> Displayed;
				for(const dock_panel Panel : Tabs(Slot))
					if(Panel != Other)
						Displayed.push_back(Panel);
				const int Count = static_cast<int>(Displayed.size());
				if(Count <= 0)
					continue;
				const int Index = base::clamp(static_cast<int>((Mouse.x - Strip.m_TopLeft.x) /
					(Strip.Width() / static_cast<float>(Count))), 0, Count - 1);
				m_DragTab = Slot == dock_slot::right_upper ? 0 : 1;
				m_DragTabIndex = Index;
				m_DragTabPos = Mouse;
				// the tab follows the cursor from the first frame, so a press
				// with no movement is still a plain tab selection
				m_bDraggedTabMoved = false;
				break;
			}
		}
		if(m_DragTab >= 0)
		{
			const dock_slot FromSlot = m_DragTab == 0 ? dock_slot::right_upper : dock_slot::right_lower;
			if(Input.MouseDown(engine::CInput::mouse_button::left))
			{
				if(std::abs(Mouse.x - m_DragTabPos.x) + std::abs(Mouse.y - m_DragTabPos.y) > 4.0f)
					m_bDraggedTabMoved = true;
			}
			else
			{
				if(m_bDraggedTabMoved)
				{
					// the docker the cursor is over: the lower one unless the
					// cursor is in the upper docker's own area
					const base::rect Upper = DockArea(dock_slot::right_upper, Window);
					const base::rect Lower = DockArea(dock_slot::right_lower, Window);
					const bool bOverLower = Lower.Width() > 1.0f && Lower.Contains(Mouse);
					const dock_slot ToSlot = bOverLower ? dock_slot::right_lower :
						(Upper.Width() > 1.0f && Upper.Contains(Mouse) ? dock_slot::right_upper : FromSlot);
					std::vector<dock_panel> &From = Tabs(FromSlot);
					std::vector<dock_panel> &To = Tabs(ToSlot);
					const int FromIndex = base::clamp(m_DragTabIndex, 0, std::max(static_cast<int>(From.size()) - 1, 0));
					const dock_panel Moved = From[static_cast<std::size_t>(FromIndex)];
					/*
						The slot is aimed at the *other* tabs of the target
						docker; the panel being carried is not in that list,
						because it is in the hand.
					*/
					const base::rect TargetRow = DockTabRow(ToSlot, Window);
					const dock_panel TargetOther = DockPanel(ToSlot == dock_slot::right_upper ? dock_slot::right_lower : dock_slot::right_upper);
					std::vector<dock_panel> Displayed;
					for(const dock_panel Panel : To)
						if(Panel != TargetOther && Panel != Moved)
							Displayed.push_back(Panel);
					const float StripWidth = std::max(TargetRow.Width() - TargetRow.Height(), 1.0f);
					const int Slots = static_cast<int>(Displayed.size());
					const int SlotIndex = Slots > 0 ? base::clamp(static_cast<int>((Mouse.x - TargetRow.m_TopLeft.x) /
						(StripWidth / static_cast<float>(Slots))), 0, Slots) : 0;
					// the left half of a tab inserts before it, the right half after
					const float SlotWidth = StripWidth / static_cast<float>(std::max(Slots, 1));
					int At = static_cast<int>(To.size());
					if(Slots > 0)
					{
						const dock_panel Reference = Displayed[static_cast<std::size_t>(base::clamp(SlotIndex, 0, Slots - 1))];
						const auto It = std::find(To.begin(), To.end(), Reference);
						const int Base = static_cast<int>(std::distance(To.begin(), It));
						const float Centre = TargetRow.m_TopLeft.x + SlotWidth * (static_cast<float>(base::clamp(SlotIndex, 0, Slots - 1)) + 0.5f);
						At = Mouse.x < Centre ? Base : Base + 1;
					}
					if(ToSlot == FromSlot)
					{
						std::vector<dock_panel> Reordered;
						for(const dock_panel Panel : From)
							if(Panel != Moved)
								Reordered.push_back(Panel);
						int Insert = base::clamp(At, 0, static_cast<int>(Reordered.size()));
						if(At > FromIndex)
							Insert = base::clamp(At - 1, 0, static_cast<int>(Reordered.size()));
						Reordered.insert(Reordered.begin() + Insert, Moved);
						From = std::move(Reordered);
					}
					else
					{
						From.erase(From.begin() + FromIndex);
						To.erase(std::remove(To.begin(), To.end(), Moved), To.end());
						const int Insert = base::clamp(At, 0, static_cast<int>(To.size()));
						To.insert(To.begin() + Insert, Moved);
						// the workspace is now shown by the docker it landed in
						SetDockTab(ToSlot, Moved);
					}
					SetStatus("moved the " + std::string(TabLabel(Moved)) + " tab");
				}
				m_DragTab = -1;
				m_DragTabIndex = -1;
			}
		}
	}

	// ---------------------------------------------------------- rules workspace
	/*
		The rule workspace is the visual editor for the project's autorules. It
		is a browser on the left and an inspector on the right, the way a vector
		editor shows a layer tree and its properties, because a rule set is a
		three level tree: configurations, runs, and the index rules of a run.

		Every field writes straight into the document, so the workspace is
		always showing the truth and "apply" can never run something the user
		did not see.
	*/
	namespace
	{
		const char *RuleSlotName(int Slot)
		{
			switch(Slot)
			{
			case 0: return "game layer";
			case 1: return "tele";
			case 2: return "speedup";
			case 3: return "front";
			case 4: return "switch";
			case 5: return "tune";
			default: return "tiles";
			}
		}

		// a small text field backed by a std::string, drawn inline: the widget
		// set has no text input, and a rule editor without names would be a
		// tree of anonymous rows
		bool TextField(ui::CPainter *pPainter, engine::CInput &Input, int Font, const base::rect &Area,
			std::string &Value, const char *pPlaceholder)
		{
			const bool bHover = ui::Hovered(Input, Area);
			ui::Rect(pPainter, Area, ui::Theme.m_PanelDark);
			ui::RectOutline(pPainter, Area, bHover ? ui::Theme.m_Accent : ui::Theme.m_Border);
			const std::string Shown = Value.empty() ? std::string(pPlaceholder) : Value;
			ui::TextClipped(pPainter, Font, base::vec2(Area.m_TopLeft.x + 6.0f, Area.Center().y + 4.5f), 12.0f,
				Value.empty() ? ui::Theme.m_TextDim : ui::Theme.m_Text, Shown, Area.Width() - 12.0f);
			return bHover;
		}

	} // namespace

	void CEditor::DrawRulesWorkspace(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = this->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);

		// ------------------------------------------------------------------ head
		const base::rect Head = base::rect::FromSize(Area.m_TopLeft, base::vec2(Area.Width(), 26.0f));
		ui::Rect(pPainter, Head, ui::Theme.m_PanelDark);
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Head.m_TopLeft.x + 8.0f, Head.Center().y + 4.5f), 12.5f,
			ui::Theme.m_Text, "Autorules");
		ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Head.m_BottomRight.x - 8.0f, Head.Center().y + 4.5f), 11.0f,
			ui::Theme.m_TextDim, base::Format("%d configs / %d rules", static_cast<int>(m_RuleSet.m_vConfigs.size()), m_RuleSet.RuleCount()),
			Head.Width() * 0.6f, ui::text_align::right);

		float y = Head.m_BottomRight.y + 6.0f;
		const float X = Area.m_TopLeft.x + 6.0f;
		const float Width = Area.Width() - 12.0f;
		const float RowHeight = 20.0f;

		// ------------------------------------------------------------------ tree
		/*
			The tree takes the top half and the inspector the bottom, because a
			rule set is read far more often than it is written: the shape of the
			whole document has to be visible while one rule is edited.
		*/
		const float TreeHeight = std::max(Area.Height() * 0.42f, 90.0f);
		const base::rect Tree = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, TreeHeight));
		ui::Rect(pPainter, Tree, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, Tree, ui::Theme.m_Border);
		pPainter->Save();
		pPainter->Scissor(Tree.m_TopLeft.x, Tree.m_TopLeft.y, Tree.Width(), Tree.Height());

		float ty = Tree.m_TopLeft.y + 2.0f;
		if(m_RuleSet.m_vConfigs.empty())
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Tree.m_TopLeft.x + 8.0f, ty + 14.0f), 12.0f,
				ui::Theme.m_TextDim, "no rules yet: create a configuration to start", Tree.Width() - 16.0f);
		for(std::size_t c = 0; c < m_RuleSet.m_vConfigs.size(); c++)
		{
			const rules::CConfiguration &Config = m_RuleSet.m_vConfigs[c];
			const base::rect Row = base::rect::FromSize(base::vec2(Tree.m_TopLeft.x + 2.0f, ty), base::vec2(Tree.Width() - 4.0f, RowHeight));
			if(static_cast<int>(c) == m_RuleConfig)
				ui::Rect(pPainter, Row, ui::Theme.m_Selection);
			else if(ui::Hovered(Input, Row))
				ui::Rect(pPainter, Row, ui::Theme.m_Hover);
			ui::TextClipped(pPainter, m_pGraphics->FontBold(), base::vec2(Row.m_TopLeft.x + 6.0f, Row.Center().y + 4.5f), 12.0f,
				ui::Theme.m_Text, base::Format("config %zu  %s", c + 1, Config.m_Name.c_str()), Row.Width() - 12.0f);
			if(ui::Hovered(Input, Row))
				Hint(Row, base::Format("config %zu", c + 1),
					base::Format("%s: a page of rules, applied to the layers it names", Config.m_Name.c_str()));
			if(ui::Hovered(Input, Row) && Input.MousePressed(engine::CInput::mouse_button::left))
				SetRuleSelection(static_cast<int>(c), 0, 0);
			ty += RowHeight;

			for(std::size_t r = 0; r < Config.m_vRuns.size(); r++)
			{
				const rules::CRun &Run = Config.m_vRuns[r];
				const base::rect RunRow = base::rect::FromSize(base::vec2(Tree.m_TopLeft.x + 16.0f, ty), base::vec2(Tree.Width() - 20.0f, RowHeight));
				const bool bRunActive = static_cast<int>(c) == m_RuleConfig && static_cast<int>(r) == m_RuleRun;
				if(bRunActive)
					ui::Rect(pPainter, RunRow, ui::Theme.m_AccentDim);
				else if(ui::Hovered(Input, RunRow))
					ui::Rect(pPainter, RunRow, ui::Theme.m_Hover);
				ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(RunRow.m_TopLeft.x + 6.0f, RunRow.Center().y + 4.5f), 11.5f,
					ui::Theme.m_Text, base::Format("%s  ->  %s  (%d rules)", RuleSlotName(Run.m_ReadSlot), RuleSlotName(Run.m_TargetSlot),
						static_cast<int>(Run.m_vIndexRules.size())),
					RunRow.Width() - 12.0f);
				if(ui::Hovered(Input, RunRow))
					Hint(RunRow, "run", base::Format("reads '%s' and writes '%s', in one pass",
						RuleSlotName(Run.m_ReadSlot), RuleSlotName(Run.m_TargetSlot)));
				if(ui::Hovered(Input, RunRow) && Input.MousePressed(engine::CInput::mouse_button::left))
					SetRuleSelection(static_cast<int>(c), static_cast<int>(r), 0);
				ty += RowHeight;

				if(static_cast<int>(c) != m_RuleConfig || static_cast<int>(r) != m_RuleRun)
					continue;
				for(std::size_t i = 0; i < Run.m_vIndexRules.size(); i++)
				{
					const rules::CIndexRule &Rule = Run.m_vIndexRules[i];
					const base::rect RuleRow = base::rect::FromSize(base::vec2(Tree.m_TopLeft.x + 30.0f, ty), base::vec2(Tree.Width() - 34.0f, RowHeight));
					if(static_cast<int>(i) == m_RuleIndex)
						ui::Rect(pPainter, RuleRow, ui::Theme.m_Selection);
					else if(ui::Hovered(Input, RuleRow))
						ui::Rect(pPainter, RuleRow, ui::Theme.m_Hover);
					ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(RuleRow.m_TopLeft.x + 6.0f, RuleRow.Center().y + 4.5f), 11.0f,
						ui::Theme.m_TextDim, base::Format("%s  writes %d", Rule.m_Name.c_str(), Rule.m_Tile), RuleRow.Width() - 12.0f);
					if(ui::Hovered(Input, RuleRow))
						Hint(RuleRow, Rule.m_Name.empty() ? "rule" : Rule.m_Name,
							"an index rule: where its conditions match, it writes its tile");
					if(ui::Hovered(Input, RuleRow) && Input.MousePressed(engine::CInput::mouse_button::left))
						SetRuleSelection(static_cast<int>(c), static_cast<int>(r), static_cast<int>(i));
					ty += RowHeight;
				}
			}
		}
		pPainter->Restore();
		y = Tree.m_BottomRight.y + 6.0f;

		// the tree actions
		const float ButtonWidth = (Width - 12.0f) / 4.0f;
		const auto TreeButton = [&](int Slot, const char *pLabel, const std::string &Detail) {
			const base::rect Rect = base::rect::FromSize(
				base::vec2(X + (ButtonWidth + 4.0f) * static_cast<float>(Slot), y), base::vec2(ButtonWidth, 22.0f));
			if(ui::Hovered(Input, Rect))
				Hint(Rect, pLabel, Detail);
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, pLabel);
		};
		if(TreeButton(0, "config", "add a configuration: a page of runs"))
			AddRuleConfig();
		if(TreeButton(1, "run", "add a run: it reads one layer and writes another"))
			AddRuleRun();
		if(TreeButton(2, "rule", "add an index rule to the selected run"))
			AddRule();
		if(TreeButton(3, "delete", "delete whatever the tree has selected"))
			DeleteRuleSelection();
		y += 28.0f;

		// ----------------------------------------------------------------- apply
		const base::rect ApplyRow = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, 24.0f));
		const float HalfWidth = (Width - 6.0f) * 0.5f;
		if(ui::Hovered(Input, ApplyRow))
			Hint(ApplyRow, "Apply", "run the rules over the selected tile layer, with one step of undo");
		if(ui::Button(pPainter, Input, m_pGraphics->FontBold(),
			base::rect::FromSize(ApplyRow.m_TopLeft, base::vec2(HalfWidth, ApplyRow.Height())), "Apply this configuration"))
		{
			std::string Error;
			// the rule runs against the layer the workspace has selected
			const int Slot = SlotForLayer(m_SelectedGroup, m_SelectedLayer);
			if(Slot < 0)
				SetStatus("select a tile layer to apply rules to");
			else if(!ApplyRules(m_RuleConfig, Slot, 0, &Error))
				LOG_WARN("editor", "autorules: %s", Error.c_str());
		}
		if(ui::Button(pPainter, Input, m_pGraphics->Font(),
			base::rect::FromSize(base::vec2(ApplyRow.m_TopLeft.x + HalfWidth + 6.0f, ApplyRow.m_TopLeft.y), base::vec2(HalfWidth, ApplyRow.Height())), "Apply all"))
		{
			std::string Error;
			const int Slot = SlotForLayer(m_SelectedGroup, m_SelectedLayer);
			if(Slot < 0)
				SetStatus("select a tile layer to apply rules to");
			else if(!ApplyRules(-1, Slot, 0, &Error))
				LOG_WARN("editor", "autorules: %s", Error.c_str());
		}
		y += 30.0f;

		// -------------------------------------------------------------- inspector
		const base::rect Inspector = base::rect::FromSize(base::vec2(X, y), base::vec2(Width, std::max(Area.m_BottomRight.y - y - 6.0f, 40.0f)));
		ui::Rect(pPainter, Inspector, ui::Theme.m_PanelDark);
		ui::RectOutline(pPainter, Inspector, ui::Theme.m_Border);
		pPainter->Save();
		pPainter->Scissor(Inspector.m_TopLeft.x, Inspector.m_TopLeft.y, Inspector.Width(), Inspector.Height());

		float iy = Inspector.m_TopLeft.y + 6.0f;
		const float FieldWidth = Inspector.Width() - 12.0f;
		const auto Label = [&](const char *pText) {
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Inspector.m_TopLeft.x + 6.0f, iy + 10.0f), 11.0f, ui::Theme.m_TextDim, pText);
			iy += 14.0f;
		};
		const auto Field = [&](const base::rect &Box) { return Box; };

		if(m_RuleConfig < 0 || m_RuleConfig >= static_cast<int>(m_RuleSet.m_vConfigs.size()))
		{
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Inspector.m_TopLeft.x + 8.0f, iy + 12.0f), 12.0f,
				ui::Theme.m_TextDim, "nothing selected", FieldWidth);
		}
		else
		{
			rules::CConfiguration &Config = m_RuleSet.m_vConfigs[static_cast<std::size_t>(m_RuleConfig)];
			Label("configuration name");
			TextField(pPainter, Input, m_pGraphics->Font(), Field(base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth, 20.0f))),
				Config.m_Name, "unnamed");
			iy += 24.0f;

			Label("region  x, y, width, height  (0 0 0 0 is the whole layer)");
			const int aValues[4] = {Config.m_StartX, Config.m_StartY, Config.m_EndX - Config.m_StartX, Config.m_EndY - Config.m_StartY};
			for(int i = 0; i < 4; i++)
			{
				const base::rect Box = base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f + i * (FieldWidth * 0.25f), iy), base::vec2(FieldWidth * 0.25f - 3.0f, 20.0f));
				ui::Rect(pPainter, Box, ui::Theme.m_Panel);
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Box.Center().x, Box.Center().y + 4.5f), 12.0f, ui::Theme.m_Text,
					std::to_string(aValues[i]), ui::text_align::center);
			}
			iy += 26.0f;

			if(m_RuleRun >= 0 && m_RuleRun < static_cast<int>(Config.m_vRuns.size()))
			{
				rules::CRun &Run = Config.m_vRuns[static_cast<std::size_t>(m_RuleRun)];
				Label("run name");
				TextField(pPainter, Input, m_pGraphics->Font(), Field(base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth, 20.0f))),
					Run.m_Name, "unnamed");
				iy += 24.0f;

				Label("reads  /  writes");
				const char *apSlots[] = {"game", "tele", "speedup", "front", "switch", "tune", "tiles"};
				const int SlotCount = static_cast<int>(std::size(apSlots));
				if(ui::Button(pPainter, Input, m_pGraphics->Font(),
					base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth * 0.5f - 4.0f, 20.0f)),
					RuleSlotName(Run.m_ReadSlot)))
					Run.m_ReadSlot = (Run.m_ReadSlot + 1) % SlotCount;
				if(ui::Button(pPainter, Input, m_pGraphics->Font(),
					base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f + FieldWidth * 0.5f, iy), base::vec2(FieldWidth * 0.5f, 20.0f)),
					Run.m_TargetSlot < 0 ? "this layer" : RuleSlotName(Run.m_TargetSlot)))
					Run.m_TargetSlot = Run.m_TargetSlot + 1 >= SlotCount ? -1 : Run.m_TargetSlot + 1;
				iy += 24.0f;

				Label("automap reference");
				if(ui::Button(pPainter, Input, m_pGraphics->Font(),
					base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth, 20.0f)),
					rules::AutomapReferenceName(Run.m_Reference)))
					Run.m_Reference = (Run.m_Reference + 1) % rules::NumAutomapReferences;
				iy += 24.0f;

				// the result only says whether the box was clicked; the row
				// advances either way, so the label cannot land on the next field
				ui::Checkbox(pPainter, Input,
					base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth, 18.0f)), Run.m_bCopy);
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Inspector.m_TopLeft.x + 28.0f, iy + 12.0f), 11.5f,
					ui::Theme.m_Text, "copy the read layer before writing");
				iy += 22.0f;

				if(m_RuleIndex >= 0 && m_RuleIndex < static_cast<int>(Run.m_vIndexRules.size()))
				{
					rules::CIndexRule &Rule = Run.m_vIndexRules[static_cast<std::size_t>(m_RuleIndex)];
					Label("rule name  /  writes tile");
					TextField(pPainter, Input, m_pGraphics->Font(),
						Field(base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth * 0.62f - 3.0f, 20.0f))),
						Rule.m_Name, "unnamed");
					if(ui::Button(pPainter, Input, m_pGraphics->Font(),
						base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f + FieldWidth * 0.62f, iy), base::vec2(FieldWidth * 0.38f, 20.0f)),
						base::Format("%d", Rule.m_Tile)))
						Rule.m_Tile = (Rule.m_Tile + 1) % 256;
					iy += 24.0f;

					Label("flags");
					const char *apFlags[] = {"hflip", "vflip", "rotate"};
					const int aBits[] = {TILEFLAG_HFLIP, TILEFLAG_VFLIP, TILEFLAG_ROTATE};
					for(int i = 0; i < 3; i++)
					{
						const base::rect Box = base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f + i * (FieldWidth / 3.0f), iy), base::vec2(FieldWidth / 3.0f - 4.0f, 20.0f));
						if(ui::Button(pPainter, Input, m_pGraphics->Font(), Box, apFlags[i], (Rule.m_Flag & aBits[i]) != 0))
							Rule.m_Flag ^= aBits[i];
					}
					iy += 24.0f;

					Label("conditions and randomness");
					ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Inspector.m_TopLeft.x + 6.0f, iy + 10.0f), 11.0f,
						ui::Theme.m_TextDim, base::Format("%d position rules, %d modulo rules", static_cast<int>(Rule.m_vRules.size()),
							static_cast<int>(Rule.m_vModuloRules.size())), FieldWidth);
					iy += 16.0f;
					float Random = Rule.m_RandomProbability;
					if(ui::Slider(pPainter, Input, base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth - 44.0f, 16.0f)),
						Random, 0.0f, 1.0f))
						Rule.m_RandomProbability = Random;
					ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Inspector.m_BottomRight.x - 6.0f, iy + 12.0f), 11.0f,
						ui::Theme.m_TextDim, base::Format("%.2f", Rule.m_RandomProbability), ui::text_align::right);
					iy += 22.0f;

					if(ui::Button(pPainter, Input, m_pGraphics->Font(),
						base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f, iy), base::vec2(FieldWidth * 0.5f - 3.0f, 20.0f)),
						Rule.m_bSkipEmpty ? "skip empty: on" : "skip empty: off"))
						Rule.m_bSkipEmpty = !Rule.m_bSkipEmpty;
					if(ui::Button(pPainter, Input, m_pGraphics->Font(),
						base::rect::FromSize(base::vec2(Inspector.m_TopLeft.x + 6.0f + FieldWidth * 0.5f, iy), base::vec2(FieldWidth * 0.5f, 20.0f)),
						Rule.m_bSkipFull ? "skip full: on" : "skip full: off"))
						Rule.m_bSkipFull = !Rule.m_bSkipFull;
					iy += 24.0f;
				}
			}
		}
		pPainter->Restore();
	}

	// ------------------------------------------------------------------ render
	void CEditor::Render()
	{
		ui::CPainter *pPainter = m_pGraphics->Painter();
		const base::vec2 Size = WindowSize();
		// minimized: there is nothing to draw into
		if(Size.x < 1.0f || Size.y < 1.0f)
			return;

		ui::Rect(pPainter, base::rect::FromSize(base::vec2(0.0f, 0.0f), Size), ui::Theme.m_Background);
		// the hint of the previous frame is stale: whatever the cursor is over
		// now asks for a new one while the panels below draw
		m_bTooltip = false;
		/*
			An open menu is drawn over the toolbar and the panels under it, and
			an immediate mode interface has no z order: the mouse is blocked for
			the frame so nothing behind the menu answers the click that was
			meant for it. The menu bar and the menu itself read the cursor
			directly, because they are what the block is there for.
		*/
		engine::CInput &Input = m_pGraphics->Input();
		Input.SetMouseBlocked(m_Menu != menu_id::none);

		const base::rect Canvas = CanvasRect(Size);
		DrawMap(pPainter, Canvas);
		// the docks' own frame work, between the canvas and the headers
		HandleDockInput(Size);
		// a platform dialog that finished during BeginFrame() lands here
		PollFileDialog();

		// the two rows of chrome: the menu bar, which is the title bar of a
		// window that draws its own, and the toolbar under it
		DrawMenuBar(pPainter, base::rect::FromSize(base::vec2(0.0f, 0.0f), base::vec2(Size.x, MenuBarHeight)));
		DrawToolbar(pPainter,
			base::rect::FromSize(base::vec2(0.0f, MenuBarHeight), base::vec2(Size.x, ToolbarHeight)));
		// the tool strip runs under the toolbar and past both docks, so a dock
		// can be collapsed without the tools moving
		DrawToolBox(pPainter, ToolBoxRect(Size));

		for(const dock_slot Slot : {dock_slot::right_upper, dock_slot::right_lower})
		{
			const base::rect Area = DockArea(Slot, Size);
			if(Area.Width() <= 1.0f)
				continue;
			ui::Rect(pPainter, Area, ui::Theme.m_Panel);
			// the seam against the canvas
			ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y),
				base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);
		}
		// the two splitters light up while they are grabbable, which is how a
		// dock edge announces that it can be dragged
		{
			const base::rect Column = DockSplitter(dock_slot::right_upper, Size);
			if(Column.Width() > 0.0f && m_bSplitterHover)
			{
				ui::Rect(pPainter, Column, ui::Theme.m_AccentDim);
				Hint(Column, "resize", "drag sideways to widen or narrow the docker column");
			}
			const base::rect Split = DockSplitterH(Size);
			if(Split.Height() > 0.0f && m_bSplitterHoverH)
			{
				ui::Rect(pPainter, Split, ui::Theme.m_AccentDim);
				Hint(Split, "resize", "drag up or down to share the column between the two dockers");
			}
		}

		// the upper docker, then the lower one, and the layer tree is the one
		// that lives at the bottom
		const base::rect UpperContent = DockContent(dock_slot::right_upper, Size);
		if(UpperContent.Width() > 1.0f)
			DrawDockContent(pPainter, DockPanel(dock_slot::right_upper), UpperContent, false);
		const base::rect LowerContent = DockContent(dock_slot::right_lower, Size);
		if(LowerContent.Width() > 1.0f)
			DrawDockContent(pPainter, DockPanel(dock_slot::right_lower), LowerContent, true);

		DrawDockHeaders(pPainter, Size);
		DrawStatusBar(pPainter, base::rect::FromSize(base::vec2(0.0f, Size.y - StatusHeight), base::vec2(Size.x, StatusHeight)));

		// the floating panels are not docks, so they sit above everything the
		// layout drew and change nothing about it
		if(m_bShowWelcome)
			DrawWelcomePanel(pPainter, Canvas);
		if(m_bShowHelp || m_bShowShortcuts)
			DrawShortcutsPanel(pPainter, Canvas);

		DrawPopups(pPainter);
		// last of all, so a hint is never covered by the panel it describes
		DrawTooltip(pPainter, Size);
	}

	void CEditor::DrawDockContent(ui::CPainter *pPainter, dock_panel Panel, const base::rect &Area, bool bLower)
	{
		switch(Panel)
		{
		case dock_panel::layers: DrawLayerPanel(pPainter, Area); return;
		case dock_panel::tiles: DrawPalettePanel(pPainter, Area); return;
		case dock_panel::assets: DrawAssetsPanel(pPainter, Area); return;
		case dock_panel::envelopes: DrawEnvelopeWorkspace(pPainter, Area); return;
		case dock_panel::rules: DrawRulesWorkspace(pPainter, Area); return;
		case dock_panel::count: break;
		}
		// an unknown workspace in the lower docker falls back to the layer tree,
		// which is the one it is meant to hold
		if(bLower)
			DrawLayerPanel(pPainter, Area);
		else
			DrawPalettePanel(pPainter, Area);
	}
} // namespace game
