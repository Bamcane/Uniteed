#include "editor.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "engine/keycodes.h"

#include <cmath>

#include "base/i18n.h"
#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/graphics.h"
#include "engine/ui_theme.h"
#include "game/assets.h"
#include "game/editor/editor_icons.h"
#include "game/editor/render.h"
#include "game/map/envelope.h"

namespace game
{
	using base::i18n::Tr;
	namespace ui = engine::ui;

	namespace
	{
		constexpr float TileSize = 32.0f;

		// how close the cursor has to be to a quad handle to grab it, and how
		// big the handles are drawn
		constexpr float HandleGrabRadius = 9.0f;
		constexpr float HandleDrawSize = 9.0f;
		constexpr float PivotDrawSize = 11.0f;

		// the biggest brush stamp: the size buttons only go to 8x8, but a
		// region picked off the map may be wider than it is tall
		constexpr int MaxBrushExtent = 64;

		// the entity names of the vanilla game layer, and the DDRace ones the
		// switch layer understands
		struct CEntityName
		{
			int m_Index;
			const char *m_pName;
		};

		const CEntityName g_aEntities[] = {
			{ENTITY_SPAWN, "spawn"},
			{ENTITY_SPAWN_RED, "spawn red"},
			{ENTITY_SPAWN_BLUE, "spawn blue"},
			{ENTITY_FLAGSTAND_RED, "flag red"},
			{ENTITY_FLAGSTAND_BLUE, "flag blue"},
			{ENTITY_ARMOR_1, "armor"},
			{ENTITY_HEALTH_1, "health"},
			{ENTITY_WEAPON_SHOTGUN, "shotgun"},
			{ENTITY_WEAPON_GRENADE, "grenade"},
			{ENTITY_POWERUP_NINJA, "ninja"},
			{ENTITY_WEAPON_LASER, "laser"},
			{ENTITY_LASER_NORMAL_CCW, "laser ccw"},
			{ENTITY_LASER_STOP, "laser stop"},
			{ENTITY_LASER_NORMAL_CW, "laser cw"},
			{ENTITY_DRAGGER_NORMAL, "dragger"},
			{ENTITY_DRAGGER_STRONG, "dragger strong"},
			{ENTITY_DOOR, "door"},
		};

		const char *ToolName(editor_tool Tool)
		{
			switch(Tool)
			{
			case editor_tool::paint: return Tr("paint");
			case editor_tool::fill: return Tr("fill");
			case editor_tool::erase: return Tr("erase");
			case editor_tool::pick: return Tr("pick");
			case editor_tool::pan: return Tr("pan");
			}
			return "?";
		}
	} // namespace

	// ------------------------------------------------------------------- setup
	bool CEditor::Init(engine::CGraphics *pGraphics)
	{
		m_pGraphics = pGraphics;
		if(pGraphics != nullptr)
		{
			const base::vec2 Size = pGraphics->WindowSize();
			if(Size.x >= 1.0f && Size.y >= 1.0f)
				m_WindowSize = Size;
			// the engine clears the frame, the panel background is the theme's job
#if UNITEED_GUI
			m_pGraphics->SetClearColor(engine::ui::Theme.m_Background.r, engine::ui::Theme.m_Background.g, engine::ui::Theme.m_Background.b);
#endif
		}
		// a windowed editor can play the sounds a map carries; the device is
		// opened here so the first preview is not delayed by it
		if(pGraphics != nullptr)
			InitSoundPreview();
#if UNITEED_GUI
		/*
			The pictograms of the chrome are SVG documents and they are parsed
			once, here; from then on the vector texture cache rasterizes them at
			whatever size the display asks for.
		*/
		if(pGraphics != nullptr)
		{
			m_VectorTextures.SetGraphics(pGraphics);
			// the icons are registered first, so the scale that follows says
			// how many drawings it is about
			editor_icons::RegisterAll(m_VectorTextures, storage::GetDataDirectory() / "icons");
			m_VectorTextures.SetDisplayScale(pGraphics->DisplayScale());
		}
#endif
		NewMap(50, 50);
		// the brush is a stamp from the start, so every path can read it
		SetBrushUniform(m_BrushSize);
		SetStatus( Tr("ready, use Ctrl+O to open a map") );
		return true;
	}

	void CEditor::InitHeadless()
	{
		Init(nullptr);
		m_WindowSize = base::vec2(1280.0f, 800.0f);
		FitToMap();
	}

	engine::CInput &CEditor::Input() { return m_pGraphics != nullptr ? m_pGraphics->Input() : m_HeadlessInput; }
	const engine::CInput &CEditor::Input() const { return m_pGraphics != nullptr ? m_pGraphics->Input() : m_HeadlessInput; }

	base::vec2 CEditor::WindowSize() const
	{
		if(m_pGraphics != nullptr)
		{
			const base::vec2 Size = m_pGraphics->WindowSize();
			if(Size.x >= 1.0f && Size.y >= 1.0f)
				return Size;
		}
		return m_WindowSize;
	}

	void CEditor::SetWindowSize(base::vec2 Size)
	{
		if(Size.x >= 1.0f && Size.y >= 1.0f)
			m_WindowSize = Size;
	}

	void CEditor::UpdateHeadless()
	{
		// one frame of the model: the keyboard, then the shortcuts and the
		// canvas, exactly the order Update() uses
		engine::CInput &In = Input();
		In.NewFrame();
		EnsureViewStates();
		ClampSelection();
		// a popup owns the mouse and the keyboard here for the same reason it
		// does in the window, and the picker is the one a windowless frame can
		// still close: clicking away from it
		if(m_bShowColor || Renaming())
		{
			HandleColorPopupInput();
			HandleRenameInput();
			In.EndFrame();
			return;
		}
		HandleShortcuts();
		const base::vec2 Window = WindowSize();
		const base::rect Canvas = CanvasRect(Window);
		/*
			The view transform works in window coordinates, so the layout the
			window does before it hit tests has to happen here too: without it a
			scripted frame would test the cursor against a canvas that is not
			where it draws.
		*/
		m_View.m_Origin = Canvas.m_TopLeft;
		m_View.m_Viewport = base::vec2(Canvas.Width(), Canvas.Height());
		m_LayerView = SelectionView();
		HandleCanvasInput(Canvas);
		In.EndFrame();
	}

	/*
		The dock layout. The two side docks are resizable and collapsible, which
		is the part of a Krita style workspace that changes how the editor is
		used: a mapmaker who is painting tiles gives the palette room, one who
		is drawing an envelope curve gives the timeline room. The tab strip on
		top of each dock switches between the workspaces that live on that side.
	*/
	float CEditor::DockWidth() const
	{
		// the dock column is one width, shared by both stacked dockers: the
		// splitters are for the height between them, not for two columns
		if(DockCollapsed(dock_slot::right_upper) && DockCollapsed(dock_slot::right_lower))
			return 0.0f;
		// one width for the column, taken from whichever docker is showing
		const dock_panel Panel = m_RightDock[DockCollapsed(dock_slot::right_upper) ? 1 : 0];
		const float Width = m_aDocks[static_cast<std::size_t>(Panel)].m_Width;
		return base::clamp(Width, 160.0f, 720.0f);
	}

	bool CEditor::DockCollapsed(dock_slot Slot) const
	{
		return Slot == dock_slot::right_upper ? m_bUpperCollapsed : m_bLowerCollapsed;
	}

	void CEditor::SetDockCollapsed(dock_slot Slot, bool bCollapsed)
	{
		bool &Flag = Slot == dock_slot::right_upper ? m_bUpperCollapsed : m_bLowerCollapsed;
		Flag = bCollapsed;
	}

	/*
		LeftEdge() is where the canvas starts on the left, which is the tool box
		and nothing else: the dockers are all on the right.
	*/
	float CEditor::LeftEdge() const
	{
		return m_bShowToolBox ? ToolBoxWidth : 0.0f;
	}

	base::rect CEditor::DockTabRow(dock_slot Slot, const base::vec2 &Window) const
	{
		const base::rect Area = DockArea(Slot, Window);
		if(Area.Width() <= 0.0f)
			return base::rect();
		return base::rect::FromSize(Area.m_TopLeft, base::vec2(Area.Width(), DockTabHeight));
	}

	base::rect CEditor::DockContent(dock_slot Slot, const base::vec2 &Window) const
	{
		const base::rect Area = DockArea(Slot, Window);
		if(Area.Width() <= 0.0f)
			return base::rect();
		const float Top = Area.m_TopLeft.y + DockTabHeight;
		return base::rect::FromSize(base::vec2(Area.m_TopLeft.x, Top),
			base::vec2(Area.Width(), std::max(Area.m_BottomRight.y - Top, 1.0f)));
	}

	/*
		The vertical split: the upper docker takes what is left above the lower
		one, and the lower one keeps the height the map maker gave it. The two
		collapsed states are what make one of them take the whole column.
	*/
	base::rect CEditor::DockArea(dock_slot Slot, const base::vec2 &Window) const
	{
		const float Width = DockWidth();
		if(Width <= 0.0f)
			return base::rect();
		const float X = Window.x - Width;
		const float Top = ChromeHeight;
		const float Bottom = Window.y - StatusHeight;
		const float Height = std::max(Bottom - Top, 1.0f);

		if(DockCollapsed(Slot))
		{
			/*
				A collapsed docker keeps its tab strip, which is what makes it
				expandable again: the upper one at the top of the column, the
				lower one at the bottom.
			*/
			const float Strip = std::min(DockTabHeight, Height);
			return base::rect::FromSize(base::vec2(X, Slot == dock_slot::right_upper ? Top : Bottom - Strip),
				base::vec2(Width, Strip));
		}

		if(Slot == dock_slot::right_upper)
		{
			const float Split = m_bLowerCollapsed ? Height : std::max(Height - m_RightSplit - DockSplitterHeight, MinDockHeight);
			return base::rect::FromSize(base::vec2(X, Top), base::vec2(Width, std::max(Split, 1.0f)));
		}
		const float Lower = m_bUpperCollapsed ? Height : std::min(m_RightSplit, std::max(Height - MinDockHeight - DockSplitterHeight, MinDockHeight));
		return base::rect::FromSize(base::vec2(X, Bottom - Lower), base::vec2(Width, std::max(Lower, 1.0f)));
	}

	base::rect CEditor::DockSplitter(dock_slot, const base::vec2 &Window) const
	{
		// the column edge against the canvas; one splitter serves both docks
		const float Width = DockWidth();
		if(Width <= 0.0f)
			return base::rect();
		return base::rect::FromSize(base::vec2(Window.x - Width - DockSplitterWidth, ChromeHeight),
			base::vec2(DockSplitterWidth, std::max(Window.y - ChromeHeight - StatusHeight, 1.0f)));
	}

	base::rect CEditor::DockSplitterH(const base::vec2 &Window) const
	{
		const float Width = DockWidth();
		if(Width <= 0.0f || DockCollapsed(dock_slot::right_upper) || DockCollapsed(dock_slot::right_lower))
			return base::rect();
		const float Bottom = Window.y - StatusHeight;
		const float Height = std::max(Bottom - ChromeHeight, 1.0f);
		const float Split = std::max(Height - m_RightSplit - DockSplitterHeight, MinDockHeight);
		return base::rect::FromSize(base::vec2(Window.x - Width, ChromeHeight + Split),
			base::vec2(Width, DockSplitterHeight));
	}

	base::rect CEditor::CanvasRect(const base::vec2 &Window) const
	{
		// the canvas is what the toolbar, the toolbox, the dock column and the
		// status bar leave over; clamped so a tiny window still gives a usable
		// viewport
		const float Left = LeftEdge();
		const float Right = DockWidth();
		return base::rect::FromSize(base::vec2(Left, ChromeHeight),
			base::vec2(std::max(Window.x - Left - Right, 1.0f), std::max(Window.y - ChromeHeight - StatusHeight, 1.0f)));
	}

	const std::vector<CEditor::dock_panel> &CEditor::DefaultTabs(dock_slot Slot)
	{
		/*
			The starting layout is Krita's: the workspaces that are about the
			thing being edited stack above, and the layer tree - the one a map
			maker keeps an eye on - is at the bottom.
		*/
		static const std::vector<dock_panel> s_aUpper = {
			dock_panel::tiles, dock_panel::assets, dock_panel::envelopes, dock_panel::rules};
		static const std::vector<dock_panel> s_aLower = {
			dock_panel::layers};
		return Slot == dock_slot::right_upper ? s_aUpper : s_aLower;
	}

