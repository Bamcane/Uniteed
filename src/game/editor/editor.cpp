#include "editor.h"

#include <algorithm>
#include <cstring>

#include <GLFW/glfw3.h>

#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/ui.h"
#include "game/assets.h"

namespace game
{
	namespace
	{
		constexpr float TileSize = 32.0f;

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

		NVGcolor FlagColor(int LayerFlags)
		{
			if(LayerFlags & TILESLAYERFLAG_TELE)
				return nvgRGBA(120, 200, 255, 90);
			if(LayerFlags & TILESLAYERFLAG_SPEEDUP)
				return nvgRGBA(255, 210, 90, 90);
			if(LayerFlags & TILESLAYERFLAG_SWITCH)
				return nvgRGBA(190, 130, 255, 90);
			if(LayerFlags & TILESLAYERFLAG_TUNE)
				return nvgRGBA(255, 130, 200, 90);
			if(LayerFlags & TILESLAYERFLAG_FRONT)
				return nvgRGBA(140, 255, 160, 80);
			return nvgRGBA(160, 160, 160, 80);
		}

		const char *ToolName(editor_tool Tool)
		{
			switch(Tool)
			{
			case editor_tool::paint: return "paint";
			case editor_tool::erase: return "erase";
			case editor_tool::pick: return "pick";
			}
			return "?";
		}
	} // namespace

	// ------------------------------------------------------------------- setup
	bool CEditor::Init(engine::CGraphics *pGraphics)
	{
		m_pGraphics = pGraphics;
		m_View.m_Zoom = 1.0f;
		// the engine clears the frame, the panel background is the theme's job
		m_pGraphics->SetClearColor(engine::ui::Theme.m_Background.r, engine::ui::Theme.m_Background.g, engine::ui::Theme.m_Background.b);
		NewMap(50, 50);
		SetStatus("ready, use Ctrl+O to open a map");
		return true;
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

	CGroup *CEditor::SelectedGroup()
	{
		if(m_SelectedGroup < 0 || m_SelectedGroup >= static_cast<int>(m_Map.m_vGroups.size()))
			return nullptr;
		return &m_Map.m_vGroups[static_cast<std::size_t>(m_SelectedGroup)];
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
			return;
		}
		m_SelectedGroup = base::clamp(m_SelectedGroup, 0, static_cast<int>(m_Map.m_vGroups.size()) - 1);
		const int Layers = static_cast<int>(m_Map.m_vGroups[static_cast<std::size_t>(m_SelectedGroup)].m_vLayers.size());
		m_SelectedLayer = Layers > 0 ? base::clamp(m_SelectedLayer, 0, Layers - 1) : 0;
	}

	bool CEditor::SetupVersionForMap()
	{
		// which mapres directory matches what the map was built for: DDRace maps
		// reference the ddnet tiles, 0.8 ships the 0.7 images, and a map without
		// run length tiles and without DDRace items is treated as 0.6
		std::string Version = "07";
		if(m_Map.m_SourceHasDdnetItems)
			Version = "ddnet";
		else if(m_Map.m_SourceDatafileVersion == 5)
			Version = "07";
		else
			Version = "06";
		for(const CGroup &Group : m_Map.m_vGroups)
			for(const CLayer &Layer : Group.m_vLayers)
				if(Layer.m_SourceTilemapVersion >= 4)
					Version = m_Map.m_SourceHasDdnetItems ? "ddnet" : "07";

		m_VersionId = Version;
		SetupResourcePaths(m_VersionId);
		LOG_INFO("editor", "assets of version '%s' selected for this map", m_VersionId.c_str());
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
		SetStatus("new map");
	}

