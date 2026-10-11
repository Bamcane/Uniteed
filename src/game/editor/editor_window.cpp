/*
	The two rows of chrome at the top of the window: the menu bar, which is
	also the title bar of a window that draws its own decorations, and the
	toolbar under it.

	This is the layout a drawing application has. The top row is about the
	*window* and the *document*: the two menus (Files, and the settings of the
	session), the title of what is open, and the three window buttons, with the
	empty space between them dragging the window. The row below is the toolbar,
	and it is about the *map*: the target it is written for, the two steps
	through the edit history, and the shortcuts that show and hide the panels.
	Keeping them apart is what stops either row from having to shrink its
	buttons when the window gets narrower.

	The top row is also what the window is grabbed by. It cannot be dragged by
	itself - moving a window is the window manager's job - so the *shape* of
	the row is published to the engine as chrome (engine/chrome.h), which turns
	"the pointer is on the bar" into "the window is being moved" and "the
	pointer is on a button" into "that click is the application's". What is a
	control therefore has to be listed exactly as it is drawn, which is why the
	buttons push their rectangles into the chrome as they go.
*/

#include <algorithm>
#include <vector>

#include "base/define.h"
#include "base/storage.h"
#include "base/string.h"
#include "base/i18n.h"
#include "engine/graphics.h"
#include "engine/ui.h"
#include "game/editor/editor.h"
#include "game/editor/editor_icons.h"

#if UNITEED_GUI

namespace game
{
	namespace ui = engine::ui;
	namespace i18n = base::i18n;
	using base::i18n::Tr;

	namespace
	{
		using editor_icons::icon;

		// the sizes the two rows are built from
		constexpr float MenuButtonHeight = 20.0f;
		constexpr float ButtonHeight = 24.0f;
		constexpr float PanelButton = 26.0f;
		constexpr float PanelGap = 4.0f;
		constexpr float WindowButton = 34.0f;
		// what the pictogram leaves free inside its square
		constexpr float PanelIconInset = 5.0f;
		// one row of a dropdown
		constexpr float MenuRowHeight = 24.0f;
		constexpr float MenuSeparator = 9.0f;

		/*
			The window buttons, drawn rather than rasterized: three straight
			lines each, which stay crisp at every display density and cost no
			texture at all.
		*/
		enum class window_button
		{
			minimize,
			maximize,
			close,
		};

		struct CWindowButton
		{
			window_button m_Button;
			const char *m_pName;
			const char *m_pHint;
		};

		// i18n:table - the name and the hint are drawn through Tr()
		const CWindowButton g_aWindowButtons[] = {
			{window_button::minimize, "minimize", "fold the window down to the taskbar"},
			{window_button::maximize, "maximize",
				"fill the screen with the window, and put it back the size it was"},
			{window_button::close, "close", "close the editor; the project is not saved on the way out"},
		};