	std::vector<CEditor::dock_panel> &CEditor::Tabs(dock_slot Slot)
	{
		std::vector<dock_panel> &Tabs = Slot == dock_slot::right_upper ? m_vRightTabs : m_vLowerTabs;
		if(Tabs.empty())
			Tabs = DefaultTabs(Slot);
		return Tabs;
	}

	const std::vector<CEditor::dock_panel> &CEditor::Tabs(dock_slot Slot) const
	{
		const std::vector<dock_panel> &Tabs = Slot == dock_slot::right_upper ? m_vRightTabs : m_vLowerTabs;
		return Tabs.empty() ? DefaultTabs(Slot) : Tabs;
	}

	const char *CEditor::TabLabel(dock_panel Panel)
	{
		switch(Panel)
		{
		case dock_panel::layers: return Tr("Layers");
		case dock_panel::tiles: return Tr("Tiles");
		case dock_panel::assets: return Tr("Assets");
		case dock_panel::envelopes: return Tr("Envelopes");
		case dock_panel::rules: return Tr("Rules");
		case dock_panel::count: break;
		}
		return "?";
	}

	const char *CEditor::TabHint(dock_panel Panel)
	{
		switch(Panel)
		{
		case dock_panel::layers: return Tr("the groups and layers of the map, with visibility and locks");
		case dock_panel::tiles: return Tr("the tile sheet of the selected layer, or the entity sheet of a game layer");
		case dock_panel::assets: return Tr("the images and sounds the map carries");
		case dock_panel::envelopes: return Tr("the curve editor: the map's envelopes and their keyframes");
		case dock_panel::rules: return Tr("the autorules: configurations, runs and index rules");
		case dock_panel::count: break;
		}
		return "";
	}

	dock_slot CEditor::HomeSlot(dock_panel Panel)
	{
		// the layer tree is the bottom docker, which is where Krita keeps it
		// and where a map maker looks for it; everything else stacks above
		return Panel == dock_panel::layers ? dock_slot::right_lower : dock_slot::right_upper;
	}

	bool CEditor::PanelShown(dock_panel Panel) const
	{
		for(const dock_slot Slot : {dock_slot::right_upper, dock_slot::right_lower})
			if(DockPanel(Slot) == Panel)
				return !DockCollapsed(Slot);
		return false;
	}

	void CEditor::TogglePanel(dock_panel Panel)
	{
		// a workspace that is showing anywhere is folded away where it is,
		// rather than being moved to the docker it started in
		for(const dock_slot Slot : {dock_slot::right_upper, dock_slot::right_lower})
		{
			if(DockPanel(Slot) != Panel)
				continue;
			SetDockCollapsed(Slot, !DockCollapsed(Slot));
			return;
		}
		SetDockTab(HomeSlot(Panel), Panel);
	}

	void CEditor::SetDockTab(dock_slot Slot, dock_panel Panel)
	{
		/*
			A workspace lives in one docker at a time. Picking a tab that the
			other docker shows swaps the two, which is what a tab click means
			everywhere else and never leaves the same workspace drawn twice.
		*/
		const int Index = Slot == dock_slot::right_upper ? 0 : 1;
		const int OtherIndex = 1 - Index;
		if(m_RightDock[OtherIndex] == Panel)
			m_RightDock[OtherIndex] = m_RightDock[Index];
		m_RightDock[Index] = Panel;
		if(Slot == dock_slot::right_upper)
			m_bUpperCollapsed = false;
		else
			m_bLowerCollapsed = false;
		// the width follows whichever docker is now showing
		m_aDocks[static_cast<std::size_t>(m_RightDock[OtherIndex])].m_Width =
			m_aDocks[static_cast<std::size_t>(Panel)].m_Width;
	}

	base::rect CEditor::ToolBoxRect(const base::vec2 &Window) const
	{
		if(!m_bShowToolBox)
			return base::rect();
		return base::rect::FromSize(base::vec2(0.0f, ChromeHeight),
			base::vec2(ToolBoxWidth, std::max(Window.y - ChromeHeight - StatusHeight, 1.0f)));
	}

	base::rect CEditor::MapBounds() const
	{
		// the layers of a map normally all have the same size, but nothing
		// forces that, so the boundary is the union of the tile layers
		bool bAny = false;
		base::vec2 Max;
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
			{
				if(!Layer.IsTiles() || Layer.m_Width <= 0 || Layer.m_Height <= 0)
					continue;
				const base::vec2 LayerMax(static_cast<float>(Layer.m_Width) * TileSize, static_cast<float>(Layer.m_Height) * TileSize);
				Max.x = bAny ? std::max(Max.x, LayerMax.x) : LayerMax.x;
				Max.y = bAny ? std::max(Max.y, LayerMax.y) : LayerMax.y;
				bAny = true;
			}
		return bAny ? base::rect(base::vec2(0.0f, 0.0f), Max) : base::rect();
	}

	CLayer *CEditor::SelectedLayer()
	{
		if(m_SelectedGroup < 0 || m_SelectedGroup >= static_cast<int>(m_Map.m_vGroups.size()))
			return nullptr;
		CGroup &Group = m_Map.m_vGroups[static_cast<std::size_t>(m_SelectedGroup)];
		if(m_SelectedLayer < 0 || m_SelectedLayer >= static_cast<int>(Group.m_vLayers.size()))
			return nullptr;
		return &Group.m_vLayers[static_cast<std::size_t>(m_SelectedLayer)];
	}

	const CLayer *CEditor::SelectedLayer() const { return const_cast<CEditor *>(this)->SelectedLayer(); }