	bool CEditor::OpenFile(const std::string &Path)
	{
		std::string Error;
		const std::optional<storage::byte_vector> Bytes = storage::read(Path);
		if(!Bytes.has_value())
		{
			SetStatus("cannot read " + Path);
			return false;
		}

		if(CUnitwpPackage::IsPackage(*Bytes))
		{
			CUnitwpPackage Package;
			if(!Package.Deserialise(*Bytes, &Error))
			{
				SetStatus("package error: " + Error);
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
				SetStatus("map error: " + Error);
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
		SetStatus(base::Format("opened %s (v%d, %d groups, %d layers, %d images)",
			storage::path(Path).filename().string().c_str(), m_Map.m_SourceDatafileVersion,
			Stats.m_Groups, Stats.m_Layers, Stats.m_Images));
		return true;
	}

	bool CEditor::SavePackage(const std::string &Path)
	{
		std::string Error;
		CUnitwpPackage Package = m_Package;
		Package.m_Map = m_Map;
		if(Package.m_Meta.m_PackageId.empty())
			Package.Adopt(m_Map, storage::path(Path).stem().string(), m_SourcePath);
		Package.m_Meta.m_ExportTarget = GetTargetProfile(m_Target).m_pId;
		Package.m_Meta.m_Author = m_Map.m_aAuthor;
		Package.m_Meta.m_Updated = std::to_string(base::UnixTime());

		// attach the mapres files the map references, so the package travels on
		// its own. Only the images the map actually uses are embedded.
		Package.m_vResources.clear();
		for(const CImage &Image : m_Map.m_vImages)
		{
			if(Image.IsEmbedded())
				continue;
			const std::optional<storage::path> File = FindImageFile(Image.m_Name);
			if(!File.has_value())
				continue;
			const std::optional<storage::byte_vector> Data = storage::read(*File);
			if(!Data.has_value())
				continue;
			CUnitwpResource Resource;
			Resource.m_Name = "mapres/" + File->filename().string();
			Resource.m_Data = *Data;
			Package.m_vResources.push_back(std::move(Resource));
		}

		if(!Package.SaveToFile(Path, &Error))
		{
			SetStatus("save failed: " + Error);
			return false;
		}
		m_Package = Package;
		m_bPackageLoaded = true;
		m_SourcePath = Path;
		m_Dirty = false;
		SetStatus(base::Format("saved %s (%zu resources)", storage::path(Path).filename().string().c_str(), Package.m_vResources.size()));
		return true;
	}

	bool CEditor::ExportMap(const std::string &Path, map_target Target)
	{
		std::string Error;
		for(const std::string &Warning : m_Map.CompatibilityWarnings(Target))
			LOG_WARN("editor", "%s", Warning.c_str());
		if(!m_Map.SaveToFile(Path, Target, &Error))
		{
			SetStatus("export failed: " + Error);
			return false;
		}
		m_Target = Target;
		const CMapTargetProfile &Profile = GetTargetProfile(Target);
		SetStatus(base::Format("exported %s for %s (datafile v%d)",
			storage::path(Path).filename().string().c_str(), Profile.m_pName, Profile.m_DatafileVersion));
		return true;
	}

	void CEditor::SetStatus(const std::string &Message)
	{
		m_Status = Message;
		m_StatusTime = base::Now();
		LOG_INFO("editor", "%s", Message.c_str());
	}

	void CEditor::SetTarget(map_target Target) { m_Target = Target; }

	void CEditor::SetSelectedLayer(int Group, int Layer)
	{
		m_SelectedGroup = Group;
		m_SelectedLayer = Layer;
		ClampSelection();
	}

	void CEditor::ResetTextures()
	{
		if(m_pGraphics != nullptr)
			m_pGraphics->ForgetTextures();
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

		const base::vec2 Window = m_pGraphics != nullptr ? m_pGraphics->WindowSize() : base::vec2(1280.0f, 800.0f);
		const float ZoomX = (Window.x - 600.0f) / MaxX;
		const float ZoomY = (Window.y - 120.0f) / MaxY;
		m_View.m_Zoom = base::clamp(std::min(ZoomX, ZoomY), 0.05f, 4.0f);
		m_View.m_Center = base::vec2(MaxX * 0.5f, MaxY * 0.5f);
	}

	int CEditor::TextureForImage(int ImageIndex)
	{
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return -1;
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];

		if(Image.IsEmbedded())
			return m_pGraphics->CreateTextureRGBA("embedded:" + Image.m_Name, Image.m_Width, Image.m_Height, Image.m_Data);

		// a package may carry the mapres file, that wins over the local one
		for(const CUnitwpResource &Resource : m_Package.m_vResources)
		{
			if(Resource.m_Name != "mapres/" + Image.m_Name + ".png")
				continue;
			const int Cached = m_pGraphics->Texture("res:" + Resource.m_Name);
			if(Cached != -1)
				return Cached;
			const std::optional<CDecodedImage> Decoded = DecodePng(Resource.m_Data);
			if(!Decoded.has_value())
				return -1;
			return m_pGraphics->CreateTextureRGBA("res:" + Resource.m_Name, Decoded->m_Width, Decoded->m_Height, Decoded->m_Pixels);
		}

		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
			return -1;
		return m_pGraphics->LoadTextureFile("mapres:" + Image.m_Name, *File);
	}

	// ------------------------------------------------------------------ undo
	void CEditor::PushUndo(int Group, int Layer)
	{
		if(Group < 0 || Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		const CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(Group)];
		if(Layer < 0 || Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		const CLayer &L = G.m_vLayers[static_cast<std::size_t>(Layer)];
		if(!L.IsTiles())
			return;

		CUndoState State;
		State.m_Group = Group;
		State.m_Layer = Layer;
		State.m_Width = L.m_Width;
		State.m_Height = L.m_Height;
		State.m_vTiles = L.m_vTiles;
		m_vUndo.push_back(std::move(State));
		if(m_vUndo.size() > 32)
			m_vUndo.erase(m_vUndo.begin());
		m_vRedo.clear();
	}

	void CEditor::Undo()
	{
		if(m_vUndo.empty())
		{
			SetStatus("nothing to undo");
			return;
		}
		CUndoState State = std::move(m_vUndo.back());
		m_vUndo.pop_back();
		if(State.m_Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(State.m_Group)];
		if(State.m_Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		CLayer &L = G.m_vLayers[static_cast<std::size_t>(State.m_Layer)];
		m_vRedo.push_back({State.m_Group, State.m_Layer, L.m_Width, L.m_Height, L.m_vTiles});
		L.m_vTiles = State.m_vTiles;
		L.m_Width = State.m_Width;
		L.m_Height = State.m_Height;
		m_Dirty = true;
		SetStatus("undo");
	}

	void CEditor::Redo()
	{
		if(m_vRedo.empty())
		{
			SetStatus("nothing to redo");
			return;
		}
		CUndoState State = std::move(m_vRedo.back());
		m_vRedo.pop_back();
		if(State.m_Group >= static_cast<int>(m_Map.m_vGroups.size()))
			return;
		CGroup &G = m_Map.m_vGroups[static_cast<std::size_t>(State.m_Group)];
		if(State.m_Layer >= static_cast<int>(G.m_vLayers.size()))
			return;
		CLayer &L = G.m_vLayers[static_cast<std::size_t>(State.m_Layer)];
		m_vUndo.push_back({State.m_Group, State.m_Layer, L.m_Width, L.m_Height, L.m_vTiles});
		L.m_vTiles = State.m_vTiles;
		L.m_Width = State.m_Width;
		L.m_Height = State.m_Height;
		m_Dirty = true;
		SetStatus("redo");
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
		SetStatus("edited " + std::string(ToolName(m_Tool)));
	}

	void CEditor::PaintAt(base::vec2 World)
	{
		CLayer *pLayer = SelectedLayer();
		if(pLayer == nullptr || !pLayer->IsTiles())
			return;
		const CLayerView *pView = ViewState(m_SelectedGroup, m_SelectedLayer);
		if(pView != nullptr && pView->m_Locked)
		{
			SetStatus("this layer is locked (DDRace physics layers are read only)");
			return;
		}

		const int TileX = static_cast<int>(std::floor(World.x / TileSize));
		const int TileY = static_cast<int>(std::floor(World.y / TileSize));
		if(m_StrokeLastTile.x == TileX && m_StrokeLastTile.y == TileY)
			return;
		m_StrokeLastTile = base::ivec2(TileX, TileY);

		// the tile flags stored in a map also carry the opaque bit of the image
		int ExtraFlags = 0;
		if(pLayer->m_Image >= 0 && pLayer->m_Image < static_cast<int>(m_Map.m_vImages.size()))
		{
			const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(pLayer->m_Image)];
			if(Image.m_aTileFlagsValid && m_BrushTile >= 0 && m_BrushTile < 256)
				ExtraFlags = Image.m_aTileFlags[static_cast<std::size_t>(m_BrushTile)] & TILEFLAG_OPAQUE;
		}

		const int Half = m_BrushSize / 2;
		for(int y = TileY - Half; y < TileY - Half + m_BrushSize; y++)
		{
			for(int x = TileX - Half; x < TileX - Half + m_BrushSize; x++)
			{
				CTile *pTile = pLayer->Tile(x, y);
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
						pTile->m_Index = static_cast<std::uint8_t>(base::clamp(m_BrushTile, 0, 255));
						pTile->m_Flags = 0;
					}
					else
					{
						pTile->m_Index = static_cast<std::uint8_t>(base::clamp(m_BrushTile, 0, 255));
						pTile->m_Flags = static_cast<std::uint8_t>(m_BrushFlags | ExtraFlags);
					}
				}
			}
		}
		m_Dirty = true;
	}

