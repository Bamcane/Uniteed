/*
	The chrome around the canvas: the vertical tool strip on the left, and the
	panels that float over the workspace (the welcome page and the shortcut
	list).

	The tool strip is the piece that makes the editor read as a Krita style
	application rather than a row of buttons: the tools stay in one place at a
	fixed size, the status bar names the one under the cursor, and nothing about
	them moves when a dock is resized or collapsed. The floating panels are the
	other half of that idea - they are not docks, so they can be shown over the
	canvas without changing the layout the map maker arranged.

	Every button in the strip is a *drawn* icon rather than a letter, because a
	letter is only readable to whoever already knows the key: hovering one names
	it and says what it does, which is what a newcomer has to go on.
*/

#include <algorithm>

#include "base/define.h"
#include "base/string.h"
#include "engine/graphics.h"
#include "engine/keycodes.h"
#include "engine/ui.h"
#include "game/editor/editor.h"
#include "game/editor/editor_icons.h"

#if UNITEED_GUI

namespace game
{
	namespace ui = engine::ui;

	namespace
	{
		using editor_icons::icon;

		constexpr float ToolButton = 30.0f;
		constexpr float ToolGap = 3.0f;
		// how much of a button the pictogram leaves free around itself
		constexpr float ToolIconInset = 6.0f;

		// one entry of the tool strip: what it selects, what it draws, and what
		// the status bar and the tooltip say about it
		struct CToolEntry
		{
			editor_tool m_Tool;
			icon m_Icon;
			const char *m_pName;
			const char *m_pKey;
			const char *m_pHint;
			// a tool that only means something on a tile layer: the button
			// stays where it is and greys out instead of disappearing, so the
			// strip does not reflow when the selected layer changes
			bool m_bTilesOnly;
		};

		const CToolEntry g_aToolEntries[] = {
			{editor_tool::pan, icon::pan, "Pan", "V", "drag the view around the map without changing it", false},
			{editor_tool::paint, icon::brush, "Brush", "B", "paint the brush under the cursor", true},
			{editor_tool::fill, icon::bucket, "Fill", "F", "flood the region of one tile with the brush", true},
			{editor_tool::erase, icon::eraser, "Eraser", "E",
				"the eraser: press E again and the tool you were using comes back", true},
			{editor_tool::pick, icon::marquee, "Pick", "P", "drag a rectangle of the map into the brush", true},
		};

		// the view toggles, which are not tools: they change what the canvas
		// shows rather than what a drag does
		enum class toggle
		{
			fit,
			grid,
			keys,
		};

		struct CToggleEntry
		{
			icon m_Icon;
			const char *m_pName;
			const char *m_pKey;
			const char *m_pHint;
			toggle m_Toggle;
		};

		const CToggleEntry g_aToggleEntries[] = {
			{icon::frame, "Fit", "Ctrl+0", "frame the whole map in the window", toggle::fit},
			{icon::grid, "Grid", "G", "show the tile grid", toggle::grid},
			{icon::keyboard, "Keys", "?", "every shortcut in one panel", toggle::keys},
		};
	} // namespace

	base::rect CEditor::FloatingRect(const base::rect &Canvas, base::vec2 Size) const
	{
		// centred over the canvas, and never larger than it, so a small window
		// still shows the whole panel
		const float Width = std::min(Size.x, std::max(Canvas.Width() - 24.0f, 120.0f));
		const float Height = std::min(Size.y, std::max(Canvas.Height() - 24.0f, 80.0f));
		return base::rect::FromSize(base::vec2(Canvas.Center().x - Width * 0.5f, Canvas.Center().y - Height * 0.5f),
			base::vec2(Width, Height));
	}

	void CEditor::RequestTooltip(base::vec2 Position, const std::string &Title, const std::string &Detail)
	{
		// the last request of a frame wins, which is the widget nearest the
		// top: two overlapping hints would be a layout bug, not a feature
		m_TooltipPos = Position;
		m_TooltipTitle = Title;
		m_TooltipDetail = Detail;
		m_bTooltip = true;
	}