		void WindowGlyph(ui::CPainter *pPainter, window_button Button, const base::rect &Area, ui::CColor Color,
			bool bRestore)
		{
			const base::vec2 Center = Area.Center();
			const float Half = 4.5f;
			switch(Button)
			{
			case window_button::minimize:
				ui::Line(pPainter, base::vec2(Center.x - Half, Center.y), base::vec2(Center.x + Half, Center.y), Color);
				break;
			case window_button::maximize:
				if(bRestore)
				{
					// two squares, the back one offset: the shape every
					// platform uses for "put it back"
					ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(Center.x - Half, Center.y - Half),
						base::vec2(Half * 2.0f - 2.0f, Half * 2.0f - 2.0f)), Color);
					ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(Center.x - Half + 2.0f, Center.y - Half + 2.0f),
						base::vec2(Half * 2.0f - 2.0f, Half * 2.0f - 2.0f)), Color);
				}
				else
					ui::RectOutline(pPainter, base::rect::FromSize(base::vec2(Center.x - Half, Center.y - Half),
						base::vec2(Half * 2.0f, Half * 2.0f)), Color);
				break;
			case window_button::close:
				ui::Line(pPainter, base::vec2(Center.x - Half, Center.y - Half),
					base::vec2(Center.x + Half, Center.y + Half), Color, 1.2f);
				ui::Line(pPainter, base::vec2(Center.x + Half, Center.y - Half),
					base::vec2(Center.x - Half, Center.y + Half), Color, 1.2f);
				break;
			}
		}

		// a tick, drawn as two lines: the interface font is a CJK one and its
		// idea of a check mark is not one to rely on
		void Tick(ui::CPainter *pPainter, const base::rect &Area, ui::CColor Color)
		{
			const base::vec2 Center = Area.Center();
			ui::Line(pPainter, base::vec2(Center.x - 4.0f, Center.y), base::vec2(Center.x - 1.0f, Center.y + 3.5f),
				Color, 1.6f);
			ui::Line(pPainter, base::vec2(Center.x - 1.0f, Center.y + 3.5f), base::vec2(Center.x + 4.5f, Center.y - 3.5f),
				Color, 1.6f);
		}

		// one of the panel shortcuts of the toolbar: which docker it works
		struct CPanelEntry
		{
			CEditor::dock_panel m_Panel;
			icon m_Icon;
			const char *m_pName;
		};

		const CPanelEntry g_aPanelEntries[] = {
			{CEditor::dock_panel::layers, icon::layers, "Layers"},
			{CEditor::dock_panel::tiles, icon::tiles, "Tiles"},
			{CEditor::dock_panel::assets, icon::assets, "Assets"},
			{CEditor::dock_panel::envelopes, icon::envelope, "Envelopes"},
			{CEditor::dock_panel::rules, icon::rules, "Rules"},
		};

		// the label of a menu title, and the shortcut the files menu adds to
		// its rows
		const char *MenuLabel(CEditor::menu_id Id)
		{
			return Id == CEditor::menu_id::files ? "Files" : "Settings";
		}

		/*
			Where a menu title sits in the bar. The bar and the dropdown it
			opens are two pieces of drawing that have to agree on that, so both
			lay the row out from these numbers rather than from a copy of them.
		*/
		base::rect MenuTitleRect(ui::CPainter *pPainter, int Font, CEditor::menu_id Id)
		{
			float x = 6.0f;
			for(const CEditor::menu_id Other : {CEditor::menu_id::files, CEditor::menu_id::settings})
			{
				const float Width = std::max(52.0f, ui::Measure(pPainter, Font, 12.5f, MenuLabel(Other)) + 26.0f);
				if(Other == Id)
					return base::rect::FromSize(base::vec2(x, 0.0f), base::vec2(Width, MenuButtonHeight));
				x += Width + 2.0f;
			}
			return base::rect();
		}

		// what a row of one of the menus does
		enum class menu_action
		{
			none,
			new_map,
			open,
			open_last,
			browse,
			save,
			save_as,
			export_map,
			quit,
			grid,
			tool_strip,
			shortcuts,
			title_bar,
			language,
		};

		/*
			A row of a dropdown. A row without a label is a separator; a row with
			Check set is drawn with a tick, and the tick is filled in when the
			menu is built, so it always says what is true now.
		*/
		struct CMenuRow
		{
			const char *m_pLabel = nullptr;
			const char *m_pKey = nullptr;
			menu_action m_Action = menu_action::none;
			int m_Check = -1;
			// the language a row switches to, for the rows that switch one
			const char *m_pId = nullptr;
		};
	} // namespace

	void CEditor::DrawMenuBar(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = this->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_PanelDark);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x, Area.m_BottomRight.y - 0.5f),
			base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y - 0.5f), ui::Theme.m_Border);

		const bool bClient = m_pGraphics->ClientDecorations();
		// the bar is part of the overlay that blocks the mouse, so it tests the
		// cursor itself rather than through ui::Hovered
		const base::vec2 Cursor = Input.MousePosition();
		const auto Hover = [&Cursor](const base::rect &Rect) { return Rect.Contains(Cursor); };

		// what the engine needs to make the drawn bar behave like a real one
		engine::CClientChrome Chrome;
		Chrome.m_bEnabled = bClient;
		Chrome.m_HeaderHeight = Area.Height();
		Chrome.m_Edge = 4.0f;

		const float Y = Area.m_TopLeft.y + (Area.Height() - MenuButtonHeight) * 0.5f;

		// --------------------------------------------------------- the menus
		struct CMenuTitle
		{
			menu_id m_Id;
			const char *m_pLabel;
			const char *m_pHint;
		};
		const CMenuTitle aTitles[] = {
			{menu_id::files, Tr("Files"), Tr("everything that reads or writes a file")},
			{menu_id::settings, Tr("Settings"), Tr("what the session shows, and how the window is drawn")},
		};
		for(const CMenuTitle &Title : aTitles)
		{
			const base::rect Rect = base::rect::FromSize(
				base::vec2(MenuTitleRect(pPainter, m_pGraphics->Font(), Title.m_Id).m_TopLeft.x, Y),
				base::vec2(MenuTitleRect(pPainter, m_pGraphics->Font(), Title.m_Id).Width(), MenuButtonHeight));

			const bool bHover = Hover(Rect);
			const bool bOpen = m_Menu == Title.m_Id;
			if(bOpen || bHover)
				ui::Rect(pPainter, Rect, bOpen ? ui::Theme.m_AccentDim : ui::Theme.m_Hover);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Rect.Center().x, Rect.Center().y + 4.5f), 12.5f,
				bOpen ? ui::Theme.m_Text : ui::Theme.m_TextDim, Title.m_pLabel, ui::text_align::center);
			// no tip while a menu is open: it would be drawn over the menu,
			// which is the one thing a tooltip must never do
			if(bHover && m_Menu == menu_id::none)
				Hint(Rect, Title.m_pLabel, Title.m_pHint);

			// one menu is open at a time, and running along the row moves to
			// the next one, which is what a menu bar does everywhere
			if(bHover && m_Menu != menu_id::none && !bOpen)
				m_Menu = Title.m_Id;
			if(bHover && Input.MousePressed(engine::CInput::mouse_button::left))
				m_Menu = bOpen ? menu_id::none : Title.m_Id;
			Chrome.m_vControls.push_back(Rect);
		}

		// ------------------------------------------------ the window buttons
		if(bClient)
		{
			const float GlyphHeight = Area.Height() - 4.0f;
			const bool bMaximized = m_pGraphics->Maximized();
			float ButtonX = Area.m_BottomRight.x - 4.0f - WindowButton * 3.0f;
			for(const CWindowButton &Entry : g_aWindowButtons)
			{
				const base::rect Rect = base::rect::FromSize(base::vec2(ButtonX, Area.m_TopLeft.y + 2.0f),
					base::vec2(WindowButton, GlyphHeight));
				ButtonX += WindowButton;

				const bool bHover = Hover(Rect);
				const bool bClose = Entry.m_Button == window_button::close;
				if(bHover)
					ui::Rect(pPainter, Rect, bClose ? ui::Theme.m_Error : ui::Theme.m_Hover);
				WindowGlyph(pPainter, Entry.m_Button, Rect,
					bHover ? (bClose ? ui::Theme.m_Background : ui::Theme.m_Text) : ui::Theme.m_TextDim, bMaximized);
				if(bHover)
					Hint(Rect, Tr(Entry.m_pName), Tr(Entry.m_pHint));
				if(bHover && Input.MousePressed(engine::CInput::mouse_button::left))
				{
					switch(Entry.m_Button)
					{
					case window_button::minimize: m_pGraphics->MinimizeWindow(); break;
					case window_button::maximize: m_pGraphics->ToggleMaximizeWindow(); break;
					case window_button::close: m_pGraphics->CloseWindow(); break;
					}
				}
				Chrome.m_vControls.push_back(Rect);
			}
		}

		// ---------------------------------------------------------- the title
		/*
			What a system title bar would say, said in the space between the two
			ends: the project, whether it has unsaved edits, and the target it
			is written for. The rest of the row is empty, which is what makes it
			draggable.
		*/
		std::string Name = ProjectName();
		if(Name.empty() && !m_SourcePath.empty())
			// a map opened straight from a file has no project name: the file
			// it came from is the name it goes by
			Name = storage::path(m_SourcePath).stem().string();
		if(Name.empty())
			Name = "untitled";
		// the window list shows the same thing, and a system that still draws a
		// title bar of its own at least shows it there
		m_pGraphics->SetWindowTitle(base::Format("{}{} - " GAME_DISPLAY_NAME, m_Dirty ? "*" : "", Name.c_str()));

		const float BarStart = MenuTitleRect(pPainter, m_pGraphics->Font(), menu_id::settings).m_BottomRight.x + 6.0f;
		const float BarEnd = Area.m_BottomRight.x - (bClient ? WindowButton * 3.0f + 12.0f : 8.0f);
		if(BarEnd - BarStart > 80.0f)
		{
			const std::string Title = (m_Dirty ? "* " : "") + Name + "  -  " + GetTargetProfile(m_Target).m_pName;
			ui::TextClipped(pPainter, m_pGraphics->Font(), base::vec2(BarEnd - 8.0f, Area.Center().y + 4.5f), 12.5f,
				ui::Theme.m_TextDim, Title, BarEnd - BarStart - 16.0f, ui::text_align::right);
		}
		if(bClient && Input.MousePosition().y < Area.m_BottomRight.y && Input.MousePosition().x >= BarStart)
		{
			// a double click on a title bar maximizes it everywhere else, and
			// the right button asks the desktop for its own menu
			if(Input.MouseDoubleClicked(engine::CInput::mouse_button::left))
				m_pGraphics->ToggleMaximizeWindow();
			else if(Input.MousePressed(engine::CInput::mouse_button::right))
				m_pGraphics->ShowWindowSystemMenu(Input.MousePosition());
		}

		m_pGraphics->SetClientChrome(Chrome);
	}

	void CEditor::DrawMenus(ui::CPainter *pPainter)
	{
		if(m_Menu == menu_id::none)
			return;
		engine::CInput &Input = this->Input();

		/*
			The rows of the open menu, built here so a tick says what is true on
			this frame rather than when the menu was opened.
		*/
		std::vector<CMenuRow> Rows;
		if(m_Menu == menu_id::files)
		{
			Rows.push_back({Tr("New map"), "Ctrl+N", menu_action::new_map, -1});
			Rows.push_back({Tr("Open..."), "Ctrl+O", menu_action::open, -1});
			Rows.push_back({Tr("Reopen the last file"), nullptr, menu_action::open_last, -1});
			if(!m_FileDialog.Available())
				Rows.push_back({Tr("Browse from the editor"), nullptr, menu_action::browse, -1});
			Rows.push_back({nullptr, nullptr, menu_action::none, -1});
			Rows.push_back({m_Dirty ? Tr("Save *") : Tr("Save"), "Ctrl+S", menu_action::save, -1});
			Rows.push_back({Tr("Save as..."), "Ctrl+Shift+S", menu_action::save_as, -1});
			Rows.push_back({Tr("Export for the target"), "Ctrl+E", menu_action::export_map, -1});
			Rows.push_back({nullptr, nullptr, menu_action::none, -1});
			Rows.push_back({Tr("Quit"), "Ctrl+Q", menu_action::quit, -1});
		}
		else
		{
			Rows.push_back({Tr("Tile grid"), "G", menu_action::grid, m_bGrid ? 1 : 0});
			Rows.push_back({Tr("Tool strip"), nullptr, menu_action::tool_strip, m_bShowToolBox ? 1 : 0});
			Rows.push_back({Tr("Keyboard shortcuts"), "?", menu_action::shortcuts, -1});
			Rows.push_back({nullptr, nullptr, menu_action::none, -1});
			Rows.push_back({Tr("Draw the title bar"), nullptr, menu_action::title_bar,
				m_pGraphics->ClientDecorations() ? 1 : 0});
			/*
				One row per language the build ships, each named in its own
				language: a user looking for theirs cannot recognise a name
				written in a language they do not read.
			*/
			Rows.push_back({nullptr, nullptr, menu_action::none, -1});
			for(const i18n::CLanguage &Language : i18n::Languages())
				Rows.push_back({Language.m_Name.c_str(), nullptr, menu_action::language,
					i18n::CurrentId() == Language.m_Id ? 1 : 0, Language.m_Id.c_str()});
		}

		// as wide as its longest row, so the shortcuts line up
		float Width = 190.0f;
		float Height = 8.0f;
		for(const CMenuRow &Row : Rows)
		{
			if(Row.m_pLabel == nullptr)
			{
				Height += MenuSeparator;
				continue;
			}
			Height += MenuRowHeight;
			float Needed = 34.0f + ui::Measure(pPainter, m_pGraphics->Font(), 12.5f, Row.m_pLabel);
			if(Row.m_pKey != nullptr)
				Needed += 24.0f + ui::Measure(pPainter, m_pGraphics->Font(), 11.5f, Row.m_pKey);
			Width = std::max(Width, Needed);
		}

		// under the button that opened it, and never off the right edge
		const float PanelX = MenuTitleRect(pPainter, m_pGraphics->Font(), m_Menu).m_TopLeft.x;
		const base::rect Panel = base::rect::FromSize(
			base::vec2(std::min(PanelX, m_pGraphics->WindowSize().x - Width - 4.0f), MenuBarHeight),
			base::vec2(Width, Height));
		ui::Rect(pPainter, Panel, ui::Theme.m_Panel);
		ui::RectOutline(pPainter, Panel, ui::Theme.m_Border);

		float y = Panel.m_TopLeft.y + 4.0f;
		for(const CMenuRow &Row : Rows)
		{
			if(Row.m_pLabel == nullptr)
			{
				ui::Line(pPainter, base::vec2(Panel.m_TopLeft.x + 8.0f, y + MenuSeparator * 0.5f),
					base::vec2(Panel.m_BottomRight.x - 8.0f, y + MenuSeparator * 0.5f), ui::Theme.m_Border);
				y += MenuSeparator;
				continue;
			}
			const base::rect Rect = base::rect::FromSize(base::vec2(Panel.m_TopLeft.x + 3.0f, y),
				base::vec2(Panel.Width() - 6.0f, MenuRowHeight));
			y += MenuRowHeight;

			const bool bEnabled = Row.m_Action != menu_action::open_last || !m_SourcePath.empty();
			const bool bHover = bEnabled && Rect.Contains(Input.MousePosition());
			if(bHover)
				ui::Rect(pPainter, Rect, ui::Theme.m_Hover);
			if(Row.m_Check == 1)
				Tick(pPainter, base::rect::FromSize(base::vec2(Rect.m_TopLeft.x + 4.0f, Rect.m_TopLeft.y),
					base::vec2(18.0f, MenuRowHeight)), ui::Theme.m_Accent);
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Rect.m_TopLeft.x + 26.0f, Rect.Center().y + 4.5f), 12.5f,
				bEnabled ? ui::Theme.m_Text : ui::Theme.m_TextDim, Row.m_pLabel);
			if(Row.m_pKey != nullptr)
				ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Rect.m_BottomRight.x - 10.0f, Rect.Center().y + 4.5f),
					11.5f, ui::Theme.m_TextDim, Row.m_pKey, ui::text_align::right);

			if(!bHover || !Input.MousePressed(engine::CInput::mouse_button::left))
				continue;
			m_Menu = menu_id::none;
			switch(Row.m_Action)
			{
			case menu_action::new_map:
				NewMap(50, 50);
				m_bShowWelcome = false;
				break;
			case menu_action::open:
				OpenFileDialog();
				m_bShowWelcome = false;
				break;
			case menu_action::open_last:
				if(!m_SourcePath.empty() && OpenFile(m_SourcePath))
					m_bShowWelcome = false;
				break;
			case menu_action::browse:
				m_bShowOpen = true;
				OpenBrowser();
				break;
			case menu_action::save: SaveFileDialog(false); break;
			case menu_action::save_as: SaveFileDialog(true); break;
			case menu_action::export_map: ExportFileDialog(m_Target); break;
			case menu_action::quit: m_pGraphics->CloseWindow(); break;
			case menu_action::grid: m_bGrid = !m_bGrid; break;
			case menu_action::tool_strip: ToggleToolBox(); break;
			case menu_action::shortcuts: m_bShowShortcuts = true; break;
			case menu_action::title_bar:
				/*
					The way out, in the interface itself: a compositor that will
					not move a window without decorations is not something the
					editor can detect, so the user has to be able to bring the
					system title bar back without editing a config file.
				*/
				{
					engine::CClientChrome Chrome;
					Chrome.m_bEnabled = !m_pGraphics->ClientDecorations();
					Chrome.m_HeaderHeight = MenuBarHeight;
					Chrome.m_Edge = 4.0f;
					m_pGraphics->SetClientChrome(Chrome);
				}
				break;
			case menu_action::language:
				if(i18n::SetLanguage(Row.m_pId))
				{
					// the status line answers in the language that was just
					// chosen: it is the check that the switch took
					SetStatus(base::FormatRuntime(Tr("the interface language is now {}"), Row.m_pLabel));
				}
				break;
			case menu_action::none: break;
			}
		}

		// a click outside closes it, the way every menu does. The click that
		// opened it was on the menu bar, which is why the bar is not "outside".
		if(Input.MousePressed(engine::CInput::mouse_button::left) &&
			!Panel.Contains(Input.MousePosition()) && Input.MousePosition().y >= MenuBarHeight)
			m_Menu = menu_id::none;
	}

	void CEditor::DrawToolbar(ui::CPainter *pPainter, const base::rect &Area)
	{
		engine::CInput &Input = this->Input();
		ui::Rect(pPainter, Area, ui::Theme.m_Panel);
		ui::Line(pPainter, base::vec2(Area.m_TopLeft.x, Area.m_BottomRight.y - 0.5f),
			base::vec2(Area.m_BottomRight.x, Area.m_BottomRight.y - 0.5f), ui::Theme.m_Border);

		const float Y = Area.m_TopLeft.y + (Area.Height() - ButtonHeight) * 0.5f;

		// the panel shortcuts go against the right edge, then a separator
		const int PanelCount = static_cast<int>(std::size(g_aPanelEntries)) + 1;
		const float PanelsWidth = static_cast<float>(PanelCount) * PanelButton +
			static_cast<float>(PanelCount - 1) * PanelGap;
		float PanelX = Area.m_BottomRight.x - 8.0f - PanelsWidth;
		const float PanelLeft = PanelX;
		{
			// the pictogram of the tool strip itself, which is the one panel
			// that is not a docker
			const base::rect Rect = base::rect::FromSize(base::vec2(PanelX, Area.Center().y - PanelButton * 0.5f),
				base::vec2(PanelButton, PanelButton));
			const bool bShown = m_bShowToolBox;
			PanelX += PanelButton + PanelGap;
			if(ui::Hovered(Input, Rect))
				Hint(Rect,   bShown ? Tr("Tools - click to hide") : Tr("Tools - click to show")  , Tr("the tool strip on the left, which is not a docker") );
			if(editor_icons::Button(m_VectorTextures, pPainter, Input, Rect, icon::toolbox, bShown, true, PanelIconInset))
				ToggleToolBox();
		}
		for(const CPanelEntry &Entry : g_aPanelEntries)
		{
			const base::rect Rect = base::rect::FromSize(base::vec2(PanelX, Area.Center().y - PanelButton * 0.5f),
				base::vec2(PanelButton, PanelButton));
			PanelX += PanelButton + PanelGap;
			const bool bShown = PanelShown(Entry.m_Panel);
			if(ui::Hovered(Input, Rect))
				Hint(Rect,   base::FormatRuntime(Tr("{} - click to {}"), Entry.m_pName, bShown ? Tr("hide") : Tr("show"))  ,
					TabHint(Entry.m_Panel));
			if(editor_icons::Button(m_VectorTextures, pPainter, Input, Rect, Entry.m_Icon, bShown, true, PanelIconInset))
				TogglePanel(Entry.m_Panel);
		}
		// the seam between the map controls and the panel shortcuts
		ui::Line(pPainter, base::vec2(PanelLeft - 7.0f, Y), base::vec2(PanelLeft - 7.0f, Y + ButtonHeight),
			ui::Theme.m_Border);

		// ------------------------------------------------------- the targets
		float x = Area.m_TopLeft.x + 8.0f;
		const float Limit = PanelLeft - 7.0f - 24.0f;
		bool bTruncated = false;
		const auto Button = [&](const std::string &Label, bool Active, bool Enabled, const char *pKey,
			const std::string &Detail) {
			const float Width = std::max(46.0f, ui::Measure(pPainter, m_pGraphics->Font(), 13.0f, Label) + 22.0f);
			if(x + Width > Limit)
			{
				bTruncated = true;
				return false;
			}
			const base::rect Rect = base::rect::FromSize(base::vec2(x, Y), base::vec2(Width, ButtonHeight));
			x += Width + 6.0f;
			if(ui::Hovered(Input, Rect))
				Hint(Rect,   pKey != nullptr ? base::FormatRuntime(Tr("{}  ({})"), Label.c_str(), pKey) : Label  ,
					Detail.empty() ? (Enabled ? base::FormatRuntime(Tr("click for {}"), Label) :
						std::string(Tr("nothing to do here yet"))) : Detail);
			return ui::Button(pPainter, Input, m_pGraphics->Font(), Rect, Label, Active, Enabled);
		};
		const auto Separator = [&]() {
			if(x + 12.0f > Limit)
			{
				bTruncated = true;
				return;
			}
			ui::Line(pPainter, base::vec2(x + 3.0f, Y), base::vec2(x + 3.0f, Y + ButtonHeight), ui::Theme.m_Border);
			x += 12.0f;
		};

		for(const map_target Target : AllTargets())
		{
			const CMapTargetProfile &Profile = GetTargetProfile(Target);
			if(Button(Profile.m_pId, m_Target == Target, true, nullptr,
				base::Format("{}: datafile v{:d}, {}{}", Profile.m_pName, Profile.m_DatafileVersion,
					Tr(Profile.HasExtension(ext_game_entities) ? "DDRace entities" : "vanilla entities"),
					m_Target == Target ? " (already selected)" : "")))
			{
				m_Target = Target;
				// the extensions switch decides whether the DDRace features the
				// map already has are kept or dropped, so changing the target
				// says out loud what it will cost
				const std::vector<std::string> Warnings = m_Map.CompatibilityWarnings(Target);
				if(Warnings.empty())
					SetStatus(  base::FormatRuntime(Tr("target {}"), Profile.m_pName)  );
				else
					SetStatus(  base::FormatRuntime(Tr("target {}: {}"), Profile.m_pName, Warnings.front().c_str())  );
			}
		}
		// the DDRace features keep their own switch beside the targets, so a
		// mapmaker can keep lasers and teleporters in a vanilla map on purpose
		if(GetTargetProfile(m_Target).HasExtension(ext_game_entities))
		{
			if(Button(Tr("ddnet ext"), m_bAllowExtensions, true, nullptr,
				Tr("whether this map may use DDRace entities, physics layers and sounds")))
				SetAllowExtensions(!m_bAllowExtensions);
		}
		else if(Button(Tr("allow ddnet"), m_bAllowExtensions, true, nullptr,
			Tr("keep the DDRace features the map already carries, even though this target is vanilla")))
			SetAllowExtensions(!m_bAllowExtensions);

		Separator();
		if(Button(Tr("Undo"), false, !m_vUndo.empty(), "Ctrl+Z", Tr("step back through the edits of this session")))
			Undo();
		if(Button(Tr("Redo"), false, !m_vRedo.empty(), "Ctrl+Y", Tr("step forward again")))
			Redo();
		if(bTruncated)
		{
			// a button that did not fit is not drawn at all; saying so is
			// better than an empty stretch of toolbar that looks complete
			const base::rect Cut = base::rect::FromSize(base::vec2(x, Y), base::vec2(20.0f, ButtonHeight));
			ui::Text(pPainter, m_pGraphics->Font(), base::vec2(Cut.Center().x, Cut.Center().y + 4.5f), 13.0f,
				ui::Theme.m_TextDim, Tr("...") , ui::text_align::center);
			if(ui::Hovered(Input, Cut))
				Hint(Cut, Tr("more buttons") , Tr("this window is too narrow for the rest of the toolbar: the keys still work, and a wider window shows them again") );
		}
	}
} // namespace game

#endif