	// -------------------------------------------------------------- shortcuts
	void CEditor::HandleShortcuts()
	{
		engine::CInput &Input = m_pGraphics->Input();
		const bool Ctrl = Input.KeyDown(GLFW_KEY_LEFT_CONTROL) || Input.KeyDown(GLFW_KEY_RIGHT_CONTROL);

		if(Ctrl && Input.KeyPressed(GLFW_KEY_Q))
			m_pGraphics->RequestQuit();
		if(Ctrl && Input.KeyPressed(GLFW_KEY_N))
			NewMap(50, 50);
		if(Ctrl && Input.KeyPressed(GLFW_KEY_O))
		{
			m_bShowOpen = true;
			m_BrowserSelected = 0;
			m_BrowserScroll = 0.0f;
			m_vBrowserEntries.clear();
			const storage::path Maps = storage::GetDataDirectory() / "maps";
			for(const storage::path &Directory : storage::list_subdirectories(Maps))
				for(const storage::path &File : storage::list_directory(Directory, ".map"))
					m_vBrowserEntries.push_back(File.string());
			for(const storage::path &File : storage::list_directory(Maps, ".map"))
				m_vBrowserEntries.push_back(File.string());
			// and the maps next to the binary, which is where exports land
			for(const storage::path &File : storage::list_directory(std::filesystem::current_path(), ".map"))
				m_vBrowserEntries.push_back(File.string());
			for(const storage::path &File : storage::list_directory(std::filesystem::current_path(), ".unitwp"))
				m_vBrowserEntries.push_back(File.string());
		}
		if(Ctrl && Input.KeyPressed(GLFW_KEY_S))
		{
			std::string Path = m_SourcePath;
			if(!base::EndsWith(Path, CUnitwpPackage::Extension))
				Path = storage::path(Path.empty() ? "untitled" : Path).stem().string() + CUnitwpPackage::Extension;
			SavePackage(Path);
		}
		if(Ctrl && Input.KeyPressed(GLFW_KEY_E))
			m_bShowExport = true;
		if(Ctrl && Input.KeyPressed(GLFW_KEY_Z))
			Undo();
		if(Ctrl && Input.KeyPressed(GLFW_KEY_Y))
			Redo();
		if(Ctrl && Input.KeyPressed(GLFW_KEY_L))
		{
			if(CGroup *pGroup = SelectedGroup())
			{
				CLayer &Layer = m_Map.NewTileLayer(*pGroup, 50, 50, -1);
				Layer.m_Name = "Tile layer";
				EnsureViewStates();
				m_SelectedLayer = static_cast<int>(pGroup->m_vLayers.size()) - 1;
				m_Dirty = true;
				SetStatus("added a tile layer");
			}
		}
		if(Ctrl && Input.KeyPressed(GLFW_KEY_G))
		{
			CGroup &Group = m_Map.NewGroup();
			Group.m_Name = "Group";
			m_Map.NewTileLayer(Group, 50, 50, -1);
			EnsureViewStates();
			m_SelectedGroup = static_cast<int>(m_Map.m_vGroups.size()) - 1;
			m_SelectedLayer = 0;
			m_Dirty = true;
			SetStatus("added a group");
		}
		if(Input.KeyPressed(GLFW_KEY_DELETE) || Input.KeyPressed(GLFW_KEY_BACKSPACE))
		{
			const bool WholeGroup = Input.KeyDown(GLFW_KEY_LEFT_SHIFT) || Input.KeyDown(GLFW_KEY_RIGHT_SHIFT);
			if(WholeGroup)
			{
				if(m_Map.m_vGroups.size() > 1 && m_SelectedGroup < static_cast<int>(m_Map.m_vGroups.size()))
				{
					m_Map.m_vGroups.erase(m_Map.m_vGroups.begin() + m_SelectedGroup);
					m_vViewStates.clear();
					EnsureViewStates();
					ClampSelection();
					m_Dirty = true;
					SetStatus("deleted the group");
				}
			}
			else if(CGroup *pGroup = SelectedGroup())
			{
				if(m_SelectedLayer >= 0 && m_SelectedLayer < static_cast<int>(pGroup->m_vLayers.size()) && !pGroup->m_vLayers.empty())
				{
					pGroup->m_vLayers.erase(pGroup->m_vLayers.begin() + m_SelectedLayer);
					m_vViewStates.clear();
					EnsureViewStates();
					ClampSelection();
					m_Dirty = true;
					SetStatus("deleted the layer");
				}
			}
		}
		if(Input.KeyPressed(GLFW_KEY_F))
			FitToMap();
		if(Input.KeyPressed(GLFW_KEY_G))
			m_bGrid = !m_bGrid;
		if(Input.KeyPressed(GLFW_KEY_1))
			m_Tool = editor_tool::paint;
		if(Input.KeyPressed(GLFW_KEY_2))
			m_Tool = editor_tool::erase;
		if(Input.KeyPressed(GLFW_KEY_3))
			m_Tool = editor_tool::pick;
		if(Input.KeyPressed(GLFW_KEY_4))
			m_BrushSize = 1;
		if(Input.KeyPressed(GLFW_KEY_5))
			m_BrushSize = 2;
		if(Input.KeyPressed(GLFW_KEY_6))
			m_BrushSize = 4;
		if(Input.KeyPressed(GLFW_KEY_7))
			m_BrushSize = 8;
		if(Input.KeyPressed(GLFW_KEY_LEFT_BRACKET))
			m_BrushSize = std::max(1, m_BrushSize / 2);
		if(Input.KeyPressed(GLFW_KEY_RIGHT_BRACKET))
			m_BrushSize = std::min(16, m_BrushSize * 2);
		if(Input.KeyPressed(GLFW_KEY_H))
			m_BrushFlags ^= TILEFLAG_HFLIP;
		if(Input.KeyPressed(GLFW_KEY_J))
			m_BrushFlags ^= TILEFLAG_VFLIP;
		if(Input.KeyPressed(GLFW_KEY_K))
			m_BrushFlags ^= TILEFLAG_ROTATE;
		if(Input.KeyPressed(GLFW_KEY_Q))
			m_BrushTile = (m_BrushTile + 255) % 256;
		if(Input.KeyPressed(GLFW_KEY_E))
			m_BrushTile = (m_BrushTile + 1) % 256;
		if(Input.KeyPressed(GLFW_KEY_P))
		{
			if(CLayer *pLayer = SelectedLayer())
				if(pLayer->IsTiles())
				{
					PushUndo(m_SelectedGroup, m_SelectedLayer);
					for(std::size_t i = 0; i < pLayer->m_vTiles.size(); i++)
					{
						pLayer->m_vTiles[i].m_Index = static_cast<std::uint8_t>(base::clamp(m_BrushTile, 0, 255));
						pLayer->m_vTiles[i].m_Flags = static_cast<std::uint8_t>(m_BrushFlags);
					}
					m_Dirty = true;
					SetStatus("filled the layer");
				}
		}
	}

