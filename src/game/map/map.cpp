#include "map.h"

#include <algorithm>
#include <cstring>

#include "base/logger.h"
#include "base/string.h"

namespace game
{
	namespace
	{
		const CMapTargetProfile g_aProfiles[] = {
			{"06", "Teeworlds 0.6", "datafile 4, tilemap 3, image 1 (0.6.1/0.6.5)",
				4, 3, 1, 3, 2, 2, 20, false, false, false, false, false},
			{"07", "Teeworlds 0.7", "datafile 4, tilemap 4 with run length tiles, image 2",
				4, 4, 2, 3, 2, 2, 20, false, true, true, true, false},
			{"ddnet", "DDRaceNetwork 0.6", "datafile 4, tilemap 3 with the DDRace physics data indices",
				4, 3, 1, 3, 2, 2, 24, true, false, true, false, true},
			{"08", "Teeworlds 0.8", "datafile 5 with zstd, tilemap 4 with run length tiles",
				5, 4, 2, 3, 2, 2, 20, false, true, true, true, false},
			// the package profile is a superset: it keeps the DDRace physics
			// layers and the UUID items, and it keeps the bezier tangents in the
			// envelope points. Datafile 5 is used when zstd is available because
			// empty tile runs compress very well even without run length coding.
			{"unitwp", "Uniteed package", "datafile 5, DDRace tilemaps, every item preserved",
#if defined(UNITEED_ZSTD)
				5, 3, 2, 3, 2, 2, 24, true, false, true, true, true},
#else
				4, 3, 2, 3, 2, 2, 24, true, false, true, true, true},
#endif
		};