	void CEditor::DrawTooltip(ui::CPainter *pPainter, const base::vec2 &Window)
	{
		if(!m_bTooltip || m_TooltipTitle.empty())
			return;
		ui::Tooltip(pPainter, m_pGraphics->FontBold(), m_pGraphics->Font(), m_TooltipPos, m_TooltipTitle,
			m_TooltipDetail, base::rect::FromSize(base::vec2(0.0f, 0.0f), Window));
	}

	void CEditor::Hint(const base::rect &Area, const std::string &Title, const std::string &Detail)
	{
		SetStatus(Detail);
		// under the widget, which is where the eye already is; the tooltip
		// moves itself back inside the window when there is no room there
		RequestTooltip(base::vec2(Area.m_TopLeft.x, Area.m_BottomRight.y + 6.0f), Title, Detail);
	}

	void CEditor::DrawToolBox(ui::CPainter *pPainter, const base::rect &Area)
	{
		if(Area.Width() <= 1.0f)
			return;
		engine::CInput &Input = this->Input();

		ui::Rect(pPainter, Area, ui::Theme.m_PanelDark);
		ui::Line(pPainter, base::vec2(Area.m_BottomRight.x - 0.5f, Area.m_TopLeft.y),
			base::vec2(Area.m_BottomRight.x - 0.5f, Area.m_BottomRight.y), ui::Theme.m_Border);

		const float X = Area.m_TopLeft.x + (Area.Width() - ToolButton) * 0.5f;
		float y = Area.m_TopLeft.y + 8.0f;

		const auto ToolButtonAt = [&](icon Icon, const char *pName, const char *pKey, bool bActive, bool bEnabled,
			const char *pHint) {
			const base::rect Rect = base::rect::FromSize(base::vec2(X, y), base::vec2(ToolButton, ToolButton));
			y += ToolButton + ToolGap;
			const bool bClicked = editor_icons::Button(m_VectorTextures, pPainter, Input, Rect, Icon, bActive, bEnabled,
				ToolIconInset);
			if(ui::Hovered(Input, Rect))
			{
				SetStatus(bEnabled ? pHint : "this needs a tile layer: pick one in the Layers docker");
				RequestTooltip(base::vec2(Rect.m_BottomRight.x + 8.0f, Rect.m_TopLeft.y - 4.0f),
					base::Format("%s  (%s)", pName, pKey),
					bEnabled ? pHint : "select a tile layer to use this tool");
			}
			return bClicked;
		};

		// the tools the canvas has, in the order a paint program puts them
		for(const CToolEntry &Entry : g_aToolEntries)
		{
			const bool bEnabled = !Entry.m_bTilesOnly || Mode() == edit_mode::tiles;
			if(ToolButtonAt(Entry.m_Icon, Entry.m_pName, Entry.m_pKey, m_Tool == Entry.m_Tool, bEnabled, Entry.m_pHint) &&
				bEnabled)
				SetTool(Entry.m_Tool);
		}

		// a gap, then the toggles that belong to the view rather than to a tool
		y += 8.0f;
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x + 6.0f, y - 4.0f),
			base::vec2(Area.m_BottomRight.x - 6.0f, y - 4.0f), ui::Theme.m_Border);

		for(const CToggleEntry &Entry : g_aToggleEntries)
		{
			const bool bActive = Entry.m_Toggle == toggle::grid ? m_bGrid :
				(Entry.m_Toggle == toggle::keys ? m_bShowShortcuts : false);
			if(!ToolButtonAt(Entry.m_Icon, Entry.m_pName, Entry.m_pKey, bActive, true, Entry.m_pHint))
				continue;
			switch(Entry.m_Toggle)
			{
			case toggle::fit: FitToMap(); break;
			case toggle::grid: m_bGrid = !m_bGrid; break;
			case toggle::keys: m_bShowShortcuts = !m_bShowShortcuts; break;
			}
		}
	}

	void CEditor::DrawWelcomePanel(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		engine::CInput &Input = this->Input();
		const base::rect Panel = FloatingRect(Canvas, base::vec2(560.0f, 330.0f));
		ui::Rect(pPainter, Panel, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Panel, ui::Theme.m_Border, 2.0f);

		const float X = Panel.m_TopLeft.x + 26.0f;
		float y = Panel.m_TopLeft.y + 34.0f;
		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(X, y), 22.0f, ui::Theme.m_Text, GAME_DISPLAY_NAME " " GAME_VERSION);
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Panel.m_BottomRight.x - 26.0f, y), 12.0f, ui::Theme.m_TextDim,
			"a map editor for 0.6, 0.7, DDRaceNetwork and 0.8", ui::text_align::right);
		y += 16.0f;
		ui::Line(pPainter, base::vec2(X, y), base::vec2(Panel.m_BottomRight.x - 26.0f, y), ui::Theme.m_Border);
		y += 26.0f;

		// four ways in, which is what a welcome page is for
		struct CEntry
		{
			const char *m_pTitle;
			std::string m_Detail;
			int m_Action; // 0 new, 1 open, 2 the last map, 3 the shortcut list
			bool m_bEnabled;
		};
		const std::string Last = m_SourcePath.empty() ? "no map has been opened yet" : m_SourcePath;
		const CEntry aEntries[] = {
			{"New map", "start from an empty map of the current target", 0, true},
			{"Open a project or a map", "the platform's own file dialog", 1, true},
			{"Open the last map", Last, 2, !m_SourcePath.empty()},
			{"Keys and mouse", "the whole shortcut list", 3, true},
		};
		const float RowHeight = 42.0f;
		for(const CEntry &Entry : aEntries)
		{
			const base::rect Row = base::rect::FromSize(base::vec2(X, y), base::vec2(Panel.Width() - 52.0f, RowHeight - 6.0f));
			const bool bHover = Entry.m_bEnabled && ui::Hovered(Input, Row);
			ui::Rect(pPainter, Row, bHover ? ui::Theme.m_Hover : ui::Theme.m_PanelDark);
			ui::RectOutline(pPainter, Row, bHover ? ui::Theme.m_Accent : ui::Theme.m_Border);
			ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Row.m_TopLeft.x + 12.0f, Row.m_TopLeft.y + 16.0f), 13.0f,
				Entry.m_bEnabled ? ui::Theme.m_Text : ui::Theme.m_TextDim, Entry.m_pTitle);
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(Row.m_TopLeft.x + 12.0f, Row.m_TopLeft.y + 31.0f), 11.0f,
				ui::Theme.m_TextDim, Entry.m_Detail, Row.Width() - 24.0f);
			if(bHover)
				Hint(Row, Entry.m_pTitle, Entry.m_Detail);
			if(bHover && Input.MousePressed(engine::CInput::mouse_button::left))
			{
				switch(Entry.m_Action)
				{
				case 0:
					NewMap(50, 50);
					m_bShowWelcome = false;
					break;
				case 1:
					OpenFileDialog();
					m_bShowWelcome = false;
					break;
				case 2:
					if(OpenFile(m_SourcePath))
						m_bShowWelcome = false;
					break;
				case 3:
					m_bShowShortcuts = true;
					break;
				}
			}
			y += RowHeight;
		}

		const base::rect Close = base::rect::FromSize(base::vec2(Panel.m_BottomRight.x - 96.0f, Panel.m_BottomRight.y - 32.0f),
			base::vec2(70.0f, 22.0f));
		if(ui::Button(pPainter, Input, m_pGraphics->Font(), Close, "Start"))
			m_bShowWelcome = false;
	}

	void CEditor::DrawShortcutsPanel(ui::CPainter *pPainter, const base::rect &Canvas)
	{
		engine::CInput &Input = this->Input();
		const base::rect Panel = FloatingRect(Canvas, base::vec2(720.0f, 540.0f));
		ui::Rect(pPainter, Panel, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Panel, ui::Theme.m_Border, 2.0f);

		ui::Text(pPainter, m_pGraphics->FontBold(), base::vec2(Panel.m_TopLeft.x + 20.0f, Panel.m_TopLeft.y + 28.0f), 16.0f,
			ui::Theme.m_Text, "Keys and mouse");
		ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Panel.m_BottomRight.x - 20.0f, Panel.m_TopLeft.y + 28.0f), 12.0f,
			ui::Theme.m_TextDim, "the canvas edits whatever the layer tree selects", ui::text_align::right);

		const char *apCommon[] = {
			"WORKSPACE",
			"middle / right drag   pan",
			"V then left drag      move the view",
			"wheel                 zoom",
			"W A S D or arrows     pan",
			"Ctrl+0                fit the map in the window",
			"G                     toggle the tile grid",
			"Ctrl+Z / Ctrl+Y       undo / redo",
			"Ctrl+N / Ctrl+O       new / open",
			"Ctrl+S / Ctrl+Shift+S save / save as",
			"Ctrl+E                export for the current target",
			"Ctrl+L / Shift+Ctrl+L add a tile layer / a quad layer",
			"Ctrl+G                add a group",
			"?                     this panel",
			"",
			"WINDOW",
			"drag the top bar      move the window",
			"double click it       maximize, and back",
			"right click it        the window's own menu",
		};
		const char *apCanvas[] = {
			"TOOLS",
			"B  left drag          brush",
			"E                     eraser, and E again to go back",
			"F                     fill: flood one tile region",
			"P                     picker: drag a region into the brush",
			"V                     move the view",
			"1 2 3                 brush / eraser / picker",
			"",
			"TILE LAYER",
			"right click           pick the tile under the cursor",
			"Space                 use the tile under the cursor",
			", .                   previous / next tile",
			"4 5 6 7 [ ]           brush size 1 2 4 8",
			"H J K                 hflip / vflip / rotate the brush",
			"Shift+P               stamp the whole layer with the brush",
			"",
			"QUAD LAYER",
			"left click            select the quad under the cursor",
			"ctrl + click          add a quad to the selection",
			"drag a point          move that corner, or the pivot",
			"C                     open the colour picker",
		};

		// a line in capitals on its own is a heading, so the two columns cannot
		// drift out of the styling the other one uses
		const auto IsHeading = [](const char *pLine) {
			if(pLine[0] == '\0')
				return false;
			int Letters = 0;
			for(const char *p = pLine; *p != '\0'; p++)
			{
				if(*p >= 'a' && *p <= 'z')
					return false;
				if(*p >= 'A' && *p <= 'Z')
					Letters++;
			}
			return Letters >= 3;
		};
		const auto Column = [&](const char *const *apLines, std::size_t Count, float ColumnX, float StartY) {
			float y = StartY;
			for(std::size_t i = 0; i < Count; i++)
			{
				const bool bHeading = IsHeading(apLines[i]);
				if(apLines[i][0] != '\0')
					ui::Text(pPainter, bHeading ? m_pGraphics->FontBold() : m_pGraphics->Font(), base::vec2(ColumnX, y),
						bHeading ? 12.5f : 11.5f, bHeading ? ui::Theme.m_Text : ui::Theme.m_TextDim, apLines[i]);
				y += bHeading ? 22.0f : 17.0f;
			}
		};
		Column(apCommon, std::size(apCommon), Panel.m_TopLeft.x + 20.0f, Panel.m_TopLeft.y + 60.0f);
		Column(apCanvas, std::size(apCanvas), Panel.m_TopLeft.x + 340.0f, Panel.m_TopLeft.y + 60.0f);

		const base::rect Close = base::rect::FromSize(base::vec2(Panel.m_BottomRight.x - 96.0f, Panel.m_BottomRight.y - 34.0f),
			base::vec2(70.0f, 22.0f));
		if(ui::Button(pPainter, Input, m_pGraphics->Font(), Close, "Close") || Input.KeyPressed(engine::keys::Escape))
			m_bShowShortcuts = false;

		// a click anywhere outside closes it, which is what a floating panel
		// does everywhere else
		if(Input.MousePressed(engine::CInput::mouse_button::left) && !ui::Hovered(Input, Panel))
			m_bShowShortcuts = false;
	}
} // namespace game

#endif
