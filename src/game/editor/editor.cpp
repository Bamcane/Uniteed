#include "editor.h"

#include <algorithm>
#include <cstring>
#include <map>

#include <SDL3/SDL_keycode.h>

#include <cmath>

#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/ui.h"
#include "game/assets.h"
#include "game/map/envelope.h"

namespace game
{
	namespace ui = engine::ui;

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

		ui::CColor FlagColor(int LayerFlags)
		{
			if(LayerFlags & TILESLAYERFLAG_TELE)
				return ui::RGBA(120, 200, 255, 90);
			if(LayerFlags & TILESLAYERFLAG_SPEEDUP)
				return ui::RGBA(255, 210, 90, 90);
			if(LayerFlags & TILESLAYERFLAG_SWITCH)
				return ui::RGBA(190, 130, 255, 90);
			if(LayerFlags & TILESLAYERFLAG_TUNE)
				return ui::RGBA(255, 130, 200, 90);
			if(LayerFlags & TILESLAYERFLAG_FRONT)
				return ui::RGBA(140, 255, 160, 80);
			return ui::RGBA(160, 160, 160, 80);
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

	base::rect CEditor::CanvasRect(const base::vec2 &Window) const
	{
		// the canvas is what the toolbar, the two panels and the status bar
		// leave over; clamped so a tiny window still gives a usable viewport
		return base::rect::FromSize(base::vec2(PanelWidth, ToolbarHeight),
			base::vec2(std::max(Window.x - PanelWidth * 2.0f, 1.0f), std::max(Window.y - ToolbarHeight - StatusHeight, 1.0f)));
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

		const base::vec2 Window = m_pGraphics != nullptr ? m_pGraphics->WindowSize() : base::vec2(1280.0f, 800.0f);
		const base::rect Canvas = CanvasRect(Window);
		const float ZoomX = (Canvas.Width() - 48.0f) / MaxX;
		const float ZoomY = (Canvas.Height() - 48.0f) / MaxY;
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

	// ----------------------------------------------------------------- colours
	namespace
	{
		int TintChannel(int Value, float Scale)
		{
			return base::clamp(static_cast<int>(std::lround(static_cast<float>(Value) * Scale)), 0, 255);
		}
	} // namespace

	/*
		The layer colour multiplies the texture, exactly like the reference does
		it with vertex colours. An image fill can only be faded by a
		scalar alpha, so a coloured tint needs a copy of the image whose rgb is
		already multiplied; those copies are kept in a small, fixed set of slots
		per image, because a colour envelope animates and an unbounded texture
		cache would grow with every frame of it.

		Animated alpha (the common case, a fading layer) uses the pattern alpha
		and costs nothing; only an animated *rgb* needs its slot rewritten, which
		is a tint and one texture upload per frame.
	*/
	int CEditor::TextureForImage(int ImageIndex, const CColor &Tint)
	{
		if(Tint.r >= 255 && Tint.g >= 255 && Tint.b >= 255)
			return TextureForImage(ImageIndex);
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return -1;

		m_TintClock++;

		// an exact hit first: the static colour of a layer stays in its slot
		int Slot = -1;
		std::size_t PerImage = 0;
		std::size_t LeastUsed = 0;
		unsigned LeastUsedClock = 0;
		bool bHasLeastUsed = false;
		for(std::size_t i = 0; i < m_vTintSlots.size(); i++)
		{
			CTintSlot &Candidate = m_vTintSlots[i];
			if(Candidate.m_Image == ImageIndex && Candidate.m_Tint.r == Tint.r && Candidate.m_Tint.g == Tint.g && Candidate.m_Tint.b == Tint.b)
			{
				Candidate.m_LastUse = m_TintClock;
				return m_pGraphics->Texture(TintSlotKey(i));
			}
			if(Candidate.m_Image == ImageIndex)
			{
				PerImage++;
				if(!bHasLeastUsed || Candidate.m_LastUse < LeastUsedClock)
				{
					bHasLeastUsed = true;
					LeastUsedClock = Candidate.m_LastUse;
					LeastUsed = i;
				}
			}
		}

		// then a free slot, then the oldest slot of this image, and only if this
		// image has fewer than the cap one more slot
		for(std::size_t i = 0; i < m_vTintSlots.size() && Slot < 0; i++)
			if(m_vTintSlots[i].m_Image < 0)
				Slot = static_cast<int>(i);
		if(Slot < 0 && PerImage < MaxTintSlotsPerImage)
		{
			m_vTintSlots.push_back(CTintSlot{});
			Slot = static_cast<int>(m_vTintSlots.size()) - 1;
		}
		if(Slot < 0 && bHasLeastUsed)
			Slot = static_cast<int>(LeastUsed);
		if(Slot < 0)
			return TextureForImage(ImageIndex); // out of slots, show it untinted

		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];
		int SourceWidth = Image.m_Width;
		int SourceHeight = Image.m_Height;
		std::vector<std::byte> Source;
		if(Image.IsEmbedded())
		{
			if(Image.m_Data.empty() || SourceWidth <= 0 || SourceHeight <= 0)
				return -1;
			Source = Image.m_Data;
		}
		else
		{
			if(!TintSource(ImageIndex, Source, SourceWidth, SourceHeight))
				return -1;
		}

		const std::size_t Count = static_cast<std::size_t>(SourceWidth) * static_cast<std::size_t>(SourceHeight);
		if(Source.size() < Count * 4)
			return -1;

		const float Red = static_cast<float>(Tint.r) / 255.0f;
		const float Green = static_cast<float>(Tint.g) / 255.0f;
		const float Blue = static_cast<float>(Tint.b) / 255.0f;
		storage::byte_vector Tinted(Source.size());
		for(std::size_t i = 0; i < Count; i++)
		{
			const std::size_t At = i * 4;
			Tinted[At + 0] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 0]), Red));
			Tinted[At + 1] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 1]), Green));
			Tinted[At + 2] = static_cast<std::byte>(TintChannel(static_cast<unsigned char>(Source[At + 2]), Blue));
			Tinted[At + 3] = Source[At + 3]; // the alpha comes from the pattern
		}

		CTintSlot &Chosen = m_vTintSlots[static_cast<std::size_t>(Slot)];
		const std::string Key = TintSlotKey(static_cast<std::size_t>(Slot));
		int Handle = -1;
		if(Chosen.m_Image == ImageIndex && Chosen.m_Width == SourceWidth && Chosen.m_Height == SourceHeight)
			Handle = m_pGraphics->UpdateTextureRGBA(Key, SourceWidth, SourceHeight, Tinted) ? m_pGraphics->Texture(Key) : -1;
		else
			m_pGraphics->ForgetTexture(Key); // the slot changes image or size
		if(Handle == -1)
			Handle = m_pGraphics->CreateTextureRGBA(Key, SourceWidth, SourceHeight, Tinted);
		if(Handle == -1)
			return TextureForImage(ImageIndex);

		Chosen.m_Image = ImageIndex;
		Chosen.m_Tint = Tint;
		Chosen.m_Width = SourceWidth;
		Chosen.m_Height = SourceHeight;
		Chosen.m_LastUse = m_TintClock;
		return Handle;
	}

	// the decoded pixels of a mapres file, kept because a tint rewrite should
	// not decode the png again
	bool CEditor::TintSource(int ImageIndex, std::vector<std::byte> &Pixels, int &Width, int &Height)
	{
		if(ImageIndex < 0 || ImageIndex >= static_cast<int>(m_Map.m_vImages.size()))
			return false;
		const CImage &Image = m_Map.m_vImages[static_cast<std::size_t>(ImageIndex)];

		const auto Cached = m_SourceImages.find(ImageIndex);
		if(Cached != m_SourceImages.end())
		{
			Pixels = Cached->second.m_Pixels;
			Width = Cached->second.m_Width;
			Height = Cached->second.m_Height;
			return !Pixels.empty();
		}

		const std::optional<storage::path> File = FindImageFile(Image.m_Name);
		if(!File.has_value())
			return false;
		const std::optional<storage::byte_vector> Bytes = storage::read(*File);
		if(!Bytes.has_value())
			return false;
		const std::optional<CDecodedImage> Decoded = DecodePng(*Bytes);
		if(!Decoded.has_value())
			return false;

		CSourceImage Stored;
		Stored.m_Width = Decoded->m_Width;
		Stored.m_Height = Decoded->m_Height;
		Stored.m_Pixels = Decoded->m_Pixels;
		const auto Inserted = m_SourceImages.emplace(ImageIndex, std::move(Stored));
		Pixels = Inserted.first->second.m_Pixels;
		Width = Inserted.first->second.m_Width;
		Height = Inserted.first->second.m_Height;
		return !Pixels.empty();
	}

	std::string CEditor::TintSlotKey(std::size_t Slot) const
	{
		return base::Format("tintslot:%zu", Slot);
	}

	CColor CEditor::LayerColor(const CLayer &Layer) const
	{
		CEnvelopeSample Environment;
		Environment[0] = Environment[1] = Environment[2] = Environment[3] = 1.0f;
		if(Layer.m_ColorEnv >= 0 && Layer.m_ColorEnv < static_cast<int>(m_Map.m_vEnvelopes.size()))
			EvaluateEnvelope(m_Map.m_vEnvelopes[static_cast<std::size_t>(Layer.m_ColorEnv)], m_EnvelopeTime + Layer.m_ColorEnvOffset / 1000.0f, Environment, 4);

		CColor Color;
		Color.r = TintChannel(Layer.m_Color.r, Environment[0]);
		Color.g = TintChannel(Layer.m_Color.g, Environment[1]);
		Color.b = TintChannel(Layer.m_Color.b, Environment[2]);
		Color.a = TintChannel(Layer.m_Color.a, Environment[3]);
		return Color;
	}

	void CEditor::QuadColors(const CQuad &Quad, CColor *paColors) const
	{
		CEnvelopeSample Environment;
		Environment[0] = Environment[1] = Environment[2] = Environment[3] = 1.0f;
		if(Quad.m_ColorEnv >= 0 && Quad.m_ColorEnv < static_cast<int>(m_Map.m_vEnvelopes.size()))
			EvaluateEnvelope(m_Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_ColorEnv)], m_EnvelopeTime + Quad.m_ColorEnvOffset / 1000.0f, Environment, 4);

		for(int i = 0; i < 4; i++)
		{
			paColors[i].r = TintChannel(Quad.m_aColors[i].r, Environment[0]);
			paColors[i].g = TintChannel(Quad.m_aColors[i].g, Environment[1]);
			paColors[i].b = TintChannel(Quad.m_aColors[i].b, Environment[2]);
			paColors[i].a = TintChannel(Quad.m_aColors[i].a, Environment[3]);
		}
	}

	void CEditor::QuadPoints(const CQuad &Quad, base::vec2 *paPoints) const
	{
		CEnvelopeSample Position;
		if(Quad.m_PosEnv >= 0 && Quad.m_PosEnv < static_cast<int>(m_Map.m_vEnvelopes.size()))
			EvaluateEnvelope(m_Map.m_vEnvelopes[static_cast<std::size_t>(Quad.m_PosEnv)], m_EnvelopeTime + Quad.m_PosEnvOffset / 1000.0f, Position, 3);

		const base::vec2 Offset(Position[0], Position[1]);
		const float Rotation = Position[2] / 180.0f * base::Pi;
		// the fifth point is the pivot the rotation turns around
		const base::vec2 Pivot(base::FixedToFloat(Quad.m_aPoints[4].x), base::FixedToFloat(Quad.m_aPoints[4].y));

		for(int i = 0; i < 4; i++)
		{
			base::vec2 Point(base::FixedToFloat(Quad.m_aPoints[i].x), base::FixedToFloat(Quad.m_aPoints[i].y));
			if(Rotation != 0.0f)
			{
				const base::vec2 Relative = Point - Pivot;
				const float Cos = std::cos(Rotation);
				const float Sin = std::sin(Rotation);
				Point = base::vec2(Relative.x * Cos - Relative.y * Sin, Relative.x * Sin + Relative.y * Cos) + Pivot;
			}
			paPoints[i] = Point + Offset;
		}
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
		// the hue is derived from the colour while the picker is drawn
		return true;
	}

	bool CEditor::PickerTargetsQuad() const
	{
		const CLayer *pLayer = SelectedLayer();
		return pLayer != nullptr && pLayer->IsQuads();
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
			Color = pLayer->m_vQuads[static_cast<std::size_t>(Index)].m_aColors[0];
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
			for(const int Index : Indices)
			{
				if(Index < 0 || Index >= static_cast<int>(pLayer->m_vQuads.size()))
					continue;
				for(int i = 0; i < 4; i++)
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
			return "nothing";
		const char *pName = pLayer->m_Name.empty() ? "layer" : pLayer->m_Name.c_str();
		if(pLayer->IsQuads())
			return m_vSelectedQuads.empty() ?
				(pLayer->m_vQuads.size() == 1 ? base::Format("the quad of '%s'", pName) : base::Format("all %zu quads of '%s'", pLayer->m_vQuads.size(), pName)) :
				base::Format("%zu of %zu quads of '%s'", m_vSelectedQuads.size(), pLayer->m_vQuads.size(), pName);
		if(pLayer->IsTiles())
			return base::Format("layer '%s'", pName);
		return "nothing";
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
		else
			State.m_Color = Layer.m_Color;
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
		ApplyUndoState(State, m_vRedo);
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
		ApplyUndoState(State, m_vUndo);
		m_Dirty = true;
		SetStatus("redo");
	}

	// puts the current state of the layer on the other stack, then restores the
	// captured one; both stacks must move together or redo would lose the scope
	void CEditor::ApplyUndoState(const CUndoState &State, std::vector<CUndoState> &Other)
	{
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
		}
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
		const bool Ctrl = Input.KeyDown(SDLK_LCTRL) || Input.KeyDown(SDLK_RCTRL);

		if(Ctrl && Input.KeyPressed(SDLK_Q))
			m_pGraphics->RequestQuit();
		if(Ctrl && Input.KeyPressed(SDLK_N))
			NewMap(50, 50);
		if(Ctrl && Input.KeyPressed(SDLK_O))
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
		if(Ctrl && Input.KeyPressed(SDLK_S))
		{
			std::string Path = m_SourcePath;
			if(!base::EndsWith(Path, CUnitwpPackage::Extension))
				Path = storage::path(Path.empty() ? "untitled" : Path).stem().string() + CUnitwpPackage::Extension;
			SavePackage(Path);
		}
		if(Ctrl && Input.KeyPressed(SDLK_E))
			m_bShowExport = true;
		if(Ctrl && Input.KeyPressed(SDLK_Z))
			Undo();
		if(Ctrl && Input.KeyPressed(SDLK_Y))
			Redo();
		if(Ctrl && Input.KeyPressed(SDLK_L))
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
		if(Ctrl && Input.KeyPressed(SDLK_G))
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
		if(Input.KeyPressed(SDLK_DELETE) || Input.KeyPressed(SDLK_BACKSPACE))
		{
			const bool WholeGroup = Input.KeyDown(SDLK_LSHIFT) || Input.KeyDown(SDLK_RSHIFT);
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
		if(Input.KeyPressed(SDLK_F))
			FitToMap();
		if(Input.KeyPressed(SDLK_G))
			m_bGrid = !m_bGrid;
		// the envelope preview clock
		if(Input.KeyPressed(SDLK_SPACE))
		{
			m_bEnvelopePlaying = !m_bEnvelopePlaying;
			SetStatus(m_bEnvelopePlaying ? "envelope preview running" : "envelope preview paused");
		}
		if(Input.KeyPressed(SDLK_MINUS))
		{
			m_EnvelopeSpeed = m_EnvelopeSpeed <= 0.25f ? 0.25f : m_EnvelopeSpeed * 0.5f;
			SetStatus(base::Format("envelope speed %.2gx", m_EnvelopeSpeed));
		}
		if(Input.KeyPressed(SDLK_EQUALS))
		{
			m_EnvelopeSpeed = std::min(8.0f, m_EnvelopeSpeed * 2.0f);
			SetStatus(base::Format("envelope speed %.2gx", m_EnvelopeSpeed));
		}
		// the colour picker for the selected tile layer or quads
		if(Input.KeyPressed(SDLK_C))
		{
			if(!OpenColorPicker())
				SetStatus("nothing to colour: select a tile layer or a quad");
		}
		if(Input.KeyPressed(SDLK_1))
			m_Tool = editor_tool::paint;
		if(Input.KeyPressed(SDLK_2))
			m_Tool = editor_tool::erase;
		if(Input.KeyPressed(SDLK_3))
			m_Tool = editor_tool::pick;
		if(Input.KeyPressed(SDLK_4))
			m_BrushSize = 1;
		if(Input.KeyPressed(SDLK_5))
			m_BrushSize = 2;
		if(Input.KeyPressed(SDLK_6))
			m_BrushSize = 4;
		if(Input.KeyPressed(SDLK_7))
			m_BrushSize = 8;
		if(Input.KeyPressed(SDLK_LEFTBRACKET))
			m_BrushSize = std::max(1, m_BrushSize / 2);
		if(Input.KeyPressed(SDLK_RIGHTBRACKET))
			m_BrushSize = std::min(16, m_BrushSize * 2);
		if(Input.KeyPressed(SDLK_H))
			m_BrushFlags ^= TILEFLAG_HFLIP;
		if(Input.KeyPressed(SDLK_J))
			m_BrushFlags ^= TILEFLAG_VFLIP;
		if(Input.KeyPressed(SDLK_K))
			m_BrushFlags ^= TILEFLAG_ROTATE;
		if(Input.KeyPressed(SDLK_Q))
			m_BrushTile = (m_BrushTile + 255) % 256;
		if(Input.KeyPressed(SDLK_E))
			m_BrushTile = (m_BrushTile + 1) % 256;
		if(Input.KeyPressed(SDLK_P))
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
						   (Input.MouseDown(engine::CInput::mouse_button::right) && Input.ModifierDown(SDLK_LALT))))
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
		if(Input.KeyDown(SDLK_A) || Input.KeyDown(SDLK_LEFT))
			m_View.m_Center.x -= PanSpeed;
		if(Input.KeyDown(SDLK_D) || Input.KeyDown(SDLK_RIGHT))
			m_View.m_Center.x += PanSpeed;
		if(Input.KeyDown(SDLK_W) || Input.KeyDown(SDLK_UP))
			m_View.m_Center.y -= PanSpeed;
		if(Input.KeyDown(SDLK_S) || Input.KeyDown(SDLK_DOWN))
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
		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::right) && !Input.ModifierDown(SDLK_LALT))
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

		// the quads of the selected quad layer are picked with the left button,
		// which is also what gives the colour picker something to edit
		UpdateQuadHover(bInCanvas);
		if(bInCanvas && Input.MousePressed(engine::CInput::mouse_button::left) && IsQuadLayerSelected())
		{
			const bool bAdd = Input.ModifierDown(SDLK_LCTRL) || Input.ModifierDown(SDLK_RCTRL);
			if(m_QuadHover < 0)
			{
				if(!bAdd && !m_vSelectedQuads.empty())
					m_vSelectedQuads.clear();
			}
			else if(bAdd)
			{
				const auto At = std::find(m_vSelectedQuads.begin(), m_vSelectedQuads.end(), m_QuadHover);
				if(At != m_vSelectedQuads.end())
					m_vSelectedQuads.erase(At);
				else
					m_vSelectedQuads.push_back(m_QuadHover);
			}
			else if(m_vSelectedQuads.size() != 1 || m_vSelectedQuads[0] != m_QuadHover)
			{
				m_vSelectedQuads.assign(1, m_QuadHover);
			}
			if(!m_vSelectedQuads.empty())
			{
				std::sort(m_vSelectedQuads.begin(), m_vSelectedQuads.end());
				CColor Color;
				if(PickerColor(Color))
					m_PickerColor = Color;
			}
			SetStatus(m_vSelectedQuads.empty() ? "no quad selected" :
				base::Format("%zu quad%s selected, press C for the colour", m_vSelectedQuads.size(), m_vSelectedQuads.size() == 1 ? "" : "s"));
			return;
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

	bool CEditor::IsQuadLayerSelected() const
	{
		const CLayer *pLayer = SelectedLayer();
		return pLayer != nullptr && pLayer->IsQuads();
	}

	// the quad under the mouse, topmost last so the newest is picked first
	void CEditor::UpdateQuadHover(bool bInCanvas)
	{
		m_QuadHover = -1;
		const CLayer *pLayer = SelectedLayer();
		if(!bInCanvas || pLayer == nullptr || !pLayer->IsQuads())
			return;

		const base::vec2 Mouse = m_pGraphics->Input().MousePosition();
		for(int Index = static_cast<int>(pLayer->m_vQuads.size()) - 1; Index >= 0; Index--)
		{
			base::vec2 aWorld[4];
			QuadPoints(pLayer->m_vQuads[static_cast<std::size_t>(Index)], aWorld);
			base::vec2 aScreen[4];
			for(int i = 0; i < 4; i++)
				aScreen[i] = m_View.WorldToScreen(aWorld[i]);
			if(base::PointInQuad(aScreen, Mouse))
			{
				m_QuadHover = Index;
				return;
			}
		}
	}

	void CEditor::Update()
	{
		// a minimized window can report itself as empty; there is nothing to lay
		// out or hit test then, and the painter must not be handed a zero size
		if(m_pGraphics != nullptr && (m_pGraphics->WindowSize().x < 1.0f || m_pGraphics->WindowSize().y < 1.0f))
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

		EnsureViewStates();
		ClampSelection();

		const base::rect Canvas = CanvasRect(m_pGraphics->WindowSize());
		// the view transform works in window coordinates: the canvas origin is
		// what makes the drawn tile and the clicked tile the same tile
		m_View.m_Origin = Canvas.m_TopLeft;
		m_View.m_Viewport = base::vec2(Canvas.Width(), Canvas.Height());

		// a popup owns the keyboard and the canvas while it is open, otherwise
		// typing a file name would also trigger the editor shortcuts
		if(m_bShowOpen || m_bShowSave || m_bShowExport || m_bShowHelp || m_bShowColor)
		{
			HandlePopupInput();
			return;
		}

		HandleShortcuts();
		HandleCanvasInput(Canvas);
	}
} // namespace game