	void CEditor::HandleCanvasInput(const base::rect &Canvas)
	{
		engine::CInput &Input = m_pGraphics->Input();
		const bool bInCanvas = Canvas.Contains(Input.MousePosition());

		// panning with the middle or the right button, zoom with the wheel
		if(bInCanvas && (Input.MouseDown(engine::CInput::mouse_button::middle) ||
						   (Input.MouseDown(engine::CInput::mouse_button::right) && Input.ModifierDown(GLFW_KEY_LEFT_ALT))))
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
		if(Input.KeyDown(GLFW_KEY_A) || Input.KeyDown(GLFW_KEY_LEFT))
			m_View.m_Center.x -= PanSpeed;
		if(Input.KeyDown(GLFW_KEY_D) || Input.KeyDown(GLFW_KEY_RIGHT))
			m_View.m_Center.x += PanSpeed;
		if(Input.KeyDown(GLFW_KEY_W) || Input.KeyDown(GLFW_KEY_UP))
			m_View.m_Center.y -= PanSpeed;
		if(Input.KeyDown(GLFW_KEY_S) || Input.KeyDown(GLFW_KEY_DOWN))
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

		// right click picks the tile under the cursor
		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::right) && !Input.ModifierDown(GLFW_KEY_LEFT_ALT))
		{
			if(const CLayer *pLayer = SelectedLayer())
				if(pLayer->IsTiles())
				{
					const int x = static_cast<int>(std::floor(m_CursorWorld.x / TileSize));
					const int y = static_cast<int>(std::floor(m_CursorWorld.y / TileSize));
					if(const CTile *pTile = pLayer->Tile(x, y))
					{
						m_BrushTile = pTile->m_Index;
						m_BrushFlags = pTile->m_Flags & (TILEFLAG_HFLIP | TILEFLAG_VFLIP | TILEFLAG_ROTATE);
						SetStatus(base::Format("picked tile %d flags %d", m_BrushTile, m_BrushFlags));
					}
				}
		}

		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::left))
		{
			if(m_Tool == editor_tool::pick)
			{
				if(const CLayer *pLayer = SelectedLayer())
					if(pLayer->IsTiles())
					{
						const int x = static_cast<int>(std::floor(m_CursorWorld.x / TileSize));
						const int y = static_cast<int>(std::floor(m_CursorWorld.y / TileSize));
						if(const CTile *pTile = pLayer->Tile(x, y))
						{
							m_BrushTile = pTile->m_Index;
							m_BrushFlags = pTile->m_Flags & (TILEFLAG_HFLIP | TILEFLAG_VFLIP | TILEFLAG_ROTATE);
						}
					}
			}
			else
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

	void CEditor::Update()
	{
		// a minimized window can report itself as empty; there is nothing to lay
		// out or hit test then, and nanovg must not be handed a zero size
		if(m_pGraphics != nullptr && (m_pGraphics->WindowSize().x < 1.0f || m_pGraphics->WindowSize().y < 1.0f))
			return;

		EnsureViewStates();
		ClampSelection();

		const base::vec2 Size = m_pGraphics->WindowSize();
		constexpr float ToolbarHeight = 40.0f;
		constexpr float PanelWidth = 268.0f;
		constexpr float StatusHeight = 26.0f;
		const base::rect Canvas = base::rect(
			base::vec2(PanelWidth, ToolbarHeight),
			base::vec2(Size.x - PanelWidth * 2.0f, Size.y - ToolbarHeight - StatusHeight));
		m_View.m_Viewport = base::vec2(std::max(Canvas.Width(), 1.0f), std::max(Canvas.Height(), 1.0f));

		HandleShortcuts();

		// popups take the input first
		if(m_bShowOpen || m_bShowSave || m_bShowExport || m_bShowHelp)
			return;

		HandleCanvasInput(Canvas);
	}
} // namespace game