	CLayer *CEditor::LayerAt(int Group, int Layer)
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return nullptr;
		CGroup &GroupRef = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer < 0 || Layer >= static_cast<int>(GroupRef.m_vLayers.size()))
			return nullptr;
		return &GroupRef.m_vLayers[static_cast<std::size_t>(Layer)];
	}

	const CLayer *CEditor::LayerAt(int Group, int Layer) const { return const_cast<CEditor *>(this)->LayerAt(Group, Layer); }

	CGroup *CEditor::SelectedGroup()
	{
		if(m_SelectedGroup < 0 || m_SelectedGroup >= static_cast<int>(m_Map.m_vGroups.size()))
			return nullptr;
		return &m_Map.m_vGroups[static_cast<std::size_t>(m_SelectedGroup)];
	}

	const CGroup *CEditor::SelectedGroup() const { return const_cast<CEditor *>(this)->SelectedGroup(); }

	/*
		What the canvas edits follows from the selection, not from a mode the
		user has to keep in sync: select a tile layer and the tile brush is
		there, select a quad layer and the points are draggable, select a layer
		that cannot be edited and the canvas says so instead of silently
		painting into the wrong layer.
	*/
	edit_mode CEditor::Mode() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return edit_mode::none;
		if(pLayer->IsQuads())
			return edit_mode::quads;
		if(pLayer->IsTiles())
		{
			const CLayerView *pView = m_SelectedGroup >= 0 && m_SelectedGroup < static_cast<int>(m_vViewStates.size()) &&
				m_SelectedLayer >= 0 && m_SelectedLayer < static_cast<int>(m_vViewStates[static_cast<std::size_t>(m_SelectedGroup)].size()) ?
				&m_vViewStates[static_cast<std::size_t>(m_SelectedGroup)][static_cast<std::size_t>(m_SelectedLayer)] : nullptr;
			if(pView != nullptr && pView->m_Locked)
				return edit_mode::none;
			return edit_mode::tiles;
		}
		return edit_mode::none;
	}

	bool CEditor::EnsureViewStates()
	{
		const std::size_t Groups = m_Map.m_vGroups.size();
		if(m_vViewStates.size() != Groups)
			m_vViewStates.resize(Groups);
		for(std::size_t g = 0; g < Groups; g++)
		{
			std::vector<CLayerView> &States = m_vViewStates[g];
			const std::size_t Layers = m_Map.m_vGroups[g].m_vLayers.size();
			const std::size_t Old = States.size();
			if(States.size() != Layers)
				States.resize(Layers);
			// a physics layer is read only in this version of the editor: its
			// payload carries the numbers, not the tile array
			for(std::size_t l = Old; l < Layers; l++)
			{
				const CLayer &Layer = m_Map.m_vGroups[g].m_vLayers[l];
				if(Layer.IsPhysicsLayer())
				{
					States[l].m_Visible = false;
					States[l].m_Locked = true;
				}
			}
		}
		return true;
	}

	CLayerView *CEditor::ViewState(int Group, int Layer)
	{
		if(Group < 0 || Group >= static_cast<int>(m_vViewStates.size()))
			return nullptr;
		if(Layer < 0 || Layer >= static_cast<int>(m_vViewStates[static_cast<std::size_t>(Group)].size()))
			return nullptr;
		return &m_vViewStates[static_cast<std::size_t>(Group)][static_cast<std::size_t>(Layer)];
	}

	void CEditor::ClampSelection()
	{
		if(m_Map.m_vGroups.empty())
		{
			m_SelectedGroup = 0;
			m_SelectedLayer = 0;
			m_vSelectedQuads.clear();
			return;
		}
		m_SelectedGroup = base::clamp(m_SelectedGroup, 0, static_cast<int>(m_Map.m_vGroups.size()) - 1);
		const int Layers = static_cast<int>(m_Map.m_vGroups[static_cast<std::size_t>(m_SelectedGroup)].m_vLayers.size());
		/*
			A negative layer is the group row itself, which is a selection the
			right panel answers with the group's own properties; everything
			else clamps into the layers that exist.
		*/
		if(m_SelectedLayer >= 0 || Layers == 0)
			m_SelectedLayer = Layers > 0 ? base::clamp(m_SelectedLayer, 0, Layers - 1) : 0;
		else
			m_SelectedLayer = -1;

		/*
			The quad selection lives here rather than in the layer, so it has to
			be pruned whenever the layer underneath it can have changed: an undo
			of a quad edit, a load, or a delete all leave indices that point at
			something else. A stale index would otherwise be dragged into a quad
			the user never selected.
		*/
		if(m_vSelectedQuads.empty())
			return;
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
		{
			ClearQuadSelection();
			return;
		}
		const int Count = static_cast<int>(pLayer->m_vQuads.size());
		m_vSelectedQuads.erase(std::remove_if(m_vSelectedQuads.begin(), m_vSelectedQuads.end(),
			[Count](int Index) { return Index < 0 || Index >= Count; }), m_vSelectedQuads.end());
		if(m_vSelectedQuads.empty())
			ClearQuadSelection();
		else if(m_DragQuad >= Count)
		{
			m_DragQuad = -1;
			m_DragPoint = -1;
			m_bQuadDragged = false;
		}
		// a load or an undo can leave the move pointing at a quad that is gone
		if(m_bQuadMoving && std::any_of(m_vQuadMove.begin(), m_vQuadMove.end(),
			[Count](const CQuadMoveStart &Start) { return Start.m_Quad < 0 || Start.m_Quad >= Count; }))
		{
			m_bQuadMoving = false;
			m_bQuadMoved = false;
			m_bQuadMoveUndoPushed = false;
			m_vQuadMove.clear();
		}
	}

	bool CEditor::SetupVersionForMap(bool bMapDecidesVersion)
	{
		// which mapres directory matches what the map was built for; the rule
		// lives with the assets so the headless renderer picks the same set
		const std::string Version = MapVersionId(m_Map);

		m_VersionId = Version;
		/*
			The *target* is a different question and only moves when the map has
			really decided: a session that opened a project keeps the version
			that project recorded, so re-saving cannot drag the assets back to
			the editor's default.
		*/
		if(bMapDecidesVersion)
			m_TargetVersionId = Version;
		SetupResourcePaths(m_VersionId);
		LOG_INFO("editor", "assets of version '{}' selected for this map", m_VersionId.c_str());
		return true;
	}

	void CEditor::NewMap(int Width, int Height)
	{
		m_Map.InitNew("untitled");
		m_Map.m_aAuthor = "";
		for(CGroup &Group : m_Map.m_vGroups)
			for(CLayer &Layer : Group.m_vLayers)
				if(Layer.IsTiles())
					Layer.Resize(std::max(Width, 4), std::max(Height, 4));
		m_Package.Clear();
		m_bPackageLoaded = false;
		m_SourcePath.clear();
		m_bSessionSeeded = false;
		m_bMapVersionKnown = false;
		m_Dirty = false;
		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedGroup = 0;
		m_SelectedLayer = 0;
		SetupVersionForMap();
		ResetTextures();
		m_vUndo.clear();
		m_vRedo.clear();
		FitToMap();
		Notify( Tr("new map") );
	}

	bool CEditor::OpenFile(const std::string &Path)
	{
		/*
			One entry point, so nothing has to know which kind of file this is: a
			.unitwp carries the editor session and the autorules as well as the
			map, and reading it as a bare map would silently throw both away.
		*/
		if(IsProjectFile(Path) || base::EndsWith(Path, CUnitwpPackage::Extension))
			return LoadProject(Path);

		std::string Error;
		const std::optional<storage::byte_vector> Bytes = storage::read(Path);
		if(!Bytes.has_value())
		{
			Notify(base::FormatRuntime(Tr("cannot read {}"), Path));
			return false;
		}

		if(CUnitwpPackage::IsPackage(*Bytes))
		{
			CUnitwpPackage Package;
			if(!Package.Deserialise(*Bytes, &Error))
			{
				Notify(base::FormatRuntime(Tr("package error: {}"), Error));
				return false;
			}
			m_Package = Package;
			m_bPackageLoaded = true;
			m_Map = Package.m_Map;
		}
		else
		{
			CMap Map;
			if(!Map.LoadFromBytes(*Bytes, &Error))
			{
				Notify(base::FormatRuntime(Tr("map error: {}"), Error));
				return false;
			}
			m_Map = Map;
			m_Package.Clear();
			m_bPackageLoaded = false;
			std::string Name = storage::path(Path).stem().string();
			m_Package.Adopt(m_Map, Name, Path);
			m_Package.m_vResources.clear();
		}

		m_SourcePath = Path;
		m_bSessionSeeded = true;
		// opening anything means the welcome page has done its job
		m_bShowWelcome = false;
		// a bare map is what says which mapres it needs
		m_bMapVersionKnown = true;
		m_Dirty = false;
		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedGroup = 0;
		m_SelectedLayer = 0;
		SetupVersionForMap();
		ResetTextures();
		m_vUndo.clear();
		m_vRedo.clear();
		FitToMap();

		const CMapStatistics Stats = m_Map.Statistics();
		Notify(  base::FormatRuntime(Tr("opened {} (v{:d}, {:d} groups, {:d} layers, {:d} images)"),
			storage::path(Path).filename().string().c_str(), m_Map.m_SourceDatafileVersion,
			Stats.m_Groups, Stats.m_Layers, Stats.m_Images)  );
		return true;
	}

	bool CEditor::SavePackage(const std::string &Path)
	{
		std::string Error;
		CUnitwpPackage Package = m_Package;
		Package.m_Map = m_Map;
		if(Package.m_Meta.m_PackageId.empty())
			Package.Adopt(m_Map, storage::path(Path).stem().string(), m_SourcePath);
		// the same rule as the project: only an explicitly chosen target
		// replaces the one the package already carried
		if(m_Target != map_target::tee08)
			Package.m_Meta.m_ExportTarget = GetTargetProfile(m_Target).m_pId;
		else if(const std::optional<map_target> Stored = TargetFromId(Package.m_Meta.m_ExportTarget))
			m_Target = *Stored;
		Package.m_Meta.m_Assets = VersionForSave();
		Package.m_Meta.m_Author = m_Map.m_aAuthor;
		Package.m_Meta.m_Updated = std::to_string(base::UnixTime());
		/*
			A package written from the editor is also a project: the session and
			the autorules go in, so the same file can be reopened and carried on
			with. --unpack and the export map path ignore both chunks.
		*/
		Package.m_EditorState = m_Project.MergeEditorState(EditorState()).dump(1);
		Package.m_Rules = rules::ToJson(m_RuleSet).dump(1);

		// attach the mapres files the map references, so the package travels on
		// its own. Only the images the map actually uses are embedded.
		Package.AttachMapResources();

		if(!Package.SaveToFile(Path, &Error))
		{
			Notify(base::FormatRuntime(Tr("save failed: {}"), Error));
			return false;
		}
		m_Package = Package;
		m_bPackageLoaded = true;
		m_SourcePath = Path;
		m_Dirty = false;
		Notify(  base::FormatRuntime(Tr("saved {} ({:d} resources)"), storage::path(Path).filename().string().c_str(), Package.m_vResources.size())  );
		return true;
	}

	bool CEditor::ExportMap(const std::string &Path, map_target Target)
	{
		std::string Error;
		for(const std::string &Warning : m_Map.CompatibilityWarnings(Target))
			LOG_WARN("editor", "{}", Warning.c_str());
		if(!m_Map.SaveToFile(Path, Target, &Error))
		{
			Notify(base::FormatRuntime(Tr("export failed: {}"), Error));
			return false;
		}
		m_Target = Target;
		const CMapTargetProfile &Profile = GetTargetProfile(Target);
		Notify(  base::FormatRuntime(Tr("exported {} for {} (datafile v{:d})"),
			storage::path(Path).filename().string().c_str(), Profile.m_pName, Profile.m_DatafileVersion)  );
		return true;
	}

	/*
		The status line is what the window says about the thing under the
		cursor or the operation that just ran, and it changes on every mouse
		move because hovering a widget reports what the widget does. Writing
		it to the log as well turned a session into a wall of tips, so the
		log is left to Notify(), which the operations that are worth a line
		call themselves.
	*/
	void CEditor::SetStatus(const std::string &Message)
	{
		m_Status = Message;
		m_StatusTime = base::Now();
	}

	/*
		An operation that ran: the status line says so and the log keeps a
		record of it, which is what a script driving the editor reads back.
	*/
	void CEditor::Notify(const std::string &Message)
	{
		SetStatus(Message);
		LOG_INFO("editor", "{}", Message.c_str());
	}

	void CEditor::SetTarget(map_target Target)
	{
		m_Target = Target;
		m_TargetVersionId = VersionIdForTarget(Target);
		/*
			The target decides which mapres set the map is read with, so picking
			one has to move the assets with it: the resource paths are re-armed
			and the texture cache is dropped, or a map switched from 0.6 to 0.8
			would keep drawing the tiles it had already loaded. This is also the
			moment the map stops deciding the version, because the user just did.
		*/
		m_VersionId = m_TargetVersionId;
		m_bMapVersionKnown = false;
		SetupResourcePaths(m_VersionId);
		LOG_INFO("editor", "assets of version '{}' selected for the target", m_VersionId.c_str());
		ResetTextures();
	}

	void CEditor::SetSelectedLayer(int Group, int Layer)
	{
		m_SelectedGroup = Group;
		m_SelectedLayer = Layer;
		/*
			A scrub belongs to the group it started on: another group's field
			has the same id, so leaving the drag live would carry the numbers
			across the two.
		*/
		m_DragGroupField = -1;
		ClampSelection();
	}

	/*
		Renaming a layer or a group from the layer tree. The row that is being
		edited keeps the keyboard while the field is open - Update() skips the
		canvas and the shortcuts for it - so what is typed is a name and never a
		tool. Layer < 0 means the group row itself.
	*/
	void CEditor::BeginRename(int Group, int Layer)
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &Target = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer >= static_cast<int>(Target.m_vLayers.size()))
			return;
		if(Layer >= 0)
			m_RenameText = Target.m_vLayers[static_cast<std::size_t>(Layer)].m_Name;
		else
			m_RenameText = Target.m_Name;
		m_RenameGroup = Group;
		m_RenameLayer = Layer;
		// the name that is there starts selected, so typing replaces it
		m_bRenameAll = true;
		SetStatus(  base::FormatRuntime(Tr("type a name for {}, Enter to keep it, Escape to leave it"),
			Layer >= 0 ? Tr("the layer") : Tr("the group"))  );
	}

	void CEditor::CancelRename()
	{
		if(!Renaming())
			return;
		m_RenameGroup = -1;
		m_RenameLayer = -1;
		m_RenameText.clear();
		m_bRenameAll = true;
		Notify( Tr("the name is unchanged") );
	}

	void CEditor::CommitRename()
	{
		if(!Renaming())
			return;
		const int Group = m_RenameGroup;
		const int Layer = m_RenameLayer;
		std::string Name = m_RenameText;
		m_RenameGroup = -1;
		m_RenameLayer = -1;
		m_RenameText.clear();
		m_bRenameAll = true;

		if(Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &Target = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer >= static_cast<int>(Target.m_vLayers.size()))
			return;
		std::string &Current = Layer >= 0 ? Target.m_vLayers[static_cast<std::size_t>(Layer)].m_Name : Target.m_Name;
		if(Current == Name)
			return;
		/*
			A name is part of the map, so the change is undoable like any other:
			the whole scope is the one that carries the layer list, and a rename
			is one step rather than one per keystroke.
		*/
		PushUndo(Group, Layer >= 0 ? Layer : 0, undo_scope::whole);
		Current = std::move(Name);
		MarkDirty();
		LOG_TRACE("editor", "renamed group {:d} layer {:d} to '{}'", Group, Layer, Current.c_str());
		Notify(  base::FormatRuntime(Tr("renamed to '{}'"), Current.empty() ? Tr("(no name)") : Current.c_str())  );
	}

	void CEditor::HandleRenameInput()
	{
		if(!Renaming())
			return;
		engine::CInput &Input = this->Input();
		const std::string &Typed = Input.TypedText();
		if(!Typed.empty())
		{
			// the first letter replaces the selected name rather than being
			// appended to it, which is what a rename does everywhere else
			if(m_bRenameAll)
				m_RenameText.clear();
			m_bRenameAll = false;
			m_RenameText += Typed;
		}
		if(Input.BackspacePressed())
		{
			if(m_bRenameAll)
				m_RenameText.clear();
			else if(!m_RenameText.empty())
				m_RenameText.pop_back();
			m_bRenameAll = false;
		}
		if(Input.KeyPressed(engine::keys::Return) || Input.KeyPressed(engine::keys::KpEnter))
			CommitRename();
		else if(Input.KeyPressed(engine::keys::Escape))
			CancelRename();
	}

	void CEditor::ForgetImageTextures()
	{
		/*
			The texture keys are per image name, and every tint copy is derived
			from one. Forgetting only the image textures (rather than all of
			them) keeps the entity sheets and the font atlas, which have nothing
			to do with the map's assets. This lives here rather than beside the
			drawing code because the headless build needs it too.
		*/
#if UNITEED_GUI
		if(m_pGraphics != nullptr)
		{
			for(const CImage &Image : m_Map.m_vImages)
			{
				m_pGraphics->ForgetTexture("embedded:" + Image.m_Name);
				m_pGraphics->ForgetTexture("mapres:" + Image.m_Name);
			}
			for(const CTintSlot &Slot : m_vTintSlots)
				m_pGraphics->ForgetTexture(TintSlotKey(static_cast<std::size_t>(Slot.m_Image)));
		}
#endif
		m_vTintSlots.clear();
		m_SourceImages.clear();
		m_TintClock = 0;
	}

	void CEditor::ResetTextures()
	{
#if UNITEED_GUI
		if(m_pGraphics != nullptr)
			m_pGraphics->ForgetTextures();
		/*
			Every texture the graphics cache held is gone, so the caches that
			remember a *handle* rather than a key have to let go of it: the two
			sprite sheets and the vector icons of the interface. Those that look
			their texture up by key - the mapres sheets, the asset thumbnails -
			simply find nothing and load again, which is why they never had this
			problem. Leaving a stale handle behind is what made the tool strip
			and the entity sheet vanish when another map was opened.
		*/
		m_EntityTexture[0] = -1;
		m_EntityTexture[1] = -1;
		for(int &Texture : m_PhysicsTexture)
			Texture = -1;
		m_VectorTextures.Forget();
#endif
		// the tint slots point at textures that no longer exist, and the decoded
		// mapres pixels belong to the map that was just replaced
		m_vTintSlots.clear();
		m_SourceImages.clear();
		m_TintClock = 0;
	}

	void CEditor::FitToMap()
	{
		float MaxX = 0.0f, MaxY = 0.0f;
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
				if(Layer.IsTiles())
				{
					MaxX = std::max(MaxX, Layer.m_Width * TileSize);
					MaxY = std::max(MaxY, Layer.m_Height * TileSize);
				}
		if(MaxX <= 0.0f || MaxY <= 0.0f)
		{
			MaxX = 1600.0f;
			MaxY = 1600.0f;
		}

		const base::rect Canvas = CanvasRect(WindowSize());
		const float ZoomX = (Canvas.Width() - 48.0f) / MaxX;
		const float ZoomY = (Canvas.Height() - 48.0f) / MaxY;
		m_View.m_Zoom = base::clamp(std::min(ZoomX, ZoomY), 0.05f, 4.0f);
		m_View.m_Center = base::vec2(MaxX * 0.5f, MaxY * 0.5f);
	}

	// ----------------------------------------------------------------- colours
	/*
		The colour and point helpers are free functions in render.cpp, shared with
		the headless renderer: a layer colour and a quad's animated corners have
		to mean the same thing in a picture and in a render, so both go through
		one implementation.
	*/
	CColor CEditor::LayerColor(const CLayer &Layer) const
	{
		return LayerColorAt(m_Map, Layer, m_EnvelopeTime);
	}

	void CEditor::QuadColors(const CQuad &Quad, CColor *paColors) const
	{
		QuadColorsAt(m_Map, Quad, m_EnvelopeTime, paColors);
	}

	base::view_transform CEditor::GroupView(const CGroup &Group) const { return game::GroupView(m_View, Group); }

	base::view_transform CEditor::SelectionView() const
	{
		// no selection, no group view: the plain one is then what the eye sees
		// while nothing is selected
		const CGroup *pGroup = SelectedGroup();
		return pGroup != nullptr ? GroupView(*pGroup) : m_View;
	}

	void CEditor::QuadPoints(const CQuad &Quad, base::vec2 *paPoints) const
	{
		QuadPointsAt(m_Map, Quad, m_EnvelopeTime, paPoints);
	}

	float CEditor::EnvelopeLoopLength() const
	{
		float Length = 0.0f;
		for(const CEnvelope &Envelope : m_Map.m_vEnvelopes)
			Length = std::max(Length, EnvelopeLength(Envelope));
		return Length > 0.05f ? Length : 1.0f;
	}

	bool CEditor::OpenColorPicker()
	{
		CColor Color;
		if(!PickerColor(Color))
			return false;
		m_PickerColor = Color;
		m_bShowColor = true;
		// nothing was clicked, so the popup has no point to sit beside
		m_PickerAnchor = base::vec2(-1.0f, -1.0f);
		// the hue is derived from the colour while the picker is drawn
		return true;
	}

	base::rect CEditor::ColorPopupRect() const
	{
		/*
			36 header + 130 square + 10 + 14 hue + 8 + 14 alpha + 12 + 22
			preview + 30 presets + 18 footer, plus the four corner swatches and
			the button under them when the picker edits a quad. The height is
			computed from the content rather than written twice: it used to be a
			fixed box, and the extra rows then drew past its bottom edge.
		*/
		const base::vec2 PopupSize(296.0f, 294.0f + (PickerTargetsQuad() ? 44.0f : 0.0f));
		const base::vec2 Window = WindowSize();
		if(m_PickerAnchor.x < 0.0f || m_PickerAnchor.y < 0.0f)
			return base::rect::FromSize(
				base::vec2(Window.x * 0.5f - PopupSize.x * 0.5f, Window.y * 0.5f - PopupSize.y * 0.5f), PopupSize);

		/*
			The popup belongs to the map, so it is placed beside the point it was
			opened from and kept over the canvas: the corner being coloured has
			to stay visible, or the click that chose it would hide it. A point
			that is itself in a corner of the window would push the popup past
			the edge, so it flips to the other side of the point first and is
			clamped last, for a canvas too small to hold it beside the point.
		*/
		const base::rect Canvas = CanvasRect(Window);
		const float Margin = 6.0f;
		const float Gap = 14.0f;
		float X = m_PickerAnchor.x + Gap;
		float Y = m_PickerAnchor.y + Gap;
		if(X + PopupSize.x > Canvas.m_BottomRight.x - Margin)
			X = m_PickerAnchor.x - Gap - PopupSize.x;
		if(Y + PopupSize.y > Canvas.m_BottomRight.y - Margin)
			Y = m_PickerAnchor.y - Gap - PopupSize.y;
		X = base::clamp(X, Canvas.m_TopLeft.x + Margin,
			std::max(Canvas.m_BottomRight.x - PopupSize.x - Margin, Canvas.m_TopLeft.x + Margin));
		Y = base::clamp(Y, Canvas.m_TopLeft.y + Margin,
			std::max(Canvas.m_BottomRight.y - PopupSize.y - Margin, Canvas.m_TopLeft.y + Margin));
		return base::rect::FromSize(base::vec2(X, Y), PopupSize);
	}

	void CEditor::HandleColorPopupInput()
	{
		if(!m_bShowColor)
			return;
		engine::CInput &Input = this->Input();
		/*
			Clicking anywhere but the popup is how it goes away, the way a popup
			behaves everywhere else; Escape stays as the other way out. The
			press is used up here - the frame skips the canvas - so dismissing
			the picker cannot also paint a tile or select a quad.
		*/
		const bool bPressed = Input.MousePressed(engine::CInput::mouse_button::left) ||
			Input.MousePressed(engine::CInput::mouse_button::middle) ||
			Input.MousePressed(engine::CInput::mouse_button::right) ||
			Input.MousePressed(engine::CInput::mouse_button::x1) ||
			Input.MousePressed(engine::CInput::mouse_button::x2);
		if(bPressed && !ColorPopupRect().Contains(Input.MousePosition()))
			m_bShowColor = false;
	}

	const char *CEditor::CornerName(int Corner)
	{
		// i18n:table
		static const char *const s_apName[4] = {"top left", "top right", "bottom left", "bottom right"};
		return Tr(s_apName[base::clamp(Corner, 0, 3)]);
	}

	bool CEditor::OpenQuadColorPickerAt(base::vec2 Screen, int QuadUnderCursor)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
			return false;

		/*
			The handle under the cursor names the corner, so nothing has to
			guess which one was meant. Without a handle the click was inside the
			shape, which colours the four corners at once; the pivot carries no
			colour, so it is reported rather than silently opening the picker on
			a corner that was not clicked.
		*/
		int Quad = QuadUnderCursor;
		int Corner = AllQuadCorners;
		const int Handle = QuadHandleAt(Screen);
		if(Handle >= 0)
		{
			Quad = m_vSelectedQuads[static_cast<std::size_t>(Handle / 5)];
			if(Handle % 5 == 4)
			{
				SetStatus( Tr("the pivot of a quad has no colour, right click one of its corners") );
				return false;
			}
			Corner = Handle % 5;
		}
		if(Quad < 0 || Quad >= static_cast<int>(pLayer->m_vQuads.size()))
			return false;

		// the quad that was clicked is the one the picker edits, so it becomes
		// the selection the popup and the corner swatches read
		m_vSelectedQuads.assign(1, Quad);
		SetQuadColorTarget(Corner);
		m_PickerColor = pLayer->m_vQuads[static_cast<std::size_t>(Quad)].m_aColors[Corner < 4 ? Corner : 0];
		m_bShowColor = true;
		// the point that was clicked is where the popup is placed, so the
		// corner it colours stays visible next to it
		m_PickerAnchor = Screen;
		SetStatus(  Corner < 4 ?
			base::FormatRuntime(Tr("the picker edits the {} corner of quad {:d}"), CornerName(Corner), Quad) :
			base::FormatRuntime(Tr("the picker edits all four corners of quad {:d}"), Quad)  );
		return true;
	}

	base::vec2 CEditor::QuadPointScreen(int Quad, int Point) const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads() || Quad < 0 || Quad >= static_cast<int>(pLayer->m_vQuads.size()) ||
			Point < 0 || Point > 4)
			return base::vec2(-1.0f, -1.0f);
		const CQuad &Quad2 = pLayer->m_vQuads[static_cast<std::size_t>(Quad)];
		const base::view_transform View = SelectionView();
		if(Point == 4)
			return View.WorldToScreen(base::vec2(base::FixedToFloat(Quad2.m_aPoints[4].x), base::FixedToFloat(Quad2.m_aPoints[4].y)));
		base::vec2 aWorld[4];
		QuadPoints(Quad2, aWorld);
		return View.WorldToScreen(aWorld[Point]);
	}

	bool CEditor::PickerTargetsQuad() const
	{
		const CLayer *pLayer = SelectedLayer();
		return pLayer != nullptr && pLayer->IsQuads();
	}

	void CEditor::SetQuadColorTarget(int Target)
	{
		m_QuadColorTarget = base::clamp(Target, 0, AllQuadCorners);
	}

	bool CEditor::PickerColor(CColor &Color) const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return false;
		if(pLayer->IsQuads())
		{
			// the first selected quad, or the first one of the layer when
			// nothing was clicked yet
			int Index = m_vSelectedQuads.empty() ? 0 : m_vSelectedQuads[0];
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				return false;
			// a single corner target edits that corner, the all-corners target
			// the first one, which is what the swatch in the layer panel shows
			const int Corner = m_QuadColorTarget < 4 ? m_QuadColorTarget : 0;
			Color = pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_aColors[Corner];
			return true;
		}
		if(pLayer->IsTiles())
		{
			Color = pLayer->m_Color;
			return true;
		}
		return false;
	}

	void CEditor::SetPickerColor(const CColor &Color)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return;
		if(pLayer->IsQuads())
		{
			// without a click selection the whole layer is the target, which is
			// what the picker title says
			std::vector<int> Indices = m_vSelectedQuads;
			if(Indices.empty())
			{
				Indices.resize(pLayer->m_vQuads.size());
				for(std::size_t i = 0; i < Indices.size(); i++)
					Indices[i] = static_cast<int>(i);
			}
			// and the corner target narrows it to one corner per quad, which is
			// what makes a gradient editable
			const int First = m_QuadColorTarget < 4 ? m_QuadColorTarget : 0;
			const int Last = m_QuadColorTarget < 4 ? m_QuadColorTarget : 3;
			for(const int Index : Indices)
			{
				if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
					continue;
				for(int i = First; i <= Last; i++)
					pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_aColors[i] = Color;
			}
			m_Dirty = true;
			return;
		}
		if(pLayer->IsTiles())
		{
			pLayer->m_Color = Color;
			m_Dirty = true;
		}
	}

	std::string CEditor::PickerTargetName() const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr)
			return Tr("nothing");
		const char *pName = pLayer->m_Name.empty() ? Tr("layer") : pLayer->m_Name.c_str();
		if(pLayer->IsQuads())
		{
			// which corner is being edited has to be visible in the title, or
			// the picker looks like it colours everything at once
			const std::string Corner = m_QuadColorTarget < 4 ?
				base::FormatRuntime(Tr(" ({} corner)"), CornerName(m_QuadColorTarget)) :
				std::string(Tr(" (all corners)"));
			if(m_vSelectedQuads.empty())
				return (pLayer->m_vQuads.size() == 1 ? base::FormatRuntime(Tr("the quad of '{}'"), pName) :
					base::FormatRuntime(Tr("all {:d} quads of '{}'"), pLayer->m_vQuads.size(), pName)) + Corner;
			return base::FormatRuntime(Tr("{:d} of {:d} quads of '{}'"), m_vSelectedQuads.size(), pLayer->m_vQuads.size(), pName) + Corner;
		}
		if(pLayer->IsTiles())
			return base::FormatRuntime(Tr("layer '{}'"), pName);
		return Tr("nothing");
	}

	// ------------------------------------------------------------------ undo
	CEditor::CUndoState CEditor::CaptureLayer(const CLayer &Layer, int Group, int LayerIndex, undo_scope Scope) const
	{
		CUndoState State;
		State.m_Scope = Scope;
		State.m_Group = Group;
		State.m_Layer = LayerIndex;
		State.m_Width = Layer.m_Width;
		State.m_Height = Layer.m_Height;
		if(Scope == undo_scope::tiles)
			State.m_vTiles = Layer.m_vTiles;
		else if(Scope == undo_scope::quads)
			State.m_vQuads = Layer.m_vQuads;
		else if(Scope == undo_scope::colour)
			State.m_Color = Layer.m_Color;
		else
		{
			// a rule run, a layer add or a curve edit can touch anything, so the
			// step carries everything the editor can change
			State.m_vGroups = m_Map.m_vGroups;
			State.m_vEnvelopes = m_Map.m_vEnvelopes;
			State.m_vImages = m_Map.m_vImages;
		}
		return State;
	}

	void CEditor::PushUndo(int Group, int Layer, undo_scope Scope)
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		const CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer < 0 || Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		const CLayer &L = G.m_vLayers[static_cast<std::size_t>(Layer)];
		if(Scope == undo_scope::tiles && !L.IsTiles())
			return;
		if(Scope == undo_scope::quads && !L.IsQuads())
			return;
		if(Scope == undo_scope::colour && !L.IsTiles())
			return;

		m_vUndo.push_back(CaptureLayer(L, Group, Layer, Scope));
		if(m_vUndo.size() > 32)
			m_vUndo.erase(m_vUndo.begin());
		m_vRedo.clear();
	}

	void CEditor::Undo()
	{
		if(m_vUndo.empty())
		{
			Notify( Tr("nothing to undo") );
			return;
		}
		CUndoState State = std::move(m_vUndo.back());
		m_vUndo.pop_back();
		if(State.m_Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(State.m_Group)];
		if(State.m_Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		ApplyUndoState(State, m_vRedo);
		m_Dirty = true;
		Notify( Tr("undo") );
	}

	void CEditor::Redo()
	{
		if(m_vRedo.empty())
		{
			Notify( Tr("nothing to redo") );
			return;
		}
		CUndoState State = std::move(m_vRedo.back());
		m_vRedo.pop_back();
		if(State.m_Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(State.m_Group)];
		if(State.m_Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		// redo moves the state back onto the undo stack; pushing it onto the
		// redo stack instead would lose the step that was just restored
		ApplyUndoState(State, m_vUndo);
		m_Dirty = true;
		Notify( Tr("redo") );
	}

	// puts the current state of the layer on the other stack, then restores the
	// captured one; both stacks must move together or redo would lose the scope
	void CEditor::ApplyUndoState(const CUndoState &State, std::vector<CUndoState> &Other)
	{
		if(State.m_Scope == undo_scope::whole || State.m_Scope == undo_scope::envelope)
		{
			/*
				The whole map steps are symmetric: capture what is there now, and
				put the stored copy back. The selection is clamped afterwards,
				because a layer that was added is not there any more after an
				undo of it.
			*/
			CUndoState Current;
			Current.m_Scope = State.m_Scope;
			Current.m_vGroups = m_Map.m_vGroups;
			Current.m_vEnvelopes = m_Map.m_vEnvelopes;
			Current.m_vImages = m_Map.m_vImages;
			Other.push_back(std::move(Current));

			m_Map.m_vGroups = State.m_vGroups;
			m_Map.m_vEnvelopes = State.m_vEnvelopes;
			m_Map.m_vImages = State.m_vImages;
			m_vViewStates.clear();
			EnsureViewStates();
			ClampSelection();
			ResetTextures();
			return;
		}

		CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(State.m_Group)];
		CLayer &L = G.m_vLayers[static_cast<std::size_t>(State.m_Layer)];
		Other.push_back(CaptureLayer(L, State.m_Group, State.m_Layer, State.m_Scope));

		switch(State.m_Scope)
		{
		case undo_scope::tiles:
			L.m_vTiles = State.m_vTiles;
			L.m_Width = State.m_Width;
			L.m_Height = State.m_Height;
			break;
		case undo_scope::quads:
			L.m_vQuads = State.m_vQuads;
			break;
		case undo_scope::colour:
			L.m_Color = State.m_Color;
			break;
		case undo_scope::envelope:
		case undo_scope::whole:
			// handled above, before the per layer switch
			break;
		}
	}

	// ---------------------------------------------------------------- brush
	/*
		The brush is a stamp, not a tile index: the palette can be dragged over
		a rectangle of the sheet, and Space or a pick drag takes a region of the
		map the same size. A 1x1 stamp is the plain tile brush, so the single
		tile path is the region path with one cell.
	*/
	void CEditor::SetBrushUniform(int Size)
	{
		m_BrushSize = base::clamp(Size, 1, 16);
		m_BrushW = m_BrushSize;
		m_BrushH = m_BrushSize;
		m_vBrush.assign(static_cast<std::size_t>(m_BrushW) * static_cast<std::size_t>(m_BrushH),
			CBrushTile{m_BrushTile, m_BrushFlags});
	}

	void CEditor::SetBrushRegion(int Width, int Height, const std::vector<CBrushTile> &Tiles, int PrimaryIndex)
	{
		m_BrushW = base::clamp(Width, 1, MaxBrushExtent);
		m_BrushH = base::clamp(Height, 1, MaxBrushExtent);
		const std::size_t Count = static_cast<std::size_t>(m_BrushW) * static_cast<std::size_t>(m_BrushH);
		m_vBrush.assign(Count, CBrushTile{m_BrushTile, m_BrushFlags});
		for(std::size_t i = 0; i < std::min(Count, Tiles.size()); i++)
			m_vBrush[i] = Tiles[i];
		m_BrushTile = base::clamp(PrimaryIndex, 0, 255);
		// the first cell is what the panel reports and what a flip toggles,
		// so the single tile follows the stamp
		m_BrushFlags = m_vBrush.empty() ? 0 : m_vBrush.front().m_Flags;
	}

	void CEditor::ToggleBrushFlags(int Flags)
	{
		m_BrushFlags ^= Flags;
		for(CBrushTile &Tile : m_vBrush)
			Tile.m_Flags ^= Flags;
	}

	const CBrushTile &CEditor::BrushAt(int X, int Y) const
	{
		static const CBrushTile Empty;
		if(m_vBrush.empty() || m_BrushW <= 0 || m_BrushH <= 0)
			return Empty;
		const int WrappedX = ((X % m_BrushW) + m_BrushW) % m_BrushW;
		const int WrappedY = ((Y % m_BrushH) + m_BrushH) % m_BrushH;
		return m_vBrush[static_cast<std::size_t>(WrappedY) * static_cast<std::size_t>(m_BrushW) + static_cast<std::size_t>(WrappedX)];
	}

	bool CEditor::BrushIsUniform(int Size) const
	{
		if(Size <= 0 || m_BrushW != Size || m_BrushH != Size ||
			m_vBrush.size() != static_cast<std::size_t>(Size) * static_cast<std::size_t>(Size))
			return false;
		for(const CBrushTile &Tile : m_vBrush)
			if(Tile.m_Index != m_BrushTile || Tile.m_Flags != m_BrushFlags)
				return false;
		return true;
	}

	base::ivec2 CEditor::BrushOrigin(base::vec2 World) const
	{
		const int TileX = static_cast<int>(std::floor(World.x / TileSize));
		const int TileY = static_cast<int>(std::floor(World.y / TileSize));
		return base::ivec2(TileX - m_BrushW / 2, TileY - m_BrushH / 2);
	}

	void CEditor::SetBrushSize(int Size) { SetBrushUniform(Size); }

	void CEditor::SetBrushTile(int Index)
	{
		m_BrushTile = base::clamp(Index, 0, 255);
		// stepping the tile keeps the size the size buttons remember, which is
		// what makes Q/E walk the sheet while the brush stays 4x4
		SetBrushUniform(m_BrushSize);
	}

	// ------------------------------------------------------------- painting
	void CEditor::BeginStroke()
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles())
			return;
		PushUndo(m_SelectedGroup, m_SelectedLayer);
		m_bPainting = true;
		m_StrokeLastTile = base::ivec2(-1, -1);
	}

	void CEditor::EndStroke()
	{
		if(!m_bPainting)
			return;
		m_bPainting = false;
		Notify(base::FormatRuntime(Tr("edited {}"), ToolName(m_Tool)));
	}

	void CEditor::PaintAt(base::vec2 World)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles())
			return;
		const CLayerView *pView = ViewState(m_SelectedGroup, m_SelectedLayer);
		if(pView != nullptr && pView->m_Locked)
		{
			SetStatus( Tr("this layer is locked (DDRace physics layers are read only)") );
			return;
		}

		const int TileX = static_cast<int>(std::floor(World.x / TileSize));
		const int TileY = static_cast<int>(std::floor(World.y / TileSize));
		if(m_StrokeLastTile.x == TileX && m_StrokeLastTile.y == TileY)
			return;
		m_StrokeLastTile = base::ivec2(TileX, TileY);

		// a tile's flags also carry the opaque bit the image declares for it
		const CImage *pImage = nullptr;
		if(pLayer->m_Image >= 0 && pLayer->m_Image < static_cast<int>(m_Map.m_vImages.size()))
		{
			const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(pLayer->m_Image)];
			if(Image.m_aTileFlagsValid)
				pImage = &Image;
		}

		// the stamp is centred on the cursor tile, the same anchor the preview
		// draws with, so what is under the cursor is what gets painted
		const base::ivec2 Origin = BrushOrigin(World);

		/*
			The fill tool floods the region of one tile index under the cursor
			instead of stamping: a map maker who has drawn an outline wants the
			inside of it filled in one click, which is what "bucket" means in
			every other editor. It is bounded by the layer and by a tile budget,
			so a misclick on a big open area cannot lock the frame up.
		*/
		if(m_Tool == editor_tool::fill)
		{
			const int Width = pLayer->m_Width;
			const int Height = pLayer->m_Height;
			CTile *pStart = pLayer->Tile(TileX, TileY);
			if(pStart == nullptr || Width <= 0 || Height <= 0)
				return;
			const std::uint8_t Target = pStart->m_Index;
			const CBrushTile &Cell = BrushAt(0, 0);
			const std::uint8_t Replacement = static_cast<std::uint8_t>(base::clamp(Cell.m_Index, 0, 255));
			if(Target == Replacement)
				return;

			std::vector<bool> Seen(static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height), false);
			std::vector<base::ivec2> Stack;
			Stack.push_back(base::ivec2(TileX, TileY));
			// 250k cells is a 500 by 500 region, well past any hand drawn map
			std::size_t Budget = 250000;
			while(!Stack.empty() && Budget > 0)
			{
				const base::ivec2 At = Stack.back();
				Stack.pop_back();
				if(At.x < 0 || At.y < 0 || At.x >= Width || At.y >= Height)
					continue;
				const std::size_t Flat = static_cast<std::size_t>(At.y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(At.x);
				if(Seen[Flat])
					continue;
				CTile *pTile = pLayer->Tile(At.x, At.y);
				if(pTile == nullptr || pTile->m_Index != Target)
					continue;
				Seen[Flat] = true;
				Budget--;
				pTile->m_Index = Replacement;
				pTile->m_Flags = static_cast<std::uint8_t>(Cell.m_Flags);
				if(pImage != nullptr)
					pTile->m_Flags |= static_cast<std::uint8_t>(pImage->m_aTileFlags[Replacement] & TILEFLAG_OPAQUE);
				Stack.push_back(base::ivec2(At.x + 1, At.y));
				Stack.push_back(base::ivec2(At.x - 1, At.y));
				Stack.push_back(base::ivec2(At.x, At.y + 1));
				Stack.push_back(base::ivec2(At.x, At.y - 1));
			}
			m_Dirty = true;
			return;
		}
		for(int y = 0; y < m_BrushH; y++)
		{
			for(int x = 0; x < m_BrushW; x++)
			{
				const CBrushTile &Cell = BrushAt(x, y);
				CTile *pTile = pLayer->Tile(Origin.x + x, Origin.y + y);
				if(pTile == nullptr)
					continue;
				if(m_Tool == editor_tool::erase)
				{
					pTile->m_Index = 0;
					pTile->m_Flags = 0;
				}
				else if(m_Tool == editor_tool::paint)
				{
					if(pLayer->IsGameLayer())
					{
						// the game layer stores entities offset by ENTITY_OFFSET
						pTile->m_Index = static_cast<std::uint8_t>(base::clamp(Cell.m_Index, 0, 255));
						pTile->m_Flags = 0;
					}
					else
					{
						int ExtraFlags = 0;
						if(pImage != nullptr && Cell.m_Index >= 0 && Cell.m_Index < 256)
							ExtraFlags = pImage->m_aTileFlags[static_cast<std::size_t>(Cell.m_Index)] & TILEFLAG_OPAQUE;
						pTile->m_Index = static_cast<std::uint8_t>(base::clamp(Cell.m_Index, 0, 255));
						pTile->m_Flags = static_cast<std::uint8_t>(Cell.m_Flags | ExtraFlags);
					}
				}
			}
		}
		m_Dirty = true;
	}

	// -------------------------------------------------------------- shortcuts
	/*
		The three layer operations the panel buttons and the shortcuts share.
		They clear the quad selection: after adding or deleting a layer the
		indices in m_vSelectedQuads point at nothing, and a stale handle would
		then be dragged into whichever layer took its place.
	*/
	void CEditor::AddLayer(bool bQuads)
	{
		CGroup *pGroup = SelectedGroup();
		if(pGroup == nullptr)
			return;

		// a new layer is as big as the group's tiles, so it lines up with what
		// is already there instead of being a separate 50 by 50 island
		int Width = 50, Height = 50;
		for(const CLayer &Layer : pGroup->m_vLayers)
			if(Layer.IsTiles() && Layer.m_Width > 0 && Layer.m_Height > 0)
			{
				Width = Layer.m_Width;
				Height = Layer.m_Height;
				break;
			}

		if(bQuads)
		{
			CLayer &Layer = m_Map.NewQuadLayer(*pGroup, -1);
			Layer.m_Name = "Quad layer";
			Notify( Tr("added a quad layer, colour and points are editable") );
		}
		else
		{
			CLayer &Layer = m_Map.NewTileLayer(*pGroup, Width, Height, -1);
			Layer.m_Name = "Tile layer";
			Notify( Tr("added a tile layer") );
		}

		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedLayer = static_cast<int>(pGroup->m_vLayers.size()) - 1;
		ClearQuadSelection();
		m_Dirty = true;
	}

	void CEditor::AddGroup()
	{
		CGroup &Group = m_Map.NewGroup();
		Group.m_Name = "Group";
		m_Map.NewTileLayer(Group, 50, 50, -1);
		m_vViewStates.clear();
		EnsureViewStates();
		m_SelectedGroup = static_cast<int>(m_Map.m_vGroups.size()) - 1;
		m_SelectedLayer = 0;
		ClearQuadSelection();
		m_Dirty = true;
		Notify( Tr("added a group") );
	}

	void CEditor::DeleteSelected(bool bWholeGroup)
	{
		if(bWholeGroup)
		{
			if(m_Map.m_vGroups.size() <= 1)
			{
				SetStatus( Tr("a map needs at least one group") );
				return;
			}
			if(m_SelectedGroup < 0 || m_SelectedGroup >= static_cast<int>(m_Map.m_vGroups.size()))
				return;
			m_vUndo.clear();
			m_vRedo.clear();
			m_Map.m_vGroups.erase(m_Map.m_vGroups.begin() + m_SelectedGroup);
			m_vViewStates.clear();
			EnsureViewStates();
			ClampSelection();
			ClearQuadSelection();
			m_Dirty = true;
			Notify( Tr("deleted the group") );
			return;
		}

		CGroup *pGroup = SelectedGroup();
		if(pGroup == nullptr)
			return;
		// an empty group cannot be selected or added to any more, so the last
		// layer stays and the status bar says why
		if(pGroup->m_vLayers.size() <= 1)
		{
			SetStatus( Tr("this is the only layer of the group; Shift+Delete removes the group") );
			return;
		}
		if(m_SelectedLayer < 0 || m_SelectedLayer >= static_cast<int>(pGroup->m_vLayers.size()))
			return;
		m_vUndo.clear();
		m_vRedo.clear();
		pGroup->m_vLayers.erase(pGroup->m_vLayers.begin() + m_SelectedLayer);
		m_vViewStates.clear();
		EnsureViewStates();
		ClampSelection();
		ClearQuadSelection();
		m_Dirty = true;
		Notify( Tr("deleted the layer") );
	}

	void CEditor::SetTool(editor_tool Tool)
	{
		// the eraser is the one tool that is not remembered as "the tool to go
		// back to", because going back to it would make the toggle do nothing
		if(Tool != editor_tool::erase)
			m_ToolBeforeErase = Tool;
		m_Tool = Tool;
	}

	void CEditor::ToggleEraser()
	{
		/*
			The eraser is a mode rather than another tool: E drops into it and E
			again puts back whatever was in use, which is how a paint program
			behaves. It only means something on a tile layer, so anywhere else
			the key explains itself instead of selecting a tool that cannot
			paint.
		*/
		if(Mode() != edit_mode::tiles)
		{
			SetStatus( Tr("the eraser is for tile layers: select one in the Layers docker") );
			return;
		}
		if(m_Tool == editor_tool::erase)
		{
			SetTool(m_ToolBeforeErase);
			Notify(  base::FormatRuntime(Tr("back to the {}"), ToolName(m_Tool))  );
		}
		else
		{
			SetTool(editor_tool::erase);
			Notify(  base::FormatRuntime(Tr("eraser: press E again for the {}"), ToolName(m_ToolBeforeErase))  );
		}
	}

	void CEditor::HandleShortcuts()
	{
		engine::CInput &Input = this->Input();
		const bool Ctrl = Input.KeyDown(engine::keys::LCtrl) || Input.KeyDown(engine::keys::RCtrl);
		const bool Shift = Input.KeyDown(engine::keys::LShift) || Input.KeyDown(engine::keys::RShift);

		if(Ctrl && Input.KeyPressed(engine::keys::Q))
		{
			// a headless editor has no window to close, so quitting is the
			// caller's business
#if UNITEED_GUI
			if(m_pGraphics != nullptr)
				m_pGraphics->RequestQuit();
#endif
		}
		if(Ctrl && Input.KeyPressed(engine::keys::N))
			NewMap(50, 50);
		if(Ctrl && Input.KeyPressed(engine::keys::O))
			OpenFileDialog();
		if(Ctrl && Input.KeyPressed(engine::keys::S))
		{
			// Ctrl+S saves where the project already is; Ctrl+Shift+S asks, and
			// a headless editor has no dialog to ask with, so it names a file
			const bool bAs = Input.KeyDown(engine::keys::LShift) || Input.KeyDown(engine::keys::RShift);
			if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			{
				const std::string Name = ProjectName().empty() ? "untitled" : ProjectName();
				SaveProject(ProjectPath(Name).string());
			}
			else
				SaveFileDialog(bAs);
		}
		if(Ctrl && Input.KeyPressed(engine::keys::E))
			m_bShowExport = true;
		if(Ctrl && Input.KeyPressed(engine::keys::Z))
			Undo();
		if(Ctrl && Input.KeyPressed(engine::keys::Y))
			Redo();
		if(Ctrl && Input.KeyPressed(engine::keys::L))
			AddLayer(Shift);
		if(Ctrl && Input.KeyPressed(engine::keys::G))
			AddGroup();
		if(Input.KeyPressed(engine::keys::Delete) || Input.KeyPressed(engine::keys::Backspace))
			DeleteSelected(Shift);
		// Ctrl+0 is "fit on screen" everywhere else, and F belongs to the fill
		// tool now, so the two do not have to share a key
		if(Ctrl && Input.KeyPressed(engine::keys::Digit0))
			FitToMap();
		if(Input.KeyPressed(engine::keys::G))
			m_bGrid = !m_bGrid;

		// ? opens the shortcut list, which is the accelerator the tool strip's
		// Keys button stands for
		if(Input.KeyPressed(engine::keys::Slash) || Input.KeyPressed(engine::keys::Question))
			m_bShowShortcuts = !m_bShowShortcuts;
		// Space is the tile brush: it takes the tile, or the region the brush
		// is, under the cursor, which is what the reference does. It only means
		// that on a tile layer, so on a quad layer it still runs and pauses the
		// envelope preview.
		if(Input.KeyPressed(engine::keys::Space))
		{
			if(Mode() == edit_mode::tiles && m_bCursorValid)
				PickBrushAt(m_CursorWorld);
			else
			{
				m_bEnvelopePlaying = !m_bEnvelopePlaying;
				SetStatus(  m_bEnvelopePlaying ? Tr("envelope preview running") : Tr("envelope preview paused")  );
			}
		}
		if(Input.KeyPressed(engine::keys::Minus))
		{
			m_EnvelopeSpeed = m_EnvelopeSpeed <= 0.25f ? 0.25f : m_EnvelopeSpeed * 0.5f;
			SetStatus(  base::FormatRuntime(Tr("envelope speed {:.2g}x"), m_EnvelopeSpeed)  );
		}
		if(Input.KeyPressed(engine::keys::Equals))
		{
			m_EnvelopeSpeed = std::min(8.0f, m_EnvelopeSpeed * 2.0f);
			SetStatus(  base::FormatRuntime(Tr("envelope speed {:.2g}x"), m_EnvelopeSpeed)  );
		}
		// the colour picker for the selected tile layer, quad, or quad corner
		if(Input.KeyPressed(engine::keys::C))
		{
			if(!OpenColorPicker())
				SetStatus( Tr("nothing to colour: select a tile layer or a quad") );
		}
		// the tool, the brush size and the flips only mean anything while a tile
		// layer is selected; on a quad layer they would silently change state
		// the next tile layer would then use
		if(Mode() == edit_mode::tiles)
		{
			if(Input.KeyPressed(engine::keys::Digit4))
				SetBrushUniform(1);
			if(Input.KeyPressed(engine::keys::Digit5))
				SetBrushUniform(2);
			if(Input.KeyPressed(engine::keys::Digit6))
				SetBrushUniform(4);
			if(Input.KeyPressed(engine::keys::Digit7))
				SetBrushUniform(8);
			if(Input.KeyPressed(engine::keys::LeftBracket))
				SetBrushUniform(std::max(1, m_BrushSize / 2));
			if(Input.KeyPressed(engine::keys::RightBracket))
				SetBrushUniform(std::min(16, m_BrushSize * 2));
			if(Input.KeyPressed(engine::keys::H))
				ToggleBrushFlags(TILEFLAG_HFLIP);
			if(Input.KeyPressed(engine::keys::J))
				ToggleBrushFlags(TILEFLAG_VFLIP);
			if(Input.KeyPressed(engine::keys::K))
				ToggleBrushFlags(TILEFLAG_ROTATE);
			/*
				The tile index is cycled with the two keys next to the period,
				which leaves Q and E free: E is the eraser toggle, and a key
				that erases and steps the palette at the same time is exactly
				the kind of binding this interface had to lose.
			*/
			if(Input.KeyPressed(engine::keys::Comma))
				SetBrushTile((m_BrushTile + 255) % 256);
			if(Input.KeyPressed(engine::keys::Period))
				SetBrushTile((m_BrushTile + 1) % 256);
			// the whole layer stamped with the brush: a rare enough operation
			// to sit behind Shift, because P alone picks a region
			if(Shift && Input.KeyPressed(engine::keys::P))
			{
				if(CLayer *pLayer = SelectedLayer())
					if(pLayer->IsTiles())
					{
						PushUndo(m_SelectedGroup, m_SelectedLayer);
						// the stamp tiles across the layer, so filling with a
						// region lays that region down over and over
						for(int y = 0; y < pLayer->m_Height; y++)
						{
							for(int x = 0; x < pLayer->m_Width; x++)
							{
								CTile *pTile = pLayer->Tile(x, y);
								if(pTile == nullptr)
									continue;
								const CBrushTile &Cell = BrushAt(x, y);
								pTile->m_Index = static_cast<std::uint8_t>(base::clamp(Cell.m_Index, 0, 255));
								pTile->m_Flags = pLayer->IsGameLayer() ? 0 : static_cast<std::uint8_t>(Cell.m_Flags);
							}
						}
						m_Dirty = true;
						Notify( Tr("filled the layer") );
					}
			}
		}
	}

	void CEditor::HandleCanvasInput(const base::rect &Canvas)
	{
		engine::CInput &Input = this->Input();
		const bool bInCanvas = Canvas.Contains(Input.MousePosition());

		/*
			The tool keys are read here, at the top of the frame's canvas input,
			rather than with the other shortcuts: a tool chosen on this frame has
			to apply to this frame's drag, otherwise the first frame of the drag
			paints with the old tool and only the following frames pan.

			Each letter names its tool - B the brush, F the fill, P the picker,
			V the view - and E is Krita's eraser toggle rather than a fifth
			tool, so the letter on a key and the button it lights up cannot
			drift apart again. The digits are the same tools in the order the
			palette lists them.
		*/
		const bool Shift = Input.KeyDown(engine::keys::LShift) || Input.KeyDown(engine::keys::RShift);
		// the view is the one tool that means something on every layer
		if(Input.KeyPressed(engine::keys::V))
			SetTool(editor_tool::pan);
		/*
			The painting tools are read where the toolbox draws them: a tile
			layer is the only canvas they can change, so on a quad layer the
			key does nothing rather than silently selecting a tool for the next
			layer the user selects. The eraser is the exception and says so.
		*/
		if(Mode() == edit_mode::tiles)
		{
			if(Input.KeyPressed(engine::keys::B))
				SetTool(editor_tool::paint);
			if(Input.KeyPressed(engine::keys::F))
				SetTool(editor_tool::fill);
			if(Input.KeyPressed(engine::keys::E))
				ToggleEraser();
			// Shift+P stamps the whole layer with the brush, so it is not the
			// key that takes the picker
			if(Input.KeyPressed(engine::keys::P) && !Shift)
				SetTool(editor_tool::pick);
			if(Input.KeyPressed(engine::keys::Digit1))
				SetTool(editor_tool::paint);
			if(Input.KeyPressed(engine::keys::Digit2))
				SetTool(editor_tool::erase);
			if(Input.KeyPressed(engine::keys::Digit3))
				SetTool(editor_tool::pick);
		}

		/*
			Panning: the middle button anywhere, the right button with Alt, or
			the left button while the move tool is selected. The move tool is
			the one a tablet or a trackpad reaches for, and it is why the tool
			strip has it at all.
		*/
		const bool bPanDrag = Input.MouseDown(engine::CInput::mouse_button::middle) ||
			(Input.MouseDown(engine::CInput::mouse_button::right) && Input.ModifierDown(engine::keys::LAlt)) ||
			(m_Tool == editor_tool::pan && Input.MouseDown(engine::CInput::mouse_button::left));
		if(bInCanvas && bPanDrag)
		{
			const base::vec2 Delta = Input.MouseDelta();
			m_View.m_Center -= Delta / m_View.m_Zoom;
		}
		if(bInCanvas && Input.Scroll() != 0.0f)
		{
			const base::vec2 Before = m_View.ScreenToWorld(Input.MousePosition());
			m_View.m_Zoom = base::clamp(m_View.m_Zoom * (1.0f + Input.Scroll() * 0.12f), 0.05f, 8.0f);
			const base::vec2 After = m_View.ScreenToWorld(Input.MousePosition());
			m_View.m_Center += Before - After;
		}

		// keyboard panning
		const float PanSpeed = 600.0f / m_View.m_Zoom * (1.0f / 60.0f);
		if(Input.KeyDown(engine::keys::A) || Input.KeyDown(engine::keys::Left))
			m_View.m_Center.x -= PanSpeed;
		if(Input.KeyDown(engine::keys::D) || Input.KeyDown(engine::keys::Right))
			m_View.m_Center.x += PanSpeed;
		if(Input.KeyDown(engine::keys::W) || Input.KeyDown(engine::keys::Up))
			m_View.m_Center.y -= PanSpeed;
		if(Input.KeyDown(engine::keys::S) || Input.KeyDown(engine::keys::Down))
			m_View.m_Center.y += PanSpeed;

		if(bInCanvas)
		{
			const base::vec2 World = m_View.ScreenToWorld(Input.MousePosition());
			m_CursorWorld = World;
			m_bCursorValid = true;
		}
		else
		{
			m_bCursorValid = false;
		}

		/*
			Right click takes the brush from the map, which is how a tile (or the
			region the brush is) is read off and painted with again. On a quad
			layer the button belongs to the quads instead - it colours the point
			under the cursor - so the two do not fight over it.
		*/
		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::right) && !Input.ModifierDown(engine::keys::LAlt) &&
			Mode() != edit_mode::quads)
			PickBrushAt(m_CursorWorld);

		// a quad layer is edited by its points, a tile layer by the tile brush;
		// neither input path touches the other
		if(Mode() == edit_mode::quads)
			HandleQuadInput(bInCanvas);
		else if(Mode() == edit_mode::tiles)
		{
			const int TileX = static_cast<int>(std::floor(m_CursorWorld.x / TileSize));
			const int TileY = static_cast<int>(std::floor(m_CursorWorld.y / TileSize));

			/*
				The pick tool takes a region, not a single tile: pressing starts
				a marquee and letting go copies everything it covers, so a piece
				of a map can be used as the brush. The brush is untouched until
				the button comes up, so abandoning the drag changes nothing.
			*/
			if(bInCanvas && m_Tool == editor_tool::pick && Input.MousePressed(engine::CInput::mouse_button::left))
			{
				m_bPickDragging = true;
				m_PickStartTile = base::ivec2(TileX, TileY);
				m_PickEndTile = m_PickStartTile;
			}
			else if(m_bPickDragging && bInCanvas && Input.MouseDown(engine::CInput::mouse_button::left))
				m_PickEndTile = base::ivec2(TileX, TileY);

			if(m_bPickDragging && !Input.MouseDown(engine::CInput::mouse_button::left))
			{
				// the button coming up commits; losing the focus instead
				// cancels, so a half dragged marquee cannot become the brush
				if(Input.MouseReleased(engine::CInput::mouse_button::left))
				{
					int X0 = 0, Y0 = 0, X1 = -1, Y1 = -1;
					if(PickRect(X0, Y0, X1, Y1))
					{
						const CLayer *pLayer = SelectedLayer();
						std::vector<CBrushTile> Cells;
						const int Width = X1 - X0 + 1;
						const int Height = Y1 - Y0 + 1;
						Cells.reserve(static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height));
						for(int y = Y0; y <= Y1; y++)
							for(int x = X0; x <= X1; x++)
							{
								const CTile *pTile = pLayer != nullptr ? pLayer->Tile(x, y) : nullptr;
								CBrushTile Cell;
								if(pTile != nullptr)
								{
									Cell.m_Index = pTile->m_Index;
									Cell.m_Flags = pTile->m_Flags & (TILEFLAG_HFLIP | TILEFLAG_VFLIP | TILEFLAG_ROTATE);
								}
								Cells.push_back(Cell);
							}
						if(!Cells.empty())
						{
							SetBrushRegion(Width, Height, Cells, Cells.front().m_Index);
							Notify(  base::FormatRuntime(Tr("brush {:d}x{:d} taken from {:d},{:d}"), Width, Height, X0, Y0)  );
						}
					}
				}
				m_bPickDragging = false;
				m_PickStartTile = base::ivec2(-1, -1);
			}

			if(bInCanvas && m_Tool != editor_tool::pick &&
				Input.MousePressed(engine::CInput::mouse_button::left) && !m_bPickDragging)
			{
				BeginStroke();
				PaintAt(m_CursorWorld);
			}
		}

		if(m_bPainting && Input.MouseDown(engine::CInput::mouse_button::left))
			PaintAt(m_CursorWorld);
		if(m_bPainting && Input.MouseReleased(engine::CInput::mouse_button::left))
			EndStroke();
	}

	void CEditor::AddGroupOp(const std::string &Name)
	{
		PushUndo(static_cast<int>(m_Map.m_vGroups.size()) - 1, 0, undo_scope::whole);
		CGroup &Group = m_Map.NewGroup();
		Group.m_Name = Name;
		RefreshAfterStructuralEdit();
		m_SelectedGroup = static_cast<int>(m_Map.m_vGroups.size()) - 1;
		m_SelectedLayer = 0;
	}

	void CEditor::PushUndoOpWhole(int Group, int Layer) { PushUndo(Group, Layer, undo_scope::whole); }
	void CEditor::PushUndoOpTiles(int Group, int Layer) { PushUndo(Group, Layer, undo_scope::tiles); }

	void CEditor::PushUndoGroupProperties(int Group)
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		/*
			The layer index is only there to say which layer a scoped snapshot
			is about; a group's own properties need none, and the selection can
			be the group row, where there is no layer to name. An empty layer
			stands in, and the whole scope is what carries the groups.
		*/
		m_vUndo.push_back(CaptureLayer(CLayer(), Group, 0, undo_scope::whole));
		if(m_vUndo.size() > 32)
			m_vUndo.erase(m_vUndo.begin());
		m_vRedo.clear();
	}

	void CEditor::AddQuadSelection(int Index)
	{
		if(Index < 0)
			return;
		if(std::find(m_vSelectedQuads.begin(), m_vSelectedQuads.end(), Index) == m_vSelectedQuads.end())
			m_vSelectedQuads.push_back(Index);
	}

	void CEditor::ClearQuadSelection()
	{
		m_vSelectedQuads.clear();
		m_QuadHover = -1;
		m_DragQuad = -1;
		m_DragPoint = -1;
		m_bQuadDragged = false;
		m_bQuadMoving = false;
		m_bQuadMoved = false;
		m_bQuadMoveUndoPushed = false;
		m_vQuadMove.clear();
	}

	void CEditor::SelectQuad(int Index)
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
			return;
		if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
		{
			ClearQuadSelection();
			return;
		}
		ClearQuadSelection();
		m_vSelectedQuads.assign(1, Index);
		CColor Color;
		if(PickerColor(Color))
			m_PickerColor = Color;
	}

	int CEditor::QuadHandleAt(base::vec2 Screen) const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads() || m_vSelectedQuads.empty())
			return -1;

		// the pivot first: it belongs to the quad and does not overlap a corner,
		// and a click on it must not be read as a click on the shape
		const base::view_transform View = SelectionView();
		const float RadiusSquared = HandleGrabRadius * HandleGrabRadius;

		for(std::size_t i = 0; i < m_vSelectedQuads.size(); i++)
		{
			const int Index = m_vSelectedQuads[i];
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				continue;
			const CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
			const base::vec2 Pivot = View.WorldToScreen(base::vec2(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y)));
			const base::vec2 Delta = Pivot - Screen;
			if(Delta.x * Delta.x + Delta.y * Delta.y <= RadiusSquared)
				return static_cast<int>(i) * 5 + 4;
		}

		for(std::size_t i = 0; i < m_vSelectedQuads.size(); i++)
		{
			const int Index = m_vSelectedQuads[i];
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				continue;
			base::vec2 aWorld[4];
			QuadPoints(pLayer->m_vQuads[static_cast<std::size_t>(Index)], aWorld);
			for(int p = 0; p < 4; p++)
			{
				const base::vec2 Corner = View.WorldToScreen(aWorld[p]);
				const base::vec2 Delta = Corner - Screen;
				if(Delta.x * Delta.x + Delta.y * Delta.y <= RadiusSquared)
					return static_cast<int>(i) * 5 + p;
			}
		}
		return -1;
	}

	/*
		Starting from a press, the selection is carried around: the press
		records where every point of every selected quad was, and the drag then
		writes start + delta. Writing the sum instead of accumulating each frame
		keeps the quads exactly under the cursor however long the drag runs.
	*/
	void CEditor::HandleQuadMoveStart()
	{
		m_vQuadMove.clear();
		m_bQuadMoving = false;
		m_bQuadMoved = false;
		m_bQuadMoveUndoPushed = false;

		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
			return;
		for(const int Index : m_vSelectedQuads)
		{
			if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
				continue;
			const CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Index)];
			CQuadMoveStart Start;
			Start.m_Quad = Index;
			for(int p = 0; p < 5; p++)
			{
				Start.m_aPoints[p][0] = Quad.m_aPoints[p].x;
				Start.m_aPoints[p][1] = Quad.m_aPoints[p].y;
			}
			m_vQuadMove.push_back(Start);
		}
		if(m_vQuadMove.empty())
			return;
		m_bQuadMoving = true;
		m_QuadMoveAnchor = SelectionView().ScreenToWorld(this->Input().MousePosition());
	}

	/*
		The quad editor. Shift turns a press on a handle into a drag of that one
		point; without it a press on a handle, or inside a quad, moves the whole
		selection instead, so the corners cannot be bent out of shape by
		accident and a quad can be carried around the map. The points are stored
		in map space, so a drag is measured in the map, not on screen: the point
		and the quad follow the cursor at every zoom level instead of moving by
		screen pixels.
	*/
	void CEditor::HandleQuadInput(bool bInCanvas)
	{
		engine::CInput &Input = this->Input();
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsQuads())
			return;

		const base::vec2 Mouse = Input.MousePosition();
		UpdateQuadHover(bInCanvas);

		/*
			Right clicking a point of the quad colours it: the point under the
			cursor is the corner, so the picker opens aimed at that one instead
			of the corner having to be picked in the panel first. Alt is left
			out because Alt and the right button pan the view.
		*/
		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::right) &&
			!Input.ModifierDown(engine::keys::LAlt))
		{
			OpenQuadColorPickerAt(Mouse, m_QuadHover);
			return;
		}

		const bool bHeld = Input.MouseDown(engine::CInput::mouse_button::left);

		// the selection being carried around
		if(m_bQuadMoving)
		{
			if(!bHeld)
			{
				if(m_bQuadMoved)
					Notify(  m_vQuadMove.size() == 1 ? Tr("moved quad") :
						base::FormatRuntime(Tr("moved {:d} quads"), m_vQuadMove.size())  );
				m_bQuadMoving = false;
				m_bQuadMoved = false;
				m_bQuadMoveUndoPushed = false;
				m_vQuadMove.clear();
			}
			else
			{
				const base::view_transform View = SelectionView();
				const base::vec2 Delta = View.ScreenToWorld(Mouse) - m_QuadMoveAnchor;
				if(Delta.x != 0.0f || Delta.y != 0.0f)
				{
					// the undo snapshot is the state before the first movement,
					// so a press that never moves anything leaves none behind
					if(!m_bQuadMoveUndoPushed)
					{
						PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::quads);
						m_bQuadMoveUndoPushed = true;
					}
					for(const CQuadMoveStart &Start : m_vQuadMove)
					{
						if(Start.m_Quad < 0 || Start.m_Quad >= static_cast<int>(pLayer->m_vQuads.size()))
							continue;
						CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(Start.m_Quad)];
						for(int p = 0; p < 5; p++)
						{
							Quad.m_aPoints[p].x = base::FloatToFixed(base::FixedToFloat(Start.m_aPoints[p][0]) + Delta.x);
							Quad.m_aPoints[p].y = base::FloatToFixed(base::FixedToFloat(Start.m_aPoints[p][1]) + Delta.y);
						}
					}
					m_bQuadMoved = true;
					m_Dirty = true;
				}
			}
			return;
		}

		// one point, dragged on its own: only Shift starts this
		if(m_DragQuad >= 0 && m_DragPoint >= 0)
		{
			if(!bHeld)
			{
				if(m_bQuadDragged)
					Notify( Tr("moved quad point") );
				m_DragQuad = -1;
				m_DragPoint = -1;
				m_bQuadDragged = false;
			}
			else
			{
				if(m_DragQuad < static_cast<int>(pLayer->m_vQuads.size()))
				{
					if(!m_bQuadDragged)
						PushUndo(m_SelectedGroup, m_SelectedLayer, undo_scope::quads);
					CQuad &Quad = pLayer->m_vQuads[static_cast<std::size_t>(m_DragQuad)];
					const base::view_transform View = SelectionView();

					/*
						The corner handles show the point the position envelope
						has moved it to, so the drag has to be undone through
						that envelope: the offset and the rotation the envelope
						adds now are subtracted again, or the handle would drift
						away from the cursor as soon as the envelope is not
						identity. The pivot is stored as it is.
					*/
					if(m_DragPoint == 4)
					{
						const base::vec2 Target = View.ScreenToWorld(Mouse);
						Quad.m_aPoints[4].x = base::FloatToFixed(Target.x);
						Quad.m_aPoints[4].y = base::FloatToFixed(Target.y);
					}
					else
					{
						CEnvelopeSample Position;
						Position[0] = Position[1] = Position[2] = 0.0f;
						if(Quad.m_PosEnv >= 0 && Quad.m_PosEnv < static_cast<int>(m_Map.m_vEnvelopes.size()))
							EvaluateEnvelope(m_Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_PosEnv)],
								m_EnvelopeTime + Quad.m_PosEnvOffset / 1000.0f, Position, 3);
						const base::vec2 Offset(Position[0], Position[1]);
						const float Rotation = Position[2] / 180.0f * base::Pi;
						const base::vec2 Pivot(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y));

						base::vec2 Target = View.ScreenToWorld(Mouse) - Offset;
						if(Rotation != 0.0f)
						{
							const base::vec2 Relative = Target - Pivot;
							const float Cos = std::cos(-Rotation);
							const float Sin = std::sin(-Rotation);
							Target = base::vec2(Relative.x * Cos - Relative.y * Sin, Relative.x * Sin + Relative.y * Cos) + Pivot;
						}
						Quad.m_aPoints[m_DragPoint].x = base::FloatToFixed(Target.x);
						Quad.m_aPoints[m_DragPoint].y = base::FloatToFixed(Target.y);
					}
					m_bQuadDragged = true;
					m_Dirty = true;
				}
			}
			return;
		}

		if(!bInCanvas || !Input.MousePressed(engine::CInput::mouse_button::left))
			return;

		// Shift is tracked as the key itself, the way the rest of the editor
		// reads it, so a scripted press of Shift holds for the whole drag
		const bool bShift = Input.KeyDown(engine::keys::LShift) || Input.KeyDown(engine::keys::RShift);
		const bool bAdd = Input.ModifierDown(engine::keys::LCtrl) || Input.ModifierDown(engine::keys::RCtrl);

		// Shift on a handle is the one way a corner or the pivot moves alone
		const int Handle = QuadHandleAt(Mouse);
		if(Handle >= 0 && bShift)
		{
			if(Handle / 5 < static_cast<int>(m_vSelectedQuads.size()))
			{
				m_DragQuad = m_vSelectedQuads[static_cast<std::size_t>(Handle / 5)];
				m_DragPoint = Handle % 5;
				m_bQuadDragged = false;
				// the handle shows the corner the picker would edit
				if(m_DragPoint < 4)
					SetQuadColorTarget(m_DragPoint);
			}
			return;
		}

		// anything else selects, and a drag from the press moves the selection
		const bool bStartMove = Handle >= 0 || m_QuadHover >= 0;
		if(Handle >= 0)
		{
			// the handle belongs to a selected quad, so the selection stands
			// and the drag carries it
		}
		else if(m_QuadHover >= 0 && bAdd)
		{
			const auto At = std::find(m_vSelectedQuads.begin(), m_vSelectedQuads.end(), m_QuadHover);
			if(At != m_vSelectedQuads.end())
				m_vSelectedQuads.erase(At);
			else
				m_vSelectedQuads.push_back(m_QuadHover);
		}
		else if(m_QuadHover >= 0)
		{
			if(m_vSelectedQuads.size() != 1 || m_vSelectedQuads[0] != m_QuadHover)
				m_vSelectedQuads.assign(1, m_QuadHover);
		}
		else
		{
			if(!bAdd && !m_vSelectedQuads.empty())
			{
				ClearQuadSelection();
				SetStatus( Tr("no quad selected") );
			}
			return;
		}

		if(!m_vSelectedQuads.empty())
		{
			std::sort(m_vSelectedQuads.begin(), m_vSelectedQuads.end());
			CColor Color;
			if(PickerColor(Color))
				m_PickerColor = Color;
			SetStatus(  m_vSelectedQuads.size() == 1 ?
				base::FormatRuntime(Tr("quad {:d} selected: drag it to move, Shift drags a point, right click colours a corner"), m_vSelectedQuads[0]) :
				base::FormatRuntime(Tr("{:d} quads selected: drag to move them, right click colours a corner"), m_vSelectedQuads.size())  );
		}

		if(bStartMove && !bAdd && !m_vSelectedQuads.empty())
			HandleQuadMoveStart();
	}

	// the quad under the mouse, topmost last so the newest is picked first
	void CEditor::UpdateQuadHover(bool bInCanvas)
	{
		m_QuadHover = -1;
		const CLayer *pLayer = SelectedLayer();
		if(!bInCanvas || pLayer == nullptr || !pLayer->IsQuads())
			return;

		/*
			The test runs on the points QuadPoints() gives, which are the ones
			the position envelope has moved and turned. Testing the stored
			points instead would pick a quad where it used to be, and the map
			would then edit something that is not under the cursor.

			PointInQuad walks the outline, so the corners are handed over in
			perimeter order: the stored order (top left, top right, bottom left,
			bottom right) traces a bowtie, and testing that reports a hit for
			the wrong half of the shape.
		*/
		const base::vec2 Mouse = this->Input().MousePosition();
		const base::view_transform View = SelectionView();
		for(int Index = static_cast<int>(pLayer->m_vQuads.size()) - 1; Index >= 0; Index--)
		{
			base::vec2 aWorld[4];
			QuadPoints(pLayer->m_vQuads[static_cast<std::size_t>(Index)], aWorld);
			base::vec2 aOutline[4];
			for(int i = 0; i < 4; i++)
				aOutline[i] = View.WorldToScreen(aWorld[CQuad::s_aPerimeter[i]]);
			if(base::PointInQuad(aOutline, Mouse))
			{
				m_QuadHover = Index;
				return;
			}
		}
	}

	/*
		The tile - or the region the brush is - under the world position becomes
		the brush. The region is the size of the stamp, so picking after a 4x4
		selection takes a 4x4 piece back, and every cell keeps its own flips, so
		a piece of map can be read off and put down again unchanged.
	*/
	void CEditor::PickBrushAt(base::vec2 World)
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles())
		{
			SetStatus( Tr("only a tile layer has tiles to pick") );
			return;
		}
		const CLayerView *pView = ViewState(m_SelectedGroup, m_SelectedLayer);
		if(pView != nullptr && pView->m_Locked)
		{
			SetStatus( Tr("this layer is locked, there is nothing to pick") );
			return;
		}

		// the same anchor the painting uses, clamped to what the layer holds
		const base::ivec2 Origin = BrushOrigin(World);
		const int X0 = std::max(Origin.x, 0);
		const int Y0 = std::max(Origin.y, 0);
		const int X1 = std::min(Origin.x + m_BrushW - 1, pLayer->m_Width - 1);
		const int Y1 = std::min(Origin.y + m_BrushH - 1, pLayer->m_Height - 1);
		if(X1 < X0 || Y1 < Y0)
		{
			SetStatus(  base::FormatRuntime(Tr("tile {:d},{:d} is outside the layer"), Origin.x, Origin.y)  );
			return;
		}

		const int Width = X1 - X0 + 1;
		const int Height = Y1 - Y0 + 1;
		std::vector<CBrushTile> Cells(static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height));
		for(int y = 0; y < Height; y++)
		{
			for(int x = 0; x < Width; x++)
			{
				const CTile *pTile = pLayer->Tile(X0 + x, Y0 + y);
				CBrushTile Cell;
				if(pTile != nullptr)
				{
					Cell.m_Index = pTile->m_Index;
					Cell.m_Flags = pTile->m_Flags & (TILEFLAG_HFLIP | TILEFLAG_VFLIP | TILEFLAG_ROTATE);
				}
				Cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(x)] = Cell;
			}
		}
		SetBrushRegion(Width, Height, Cells, Cells.front().m_Index);
		Notify(  base::FormatRuntime(Tr("brush {:d}x{:d} taken from {:d},{:d}"), Width, Height, X0, Y0)  );
	}

	bool CEditor::PickRect(int &X0, int &Y0, int &X1, int &Y1) const
	{
		const CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles() || m_PickStartTile.x < 0)
			return false;
		X0 = std::max(std::min(m_PickStartTile.x, m_PickEndTile.x), 0);
		Y0 = std::max(std::min(m_PickStartTile.y, m_PickEndTile.y), 0);
		X1 = std::min(std::max(m_PickStartTile.x, m_PickEndTile.x), pLayer->m_Width - 1);
		Y1 = std::min(std::max(m_PickStartTile.y, m_PickEndTile.y), pLayer->m_Height - 1);
		// the stamp has a bound, and the marquee shows exactly what is
		// committed, so the region stops growing once it is reached
		X1 = std::min(X1, X0 + MaxBrushExtent - 1);
		Y1 = std::min(Y1, Y0 + MaxBrushExtent - 1);
		return X1 >= X0 && Y1 >= Y0;
	}

	void CEditor::Update()
	{
		// a minimized window can report itself as empty; there is nothing to lay
		// out or hit test then, and the painter must not be handed a zero size
		if(m_pGraphics == nullptr)
			return;
		if(m_pGraphics->WindowSize().x < 1.0f || m_pGraphics->WindowSize().y < 1.0f)
			return;

		// the envelope preview runs on its own clock, so colour envelopes and
		// animated quads can be looked at while editing
		const double Now = base::Now();
		if(m_LastFrameTime > 0.0)
		{
			const float Delta = static_cast<float>(std::min(Now - m_LastFrameTime, 0.1));
			if(m_bEnvelopePlaying && Delta > 0.0f)
			{
				const float Loop = EnvelopeLoopLength();
				m_EnvelopeTime += Delta * m_EnvelopeSpeed;
				if(m_EnvelopeTime > Loop)
					m_EnvelopeTime = std::fmod(m_EnvelopeTime, Loop);
			}
		}
		m_LastFrameTime = Now;

#if UNITEED_GUI
		/*
			The vector textures follow the display: the density of the monitor
			and the interface scale the user asked for are read every frame, and
			a change makes the icons rasterize again - the next time each one is
			drawn, so an icon that is off screen costs nothing.
		*/
		m_VectorTextures.SetDisplayScale(m_pGraphics->DisplayScale());
#endif

		EnsureViewStates();
		ClampSelection();

		const base::rect Canvas = CanvasRect(WindowSize());
		// the view transform works in window coordinates: the canvas origin is
		// what makes the drawn tile and the clicked tile the same tile
		m_View.m_Origin = Canvas.m_TopLeft;
		m_View.m_Viewport = base::vec2(Canvas.Width(), Canvas.Height());
		// The drawing resolves the selected group's view as well, and it has to
		// be the same one here: an update that ran with the previous frame's
		// layer view would hit test against a different mapping than it draws.
		m_LayerView = SelectionView();

		// a popup owns the keyboard and the canvas while it is open, otherwise
		// typing a file name would also trigger the editor shortcuts. An open
		// menu owns them for the same reason, and so does a name field in the
		// layer tree: a click on a menu row, or the letter of a layer name,
		// must not also paint the tile under it.
		if(m_bShowOpen || m_bShowSave || m_bShowExport || m_bShowHelp || m_bShowColor || m_Menu != menu_id::none ||
			Renaming())
		{
			HandleColorPopupInput();
#if UNITEED_GUI
			HandlePopupInput();
#endif
			HandleRenameInput();
			return;
		}

		HandleShortcuts();
		HandleCanvasInput(Canvas);
	}
} // namespace game