		const CMapTargetProfile g_InvalidProfile{"?", "unknown", "unknown", 0, 0, 0, 0, 0, 0, 0, false, false, false, false, false};
	} // namespace

	const CMapTargetProfile &GetTargetProfile(map_target Target)
	{
		switch(Target)
		{
		case map_target::tee06: return g_aProfiles[0];
		case map_target::tee07: return g_aProfiles[1];
		case map_target::ddnet: return g_aProfiles[2];
		case map_target::tee08: return g_aProfiles[3];
		case map_target::unitwp: return g_aProfiles[4];
		}
		return g_InvalidProfile;
	}

	map_target GetPackageTarget() { return map_target::unitwp; }

	std::optional<map_target> TargetFromId(std::string_view Id)
	{
		const std::string Lower = base::ToLower(std::string(Id));
		if(Lower == "06" || Lower == "0.6" || Lower == "teeworlds 0.6" || Lower == "tee06" || Lower == "6")
			return map_target::tee06;
		if(Lower == "07" || Lower == "0.7" || Lower == "teeworlds 0.7" || Lower == "tee07" || Lower == "7")
			return map_target::tee07;
		if(Lower == "08" || Lower == "0.8" || Lower == "teeworlds 0.8" || Lower == "tee08" || Lower == "8")
			return map_target::tee08;
		if(Lower == "ddnet" || Lower == "ddrace" || Lower == "ddracenetwork")
			return map_target::ddnet;
		if(Lower == "unitwp" || Lower == "package")
			return map_target::unitwp;
		return std::nullopt;
	}

	const std::array<map_target, 4> &AllTargets()
	{
		static const std::array<map_target, 4> s_aTargets = {map_target::tee06, map_target::tee07, map_target::ddnet, map_target::tee08};
		return s_aTargets;
	}

	// ------------------------------------------------------------------- layers
	CTile *CLayer::Tile(int x, int y)
	{
		if(!InBounds(x, y))
			return nullptr;
		return &m_vTiles[static_cast<std::size_t>(y) * m_Width + x];
	}

	const CTile *CLayer::Tile(int x, int y) const
	{
		if(!InBounds(x, y))
			return nullptr;
		return &m_vTiles[static_cast<std::size_t>(y) * m_Width + x];
	}

	void CLayer::Resize(int Width, int Height)
	{
		Width = std::max(Width, 1);
		Height = std::max(Height, 1);
		if(Width == m_Width && Height == m_Height)
			return;

		std::vector<CTile> vNew(static_cast<std::size_t>(Width) * Height);
		const int CopyW = std::min(Width, m_Width);
		const int CopyH = std::min(Height, m_Height);
		for(int y = 0; y < CopyH; y++)
			for(int x = 0; x < CopyW; x++)
				vNew[static_cast<std::size_t>(y) * Width + x] = m_vTiles[static_cast<std::size_t>(y) * m_Width + x];

		m_vTiles.swap(vNew);
		m_Width = Width;
		m_Height = Height;
	}

	// --------------------------------------------------------------------- map
	void CMap::Clear()
	{
		m_aAuthor.clear();
		m_aVersion.clear();
		m_aCredits.clear();
		m_aLicense.clear();
		m_vImages.clear();
		m_vSounds.clear();
		m_vEnvelopes.clear();
		m_vGroups.clear();
		m_vExtras.clear();
		m_vRawData.clear();
		m_SourceDatafileVersion = 0;
		m_SourceHasDdnetItems = false;
		m_SourcePath.clear();
	}

	base::view_transform GroupView(const base::view_transform &View, const CGroup &Group)
	{
		base::view_transform Result = View;
		Result.m_Center = base::vec2(
			View.m_Center.x * static_cast<float>(Group.m_ParallaxX) / 100.0f + static_cast<float>(Group.m_OffsetX),
			View.m_Center.y * static_cast<float>(Group.m_ParallaxY) / 100.0f + static_cast<float>(Group.m_OffsetY));
		return Result;
	}

	void CMap::InitNew(std::string_view Name)
	{
		Clear();
		m_aVersion = std::string(Name);

		// the reference editor starts a map with a game group and a tiles group
		CImage &Image = m_vImages.emplace_back();
		Image.m_Name = "generic_unhookable";
		Image.m_Width = 1024;
		Image.m_Height = 1024;
		Image.m_External = 1;

		CGroup &GameGroup = NewGroup();
		GameGroup.m_Name = "Game";
		CLayer &GameLayer = NewTileLayer(GameGroup, 50, 50, -1);
		GameLayer.m_Flags = TILESLAYERFLAG_GAME;
		GameLayer.m_Name = "Game";

		CGroup &TilesGroup = NewGroup();
		TilesGroup.m_Name = "Tiles";
		CLayer &Layer = NewTileLayer(TilesGroup, 50, 50, 0);
		Layer.m_Name = "Tile layer 1";

		LOG_INFO("map", "created a new map");
	}

	CImage *CMap::FindImage(std::string_view Name)
	{
		for(CImage &Image : m_vImages)
			if(Image.m_Name == Name)
				return &Image;
		return nullptr;
	}

	int CMap::FindImageIndex(std::string_view Name) const
	{
		for(std::size_t i = 0; i < m_vImages.size(); i++)
			if(m_vImages[i].m_Name == Name)
				return static_cast<int>(i);
		return -1;
	}

	CLayer *CMap::FindGameLayer()
	{
		for(CGroup &Group : m_vGroups)
			for(CLayer &Layer : Group.m_vLayers)
				if(Layer.IsGameLayer())
					return &Layer;
		return nullptr;
	}

	CGroup &CMap::NewGroup()
	{
		m_vGroups.emplace_back();
		return m_vGroups.back();
	}

	CLayer &CMap::NewTileLayer(CGroup &Group, int Width, int Height, int Image)
	{
		Group.m_vLayers.emplace_back();
		CLayer &Layer = Group.m_vLayers.back();
		Layer.m_Type = LAYERTYPE_TILES;
		Layer.m_Width = std::max(Width, 1);
		Layer.m_Height = std::max(Height, 1);
		Layer.m_Image = Image;
		Layer.m_vTiles.assign(static_cast<std::size_t>(Layer.m_Width) * Layer.m_Height, CTile{});
		return Layer;
	}

	CLayer &CMap::NewQuadLayer(CGroup &Group, int Image)
	{
		Group.m_vLayers.emplace_back();
		CLayer &Layer = Group.m_vLayers.back();
		Layer.m_Type = LAYERTYPE_QUADS;
		Layer.m_Image = Image;
		return Layer;
	}

	CLayer &CMap::NewSoundLayer(CGroup &Group, int Sound)
	{
		Group.m_vLayers.emplace_back();
		CLayer &Layer = Group.m_vLayers.back();
		Layer.m_Type = LAYERTYPE_SOUNDS;
		Layer.m_Sound = Sound;
		return Layer;
	}

	CMapStatistics CMap::Statistics() const
	{
		CMapStatistics Stats;
		Stats.m_Groups = static_cast<int>(m_vGroups.size());
		Stats.m_Images = static_cast<int>(m_vImages.size());
		Stats.m_Envelopes = static_cast<int>(m_vEnvelopes.size());
		Stats.m_Extras = static_cast<int>(m_vExtras.size());
		for(const CImage &Image : m_vImages)
			if(Image.IsEmbedded())
				Stats.m_EmbeddedImages++;
		for(const CGroup &Group : m_vGroups)
		{
			Stats.m_Layers += static_cast<int>(Group.m_vLayers.size());
			for(const CLayer &Layer : Group.m_vLayers)
			{
				Stats.m_Quads += static_cast<int>(Layer.m_vQuads.size());
				if(!Layer.IsTiles())
					continue;
				Stats.m_Tiles += static_cast<int>(Layer.TileCount());
				for(const CTile &Tile : Layer.m_vTiles)
					if(Tile.m_Index != 0)
						Stats.m_TilesWithContent++;
			}
		}
		return Stats;
	}

	std::vector<std::string> CMap::CompatibilityWarnings(map_target Target) const
	{
		const CMapTargetProfile &Profile = GetTargetProfile(Target);
		std::vector<std::string> Warnings;

		if(!Profile.m_DdnetTilemap)
		{
			for(const CGroup &Group : m_vGroups)
				for(const CLayer &Layer : Group.m_vLayers)
					if(Layer.IsPhysicsLayer())
					{
						Warnings.push_back("DDRace physics layers (tele, speedup, front, switch, tune) are dropped for " + std::string(Profile.m_pName));
						break;
					}
		}
		if(!Profile.m_HasSounds && !m_vSounds.empty())
			Warnings.push_back("embedded sounds are dropped for " + std::string(Profile.m_pName));
		if(!Profile.m_BezierEnvelopes)
		{
			for(const CEnvelope &Envelope : m_vEnvelopes)
				if(Envelope.UsesBezier())
				{
					Warnings.push_back("bezier envelope curves lose their tangents for " + std::string(Profile.m_pName));
					break;
				}
		}
		if(!Profile.m_KeepExtras && !m_vExtras.empty())
			Warnings.push_back("DDRace specific items (automapper and friends) are dropped for " + std::string(Profile.m_pName));
		return Warnings;
	}
} // namespace game
