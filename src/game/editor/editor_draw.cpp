/*
	All the drawing of the editor, in one immediate mode pass: the map under the
	camera, then the panels on top of it.
*/

#include <algorithm>
#include <cstring>

#include <GLFW/glfw3.h>

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

		// draws one tile of a tile sheet. The sheet is a 16 by 16 grid, so the
		// pattern is offset by whole destination tiles.
		void DrawTile(NVGcontext *pContext, int Texture, int Index, int Flags, float X, float Y, float Size)
		{
			if(Texture <= 0 || Index <= 0)
				return;
			const int Col = Index % 16;
			const int Row = Index / 16;

			nvgSave(pContext);
			nvgTranslate(pContext, X, Y);
			if(Flags & TILEFLAG_ROTATE)
			{
				nvgTranslate(pContext, Size, 0.0f);
				nvgRotate(pContext, NVG_PI * 0.5f);
			}
			if(Flags & TILEFLAG_HFLIP)
			{
				nvgTranslate(pContext, Size, 0.0f);
				nvgScale(pContext, -1.0f, 1.0f);
			}
			if(Flags & TILEFLAG_VFLIP)
			{
				nvgTranslate(pContext, 0.0f, Size);
				nvgScale(pContext, 1.0f, -1.0f);
			}
			// the whole 16x16 sheet spans 16 * Size, so the wanted cell starts at
			// -Col * Size
			const NVGpaint Paint = nvgImagePattern(pContext, -Col * Size, -Row * Size, Size * 16.0f, Size * 16.0f, 0.0f, Texture, 1.0f);
			nvgBeginPath(pContext);
			nvgRect(pContext, 0.0f, 0.0f, Size, Size);
			nvgFillPaint(pContext, Paint);
			nvgFill(pContext);
			nvgRestore(pContext);
		}

		NVGcolor EntityColor(int Entity)
		{
			switch(Entity)
			{
			case ENTITY_SPAWN: return nvgRGB(230, 230, 230);
			case ENTITY_SPAWN_RED: return nvgRGB(235, 90, 90);
			case ENTITY_SPAWN_BLUE: return nvgRGB(95, 140, 245);
			case ENTITY_FLAGSTAND_RED: return nvgRGB(200, 60, 60);
			case ENTITY_FLAGSTAND_BLUE: return nvgRGB(60, 90, 200);
			case ENTITY_ARMOR_1: return nvgRGB(240, 220, 120);
			case ENTITY_HEALTH_1: return nvgRGB(120, 230, 120);
			case ENTITY_WEAPON_SHOTGUN: return nvgRGB(190, 150, 90);
			case ENTITY_WEAPON_GRENADE: return nvgRGB(120, 200, 160);
			case ENTITY_POWERUP_NINJA: return nvgRGB(90, 90, 110);
			case ENTITY_WEAPON_LASER: return nvgRGB(120, 190, 230);
			default: return nvgRGB(220, 150, 240);
			}
		}

		const char *EntityName(int Entity)
		{
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
				if(Entry.first == Entity)
					return Entry.second;
			return "entity";
		}
	} // namespace

	// ------------------------------------------------------------------ canvas
	void CEditor::DrawTileLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer, bool Selected)
	{
		const int Texture = TextureForImage(Layer.m_Image);
		const base::rect World = m_View.VisibleWorld();

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
					const base::vec2 Screen = m_View.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
					ui::Rect(pContext, base::rect::FromSize(Screen, base::vec2(TileSize * m_View.m_Zoom, TileSize * m_View.m_Zoom)), nvgRGBA(150, 150, 160, 90));
				}
			return;
		}

		// a color tint on the whole layer
		if(Layer.m_Color.a != 255 && Layer.m_Color.a > 0 && Selected)
		{
			const base::vec2 TopLeft = m_View.WorldToScreen(base::vec2(0.0f, 0.0f));
			ui::Rect(pContext, base::rect::FromSize(TopLeft, base::vec2(Layer.m_Width * TileSize * m_View.m_Zoom, Layer.m_Height * TileSize * m_View.m_Zoom)),
				nvgRGBA(static_cast<unsigned char>(Layer.m_Color.r), static_cast<unsigned char>(Layer.m_Color.g), static_cast<unsigned char>(Layer.m_Color.b),
					static_cast<unsigned char>(255 - Layer.m_Color.a)));
		}

		const float Size = TileSize * m_View.m_Zoom;
		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_View.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				DrawTile(pContext, Texture, pTile->m_Index, pTile->m_Flags, Screen.x, Screen.y, Size);
			}
		}
	}

	void CEditor::DrawGameLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer)
	{
		(void)Canvas;
		const base::rect World = m_View.VisibleWorld();
		const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
		const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
		const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);
		const float Size = TileSize * m_View.m_Zoom;
		const bool bLabel = Size >= 14.0f;

		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_View.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				const base::rect Cell = base::rect::FromSize(Screen, base::vec2(Size, Size));
				if(pTile->m_Index >= ENTITY_OFFSET)
				{
					const int Entity = pTile->m_Index - ENTITY_OFFSET;
					const NVGcolor Color = EntityColor(Entity);
					ui::Rect(pContext, base::rect::FromSize(Screen + base::vec2(Size * 0.15f, Size * 0.15f), base::vec2(Size * 0.7f, Size * 0.7f)), Color);
					if(bLabel)
						ui::TextClipped(pContext, m_pGraphics->Font(), base::vec2(Screen.x + Size * 0.5f, Screen.y + Size * 0.62f), std::min(11.0f, Size * 0.45f),
							nvgRGB(20, 20, 24), std::to_string(Entity), Size, ui::text_align::center);
				}
				else
				{
					// a tile controller index, the client decides what it means
					ui::RectOutline(pContext, Cell, nvgRGBA(255, 255, 255, 60));
					if(bLabel)
						ui::Text(pContext, m_pGraphics->Font(), base::vec2(Screen.x + Size * 0.5f, Screen.y + Size * 0.62f), std::min(11.0f, Size * 0.45f),
							ui::Theme.m_TextDim, std::to_string(pTile->m_Index), ui::text_align::center);
				}
			}
		}
	}

	void CEditor::DrawPhysicsLayer(NVGcontext *pContext, const base::rect &Canvas, const CLayer &Layer, int Flags)
	{
		(void)Canvas;
		const base::rect World = m_View.VisibleWorld();
		const int x0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.x / TileSize)) - 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(World.m_TopLeft.y / TileSize)) - 1);
		const int x1 = std::min(Layer.m_Width, static_cast<int>(std::ceil(World.m_BottomRight.x / TileSize)) + 1);
		const int y1 = std::min(Layer.m_Height, static_cast<int>(std::ceil(World.m_BottomRight.y / TileSize)) + 1);
		const float Size = TileSize * m_View.m_Zoom;
		const NVGcolor Color = Flags == TILESLAYERFLAG_TELE ? nvgRGBA(120, 200, 255, 150) :
			Flags == TILESLAYERFLAG_SPEEDUP ? nvgRGBA(255, 210, 90, 150) :
			Flags == TILESLAYERFLAG_SWITCH ? nvgRGBA(190, 130, 255, 150) :
			Flags == TILESLAYERFLAG_TUNE ? nvgRGBA(255, 130, 200, 150) :
			nvgRGBA(140, 255, 160, 130);

		for(int y = y0; y < y1; y++)
		{
			for(int x = x0; x < x1; x++)
			{
				const CTile *pTile = Layer.Tile(x, y);
				if(pTile == nullptr || pTile->m_Index == 0)
					continue;
				const base::vec2 Screen = m_View.WorldToScreen(base::vec2(x * TileSize, y * TileSize));
				ui::RectOutline(pContext, base::rect::FromSize(Screen, base::vec2(Size, Size)), Color, 2.0f);
			}
		}
	}

	void CEditor::DrawQuadLayer(NVGcontext *pContext, const CLayer &Layer)
	{
		for(const CQuad &Quad : Layer.m_vQuads)
		{
			nvgBeginPath(pContext);
			for(int i = 0; i < 5; i++)
			{
				const base::vec2 Screen = m_View.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[i].x), base::FixedToFloat(Quad.m_aPoints[i].y)));
				if(i == 0)
					nvgMoveTo(pContext, Screen.x, Screen.y);
				else
					nvgLineTo(pContext, Screen.x, Screen.y);
			}
			nvgClosePath(pContext);
			const int Alpha = base::clamp(Quad.m_aColors[0].a / 8, 12, 90);
			nvgFillColor(pContext, nvgRGBA(static_cast<unsigned char>(base::clamp(Quad.m_aColors[0].r / 4, 40, 255)),
				static_cast<unsigned char>(base::clamp(Quad.m_aColors[0].g / 4, 40, 255)),
				static_cast<unsigned char>(base::clamp(Quad.m_aColors[0].b / 4, 40, 255)), static_cast<unsigned char>(Alpha)));
			nvgFill(pContext);
			nvgStrokeColor(pContext, nvgRGBA(200, 210, 230, 90));
			nvgStrokeWidth(pContext, 1.0f);
			nvgStroke(pContext);
		}
	}

	void CEditor::DrawCursor(NVGcontext *pContext, const base::rect &Canvas)
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
		ui::RectOutline(pContext, Cursor, ui::Theme.m_Accent, 2.0f);
	}

	void CEditor::DrawMap(NVGcontext *pContext, const base::rect &Canvas)
	{
		nvgSave(pContext);
		nvgScissor(pContext, Canvas.m_TopLeft.x, Canvas.m_TopLeft.y, Canvas.Width(), Canvas.Height());
		ui::Rect(pContext, Canvas, nvgRGB(18, 19, 23));

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
			nvgBeginPath(pContext);
			nvgRect(pContext, Canvas.m_TopLeft.x, Canvas.m_TopLeft.y, Canvas.Width(), Canvas.Height());
			nvgPathWinding(pContext, NVG_HOLE);
			nvgRect(pContext, BoundsTopLeft.x, BoundsTopLeft.y, BoundsBottomRight.x - BoundsTopLeft.x, BoundsBottomRight.y - BoundsTopLeft.y);
			nvgFillColor(pContext, nvgRGBA(0, 0, 0, 110));
			nvgFill(pContext);
		}

		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
		{
			const CGroup &Group = m_Map.m_vGroups[g];
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
						DrawGameLayer(pContext, Canvas, Layer);
					else if(Layer.IsPhysicsLayer())
						DrawPhysicsLayer(pContext, Canvas, Layer, Layer.m_Flags);
					else
						DrawTileLayer(pContext, Canvas, Layer, Selected);
				}
				else if(Layer.IsQuads())
				{
					DrawQuadLayer(pContext, Layer);
				}
			}
		}

		// The grid belongs to the tilemap, so it is clipped to the map boundary
		// and never suggested outside of it.
		if(m_bGrid && bHasBounds && m_View.m_Zoom > 0.15f)
		{
			const base::rect Area = m_View.VisibleWorld().Intersect(Bounds);
			if(Area.Width() > 0.0f && Area.Height() > 0.0f)
			{
				const NVGcolor Grid = nvgRGBA(255, 255, 255, 14);
				for(float x = std::floor(Area.m_TopLeft.x / TileSize) * TileSize; x < Area.m_BottomRight.x; x += TileSize)
				{
					const base::vec2 From = m_View.WorldToScreen(base::vec2(x, Area.m_TopLeft.y));
					const base::vec2 To = m_View.WorldToScreen(base::vec2(x, Area.m_BottomRight.y));
					ui::Line(pContext, From, To, Grid);
				}
				for(float y = std::floor(Area.m_TopLeft.y / TileSize) * TileSize; y < Area.m_BottomRight.y; y += TileSize)
				{
					const base::vec2 From = m_View.WorldToScreen(base::vec2(Area.m_TopLeft.x, y));
					const base::vec2 To = m_View.WorldToScreen(base::vec2(Area.m_BottomRight.x, y));
					ui::Line(pContext, From, To, Grid);
				}
			}
		}

		// the boundary itself, on top of the grid so it stays readable
		if(bHasBounds)
			ui::RectOutline(pContext, base::rect(BoundsTopLeft, BoundsBottomRight), nvgRGBA(120, 132, 154, 220), 2.0f);

		DrawCursor(pContext, Canvas);
		nvgRestore(pContext);
	}

	// ---------------------------------------------------------------- toolbar
	void CEditor::DrawToolbar(NVGcontext *pContext, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pContext, Area, ui::Theme.m_Panel);
		ui::Line(pContext, base::vec2(Area.m_TopLeft.x, Area.m_BottomRight.y), base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y), ui::Theme.m_Border);

		float x = 8.0f;
		const float Y = Area.m_TopLeft.y + 8.0f;
		const float H = 24.0f;
		const auto NextButton = [&x, &pContext](const std::string &Label) {
			const float Width = std::max(46.0f, ui::Measure(pContext, 0, 13.0f, Label) + 22.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, 0.0f), base::vec2(Width, 24.0f));
			x += Width + 6.0f;
			return Rect;
		};
		(void)NextButton;

		const auto Button = [&](const std::string &Label, bool Active = false, bool Enabled = true) {
			const float Width = std::max(46.0f, ui::Measure(pContext, m_pGraphics->Font(), 13.0f, Label) + 22.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, Y), base::vec2(Width, H));
			x += Width + 6.0f;
			return ui::Button(pContext, Input, m_pGraphics->Font(), Rect, Label, Active, Enabled);
		};
		const auto Separator = [&]() {
			ui::Line(pContext, base::vec2(x + 3.0f, Y), base::vec2(x + 3.0f, Y + H), ui::Theme.m_Border);
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
		int TargetIndex = 0;
		for(const map_target Target : AllTargets())
		{
			const CMapTargetProfile &Profile = GetTargetProfile(Target);
			if(Button(Profile.m_pId, m_Target == Target))
				m_Target = Target;
			TargetIndex++;
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
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 12.0f, Y + 16.0f), 13.0f, ui::Theme.m_TextDim, Title, ui::text_align::right);
	}

	// ------------------------------------------------------------ layer panel
	void CEditor::DrawLayerPanel(NVGcontext *pContext, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pContext, Area, ui::Theme.m_Panel);
		ui::Line(pContext, base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_TopLeft.y), base::vec2(Area.m_BottomRight.x - 1.0f, Area.m_BottomRight.y), ui::Theme.m_Border);

		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, Area.m_TopLeft.y + 22.0f), 14.0f, ui::Theme.m_Text, "Layers");

		const base::rect List = base::rect(
			base::vec2(Area.m_TopLeft.x, Area.m_TopLeft.y + 32.0f),
			base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y - 40.0f));

		nvgSave(pContext);
		nvgScissor(pContext, List.m_TopLeft.x, List.m_TopLeft.y, List.Width(), List.Height());

		float y = List.m_TopLeft.y - m_LayerScroll;
		for(std::size_t g = 0; g < m_Map.m_vGroups.size(); g++)
		{
			CGroup &Group = m_Map.m_vGroups[g];
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 4.0f, y), base::vec2(List.Width() - 8.0f, RowHeight));
			if(Row.m_BottomRight.y > List.m_TopLeft.y && Row.m_TopLeft.y < List.m_BottomRight.y)
			{
				ui::Rect(pContext, Row, nvgRGBA(255, 255, 255, 8));
				const std::string Label = Group.m_Name.empty() ? ("group " + std::to_string(g)) : Group.m_Name;
				ui::TextClipped(pContext, m_pGraphics->FontBold(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.m_TopLeft.y + 15.0f), 12.5f, ui::Theme.m_Text, Label, Row.Width() - 16.0f);
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
						ui::Rect(pContext, LayerRow, ui::Theme.m_Selection);
					else if(ui::Hovered(Input, LayerRow))
						ui::Rect(pContext, LayerRow, ui::Theme.m_Hover);

					if(pView != nullptr)
					{
						bool Visible = pView->m_Visible;
						const base::rect Eye = base::rect::FromSize(base::vec2(LayerRow.m_TopLeft.x + 3.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(12.0f, 12.0f));
						if(ui::Checkbox(pContext, Input, Eye, Visible))
							pView->m_Visible = Visible;
						bool Locked = pView->m_Locked;
						const base::rect Lock = base::rect::FromSize(base::vec2(LayerRow.m_TopLeft.x + 19.0f, LayerRow.m_TopLeft.y + 5.0f), base::vec2(12.0f, 12.0f));
						if(ui::Checkbox(pContext, Input, Lock, Locked))
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
					const NVGcolor TextColor = (pView != nullptr && !pView->m_Visible) ? ui::Theme.m_TextDim : ui::Theme.m_Text;
					ui::TextClipped(pContext, m_pGraphics->Font(), base::vec2(LayerRow.m_TopLeft.x + 36.0f, LayerRow.m_TopLeft.y + 15.0f), 12.0f, TextColor, Label, LayerRow.Width() - 40.0f);

					if(ui::Hovered(Input, LayerRow) && Input.MousePressed(engine::CInput::mouse_button::left))
					{
						m_SelectedGroup = static_cast<int>(g);
						m_SelectedLayer = static_cast<int>(l);
					}
				}
				y += RowHeight;
			}
		}
		nvgRestore(pContext);

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
			const float Width = std::max(56.0f, ui::Measure(pContext, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(ButtonX, ButtonY), base::vec2(Width, 24.0f));
			ButtonX += Width + 6.0f;
			return ui::Button(pContext, Input, m_pGraphics->Font(), Rect, Label);
		};
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
	}

	// ---------------------------------------------------------- palette panel
	void CEditor::DrawPalettePanel(NVGcontext *pContext, const base::rect &Area)
	{
		engine::CInput &Input = m_pGraphics->Input();
		ui::Rect(pContext, Area, ui::Theme.m_Panel);
		ui::Line(pContext, base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_TopLeft.y), base::vec2(Area.m_TopLeft.x + 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);

		float y = Area.m_TopLeft.y + 10.0f;
		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 14.0f, ui::Theme.m_Text, "Brush");
		y += 26.0f;

		float x = Area.m_TopLeft.x + 12.0f;
		const auto SmallButton = [&](const std::string &Label, bool Active, float Width = 0.0f) {
			const float W = Width > 0.0f ? Width : std::max(50.0f, ui::Measure(pContext, m_pGraphics->Font(), 12.0f, Label) + 16.0f);
			const base::rect Rect = base::rect::FromSize(base::vec2(x, y), base::vec2(W, 24.0f));
			x += W + 6.0f;
			return ui::Button(pContext, Input, m_pGraphics->Font(), Rect, Label, Active);
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
			if(ui::Checkbox(pContext, Input, Box, HFlip))
				m_BrushFlags ^= TILEFLAG_HFLIP;
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "hflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Checkbox(pContext, Input, Box, VFlip))
				m_BrushFlags ^= TILEFLAG_VFLIP;
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "vflip");
			x += 72.0f;
		}
		{
			const base::rect Box = base::rect::FromSize(base::vec2(x, y + 4.0f), base::vec2(14.0f, 14.0f));
			if(ui::Checkbox(pContext, Input, Box, Rotate))
				m_BrushFlags ^= TILEFLAG_ROTATE;
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(x + 20.0f, y + 15.0f), 12.0f, ui::Theme.m_Text, "rot");
		}
		y += 32.0f;

		const CLayer *pLayer = SelectedLayer();
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.5f, ui::Theme.m_TextDim,
			base::Format("tool %s   tile %d   flags %d", m_Tool == editor_tool::paint ? "paint" : (m_Tool == editor_tool::erase ? "erase" : "pick"),
				m_BrushTile, m_BrushFlags));
		y += 24.0f;
		ui::Line(pContext, base::vec2(Area.m_TopLeft.x + 8.0f, y), base::vec2(Area.m_BottomRight.x - 8.0f, y), ui::Theme.m_Border);
		y += 10.0f;

		if(pLayer != nullptr && pLayer->IsGameLayer())
		{
			ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, "Entities");
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
				if(ui::Button(pContext, Input, m_pGraphics->Font(), Rect, Entry.second, m_BrushTile == Index))
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
			ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 13.0f, ui::Theme.m_Text, Name);
			y += 22.0f;
			const float Sheet = std::min(Area.Width() - 24.0f, Area.m_BottomRight.y - y - 12.0f);
			if(Sheet > 32.0f)
			{
				const base::vec2 TopLeft(Area.m_TopLeft.x + 12.0f, y);
				const base::rect SheetRect = base::rect::FromSize(TopLeft, base::vec2(Sheet, Sheet));
				if(Texture > 0)
				{
					const NVGpaint Paint = nvgImagePattern(pContext, TopLeft.x, TopLeft.y, Sheet, Sheet, 0.0f, Texture, 1.0f);
					nvgBeginPath(pContext);
					nvgRect(pContext, TopLeft.x, TopLeft.y, Sheet, Sheet);
					nvgFillPaint(pContext, Paint);
					nvgFill(pContext);
				}
				else
				{
					ui::Rect(pContext, SheetRect, ui::Theme.m_PanelDark);
					ui::Text(pContext, m_pGraphics->Font(), base::vec2(SheetRect.Center().x, SheetRect.Center().y), 12.0f, ui::Theme.m_Warning,
						"texture missing", ui::text_align::center);
				}
				ui::RectOutline(pContext, SheetRect, ui::Theme.m_Border);

				const float Cell = Sheet / 16.0f;
				const int Col = m_BrushTile % 16;
				const int Row = m_BrushTile / 16;
				ui::RectOutline(pContext, base::rect::FromSize(base::vec2(TopLeft.x + Col * Cell, TopLeft.y + Row * Cell), base::vec2(Cell, Cell)), ui::Theme.m_Accent, 2.0f);

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

		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, y + 12.0f), 12.0f, ui::Theme.m_TextDim,
			"select a tile layer to see its palette");
	}

	// ------------------------------------------------------------- status bar
	void CEditor::DrawStatusBar(NVGcontext *pContext, const base::rect &Area)
	{
		ui::Rect(pContext, Area, ui::Theme.m_PanelDark);
		ui::Line(pContext, Area.m_TopLeft, base::vec2(Area.m_BottomRight.x, Area.m_TopLeft.y), ui::Theme.m_Border);

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
		const NVGcolor MessageColor = Age < 6.0 ? ui::Theme.m_Accent : ui::Theme.m_TextDim;
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Area.m_TopLeft.x + 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, ui::Theme.m_TextDim, Info);
		ui::TextClipped(pContext, m_pGraphics->Font(), base::vec2(Area.m_BottomRight.x - 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, MessageColor,
			m_Status, Area.Width() * 0.5f, ui::text_align::right);

		if(m_Dirty)
			ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Area.m_BottomRight.x - 12.0f, Area.m_TopLeft.y + 17.0f), 12.0f, ui::Theme.m_Warning, "", ui::text_align::right);
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

		if(Input.KeyPressed(GLFW_KEY_ESCAPE))
		{
			m_bShowOpen = m_bShowSave = m_bShowExport = m_bShowHelp = false;
			return;
		}

		if(m_bShowOpen)
		{
			if(Input.KeyPressed(GLFW_KEY_DOWN))
				m_BrowserSelected = std::min(m_BrowserSelected + 1, static_cast<int>(m_vBrowserEntries.size()) - 1);
			if(Input.KeyPressed(GLFW_KEY_UP))
				m_BrowserSelected = std::max(m_BrowserSelected - 1, 0);
			if(Input.KeyPressed(GLFW_KEY_ENTER) || Input.KeyPressed(GLFW_KEY_KP_ENTER))
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
			if(Input.KeyPressed(GLFW_KEY_TAB) || Input.KeyPressed(GLFW_KEY_DOWN))
				m_ExportTarget = (m_ExportTarget + 1) % static_cast<int>(AllTargets().size());
			if(Input.KeyPressed(GLFW_KEY_UP))
				m_ExportTarget = (m_ExportTarget + static_cast<int>(AllTargets().size()) - 1) % static_cast<int>(AllTargets().size());
			m_TextInput += Input.TypedText();
			if(Input.BackspacePressed() && !m_TextInput.empty())
				m_TextInput.pop_back();
			if(Input.KeyPressed(GLFW_KEY_ENTER) || Input.KeyPressed(GLFW_KEY_KP_ENTER))
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
			if(Input.KeyPressed(GLFW_KEY_ENTER) || Input.KeyPressed(GLFW_KEY_KP_ENTER))
			{
				std::string Path = m_TextInput;
				if(!base::EndsWith(Path, CUnitwpPackage::Extension))
					Path += CUnitwpPackage::Extension;
				m_bShowSave = false;
				SavePackage(Path);
			}
		}
	}

	void CEditor::DrawOpenPopup(NVGcontext *pContext)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 300.0f, Size.y * 0.5f - 240.0f), base::vec2(600.0f, 480.0f));
		ui::Rect(pContext, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pContext, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Open a map or package");
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_BottomRight.x - 16.0f, Popup.m_TopLeft.y + 26.0f), 11.0f, ui::Theme.m_TextDim,
			"up/down then enter, esc to cancel", ui::text_align::right);

		const base::rect List = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 12.0f, Popup.m_TopLeft.y + 40.0f), base::vec2(Popup.Width() - 24.0f, Popup.Height() - 60.0f));
		ui::Rect(pContext, List, ui::Theme.m_PanelDark);
		nvgSave(pContext);
		nvgScissor(pContext, List.m_TopLeft.x, List.m_TopLeft.y, List.Width(), List.Height());
		float y = List.m_TopLeft.y + 4.0f;
		for(std::size_t i = 0; i < m_vBrowserEntries.size(); i++)
		{
			const base::rect Row = base::rect::FromSize(base::vec2(List.m_TopLeft.x + 4.0f, y), base::vec2(List.Width() - 8.0f, 22.0f));
			if(Row.m_BottomRight.y > List.m_TopLeft.y && Row.m_TopLeft.y < List.m_BottomRight.y)
			{
				if(static_cast<int>(i) == m_BrowserSelected)
					ui::Rect(pContext, Row, ui::Theme.m_Selection);
				else if(ui::Hovered(Input, Row))
					ui::Rect(pContext, Row, ui::Theme.m_Hover);
				ui::TextClipped(pContext, m_pGraphics->Font(), base::vec2(Row.m_TopLeft.x + 8.0f, Row.m_TopLeft.y + 15.0f), 12.5f, ui::Theme.m_Text,
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
					nvgRestore(pContext);
					return;
				}
			}
			y += 22.0f;
		}
		nvgRestore(pContext);

		if(m_vBrowserEntries.empty())
			ui::Text(pContext, m_pGraphics->Font(), List.Center(), 13.0f, ui::Theme.m_TextDim,
				"no maps found in <data>/maps or the working directory", ui::text_align::center);
	}

	void CEditor::DrawExportPopup(NVGcontext *pContext)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 260.0f, Size.y * 0.5f - 160.0f), base::vec2(520.0f, 320.0f));
		ui::Rect(pContext, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pContext, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Export the map");
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_BottomRight.x - 16.0f, Popup.m_TopLeft.y + 26.0f), 11.0f, ui::Theme.m_TextDim,
			"up/down picks the version, enter writes <name>.map", ui::text_align::right);

		float y = Popup.m_TopLeft.y + 44.0f;
		int Index = 0;
		for(const map_target Target : AllTargets())
		{
			const CMapTargetProfile &Profile = GetTargetProfile(Target);
			const base::rect Row = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 14.0f, y), base::vec2(Popup.Width() - 28.0f, 28.0f));
			const bool Active = Index == m_ExportTarget;
			if(ui::Button(pContext, Input, m_pGraphics->Font(), Row, Profile.m_pName, Active))
				m_ExportTarget = Index;
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(Row.m_BottomRight.x - 12.0f, Row.Center().y + 4.0f), 11.0f, ui::Theme.m_TextDim,
				base::Format("datafile v%d", Profile.m_DatafileVersion), ui::text_align::right);
			y += 32.0f;
			Index++;
		}

		y += 6.0f;
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 4.0f), 12.0f, ui::Theme.m_TextDim, "file name");
		const base::rect Field = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 14.0f, y + 10.0f), base::vec2(Popup.Width() - 28.0f, 26.0f));
		ui::Rect(pContext, Field, ui::Theme.m_PanelDark);
		ui::RectOutline(pContext, Field, ui::Theme.m_Accent);
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Field.m_TopLeft.x + 8.0f, Field.Center().y + 5.0f), 13.0f, ui::Theme.m_Text, m_TextInput + ".map");

		const map_target Target = AllTargets()[static_cast<std::size_t>(m_ExportTarget)];
		y += 48.0f;
		const std::vector<std::string> Warnings = m_Map.CompatibilityWarnings(Target);
		if(Warnings.empty())
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 12.0f), 12.0f, ui::Theme.m_Success, "this map converts without losses");
		else
			for(std::size_t i = 0; i < Warnings.size() && i < 3; i++)
				ui::TextClipped(pContext, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 14.0f, y + 12.0f + i * 16.0f), 12.0f, ui::Theme.m_Warning,
					"- " + Warnings[i], Popup.Width() - 28.0f);
	}

	void CEditor::DrawSavePopup(NVGcontext *pContext)
	{
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 240.0f, Size.y * 0.5f - 60.0f), base::vec2(480.0f, 120.0f));
		ui::Rect(pContext, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pContext, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 26.0f), 15.0f, ui::Theme.m_Text, "Save the package");
		const base::rect Field = base::rect::FromSize(base::vec2(Popup.m_TopLeft.x + 16.0f, Popup.m_TopLeft.y + 44.0f), base::vec2(Popup.Width() - 32.0f, 28.0f));
		ui::Rect(pContext, Field, ui::Theme.m_PanelDark);
		ui::RectOutline(pContext, Field, ui::Theme.m_Accent);
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Field.m_TopLeft.x + 8.0f, Field.Center().y + 5.0f), 13.0f, ui::Theme.m_Text,
			m_TextInput + CUnitwpPackage::Extension);
	}

	void CEditor::DrawHelpPopup(NVGcontext *pContext)
	{
		const base::vec2 Size = m_pGraphics->WindowSize();
		const base::rect Popup = base::rect::FromSize(base::vec2(Size.x * 0.5f - 330.0f, Size.y * 0.5f - 210.0f), base::vec2(660.0f, 420.0f));
		ui::Rect(pContext, Popup, ui::Theme.m_Panel);
		ui::RectOutline(pContext, Popup, ui::Theme.m_Border, 2.0f);
		ui::Text(pContext, m_pGraphics->FontBold(), base::vec2(Popup.m_TopLeft.x + 18.0f, Popup.m_TopLeft.y + 28.0f), 16.0f, ui::Theme.m_Text, "Uniteed - keys");
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
			"Ctrl+Q                  quit",
		};
		float y = Popup.m_TopLeft.y + 56.0f;
		for(const char *pLine : apLines)
		{
			ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 18.0f, y), 13.0f, ui::Theme.m_Text, pLine);
			y += 22.0f;
		}
		ui::Text(pContext, m_pGraphics->Font(), base::vec2(Popup.m_TopLeft.x + 18.0f, Popup.m_BottomRight.y - 16.0f), 12.0f, ui::Theme.m_TextDim,
			"esc closes this panel");
	}

	void CEditor::DrawPopups(NVGcontext *pContext)
	{
		engine::CInput &Input = m_pGraphics->Input();
		if(m_bShowOpen)
			DrawOpenPopup(pContext);
		if(m_bShowSave)
			DrawSavePopup(pContext);
		if(m_bShowExport)
			DrawExportPopup(pContext);
		if(m_bShowHelp)
			DrawHelpPopup(pContext);

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
		NVGcontext *pContext = m_pGraphics->Context();
		const base::vec2 Size = m_pGraphics->WindowSize();
		// minimized: nanovg builds its projection from the size it is given
		if(Size.x < 1.0f || Size.y < 1.0f)
			return;

		nvgBeginFrame(pContext, Size.x, Size.y, m_pGraphics->PixelRatio());

		ui::Rect(pContext, base::rect::FromSize(base::vec2(0.0f, 0.0f), Size), ui::Theme.m_Background);

		const base::rect Canvas = CanvasRect(Size);
		DrawMap(pContext, Canvas);

		DrawToolbar(pContext, base::rect::FromSize(base::vec2(0.0f, 0.0f), base::vec2(Size.x, ToolbarHeight)));
		DrawLayerPanel(pContext, base::rect::FromSize(base::vec2(0.0f, ToolbarHeight), base::vec2(PanelWidth, Canvas.Height())));
		DrawPalettePanel(pContext, base::rect::FromSize(base::vec2(Size.x - PanelWidth, ToolbarHeight), base::vec2(PanelWidth, Canvas.Height())));
		DrawStatusBar(pContext, base::rect::FromSize(base::vec2(0.0f, Size.y - StatusHeight), base::vec2(Size.x, StatusHeight)));

		DrawPopups(pContext);

		nvgEndFrame(pContext);
	}
} // namespace game
