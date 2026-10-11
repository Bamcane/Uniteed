#include "map.h"

#include <algorithm>
#include <cstring>

#include "base/logger.h"
#include "base/string.h"

namespace game
{
	namespace
	{
		/*
			The target matrix. The datafile container version and the map *item*
			versions are independent, which is why each row states both.

			`ddnet06` and `ddnet07` are the same format with one difference that
			matters to an editor: the 0.7 flavour may write run length compressed
			tile data (tilemap version 4), the way Teeworlds 0.7 does, while a
			0.6 DDRace map has to stay on version 3. Both keep the physics
			layers, the DDRace game entities and the UUID items, and both write
			the *Teeworlds* bezier envelope points rather than the separate
			`mapitemtype-envpoints-bezier@ddnet.tw` UUID item, because the
			tangents then live in the envelope points and every other target can
			read the same map.
		*/
		const CMapTargetProfile g_aProfiles[] = {
			{"06", "Teeworlds 0.6", "datafile 4, tilemap 3, image 1 (0.6.1/0.6.5)",
				4, 3, 1, 3, 2, 2, 20, ext_bezier_envelopes, false},
			{"07", "Teeworlds 0.7", "datafile 4, tilemap 4 with run length tiles, image 2",
				4, 4, 2, 3, 2, 2, 20, ext_sounds | ext_bezier_envelopes, true},
			{"ddnet06", "DDRaceNetwork 0.6", "datafile 4, tilemap 3 with the DDRace physics data indices",
				4, 3, 1, 3, 2, 2, 24, ext_physics | ext_game_entities | ext_sounds | ext_bezier_envelopes | ext_extras,
				false},
			{"ddnet07", "DDRaceNetwork 0.7", "datafile 4, DDRace physics data indices, run length tiles",
				4, 4, 2, 3, 2, 2, 24, ext_physics | ext_game_entities | ext_sounds | ext_bezier_envelopes | ext_extras,
				true},
			{"08", "Teeworlds 0.8", "datafile 5 with zstd, tilemap 4 with run length tiles",
				5, 4, 2, 3, 2, 2, 20, ext_sounds | ext_bezier_envelopes, true},
			// the package profile is a superset: it keeps the DDRace physics
			// layers and the UUID items, and it keeps the bezier tangents in the
			// envelope points. Datafile 5 is used when zstd is available because
			// empty tile runs compress very well even without run length coding.
			{"unitwp", "Uniteed package", "datafile 5, DDRace tilemaps, every item preserved",
				5, 3, 2, 3, 2, 2, 24, ext_all, false},
		};

		const CMapTargetProfile g_InvalidProfile{"?", "unknown", "unknown", 0, 0, 0, 0, 0, 0, 0, ext_none, false};
	} // namespace

	const CMapTargetProfile &GetTargetProfile(map_target Target)
	{
		switch(Target)
		{
		case map_target::tee06: return g_aProfiles[0];
		case map_target::tee07: return g_aProfiles[1];
		case map_target::ddnet06: return g_aProfiles[2];
		case map_target::ddnet07: return g_aProfiles[3];
		case map_target::tee08: return g_aProfiles[4];
		case map_target::unitwp: return g_aProfiles[5];
		}
		return g_InvalidProfile;
	}

	map_target GetPackageTarget() { return map_target::unitwp; }

	bool IsDdnetTarget(map_target Target)
	{
		return Target == map_target::ddnet06 || Target == map_target::ddnet07 || Target == map_target::unitwp;
	}

	std::optional<map_target> TargetFromId(std::string_view Id)
	{
		const std::string Lower = base::ToLower(std::string(Id));
		if(Lower == "06" || Lower == "0.6" || Lower == "teeworlds 0.6" || Lower == "tee06" || Lower == "6")
			return map_target::tee06;
		if(Lower == "07" || Lower == "0.7" || Lower == "teeworlds 0.7" || Lower == "tee07" || Lower == "7")
			return map_target::tee07;
		if(Lower == "08" || Lower == "0.8" || Lower == "teeworlds 0.8" || Lower == "tee08" || Lower == "8")
			return map_target::tee08;
		// the DDRace names: the bare one is the 0.6 flavour, which is what every
		// existing DDRace map is
		if(Lower == "ddnet" || Lower == "ddrace" || Lower == "ddracenetwork" ||
			Lower == "ddnet06" || Lower == "ddnet 0.6" || Lower == "ddnet0.6" || Lower == "ddrace 0.6")
			return map_target::ddnet06;
		if(Lower == "ddnet07" || Lower == "ddnet 0.7" || Lower == "ddnet0.7" || Lower == "ddrace 0.7")
			return map_target::ddnet07;
		if(Lower == "unitwp" || Lower == "package")
			return map_target::unitwp;
		return std::nullopt;
	}

	const std::array<map_target, 5> &AllTargets()
	{
		static const std::array<map_target, 5> s_aTargets = {
			map_target::tee06, map_target::tee07, map_target::ddnet06, map_target::ddnet07, map_target::tee08};
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

		/*
			The extensions switch is what decides whether a DDRace feature the
			target cannot express is a loss or a deliberate choice: with the
			switch on the layer is written and the warning says that only a
			DDRace client will act on it, with it off the export leaves the layer
			out and this list explains why.
		*/
		if(!Profile.HasExtension(ext_physics))
		{
			for(const CGroup &Group : m_vGroups)
				for(const CLayer &Layer : Group.m_vLayers)
					if(Layer.IsPhysicsLayer())
					{
						Warnings.push_back(m_bAllowExtensions ?
							std::string("DDRace physics layers (tele, speedup, front, switch, tune) are written for ") +
								Profile.m_pName + ", which does not define them: a vanilla client ignores them" :
							std::string("DDRace physics layers (tele, speedup, front, switch, tune) are dropped for ") +
								Profile.m_pName + " (turn on \"allow DDRace extensions\" to write them anyway)");
						break;
					}
		}
		if(!m_vSounds.empty() && !Profile.HasExtension(ext_sounds))
			Warnings.push_back("embedded sounds are dropped for " + std::string(Profile.m_pName));
		if(!Profile.HasExtension(ext_bezier_envelopes))
		{
			for(const CEnvelope &Envelope : m_vEnvelopes)
				if(Envelope.UsesBezier())
				{
					Warnings.push_back("bezier envelope curves lose their tangents for " + std::string(Profile.m_pName));
					break;
				}
		}
		if(!Profile.HasExtension(ext_extras) && !m_vExtras.empty())
			Warnings.push_back("DDRace specific items (automapper and friends) are dropped for " + std::string(Profile.m_pName));

		// The reverse case, worth saying out loud: the game layer carries DDRace
		// entities and the target is a vanilla one, so a client would render
		// them as the wrong thing unless the switch was a deliberate choice.
		if(!Profile.HasExtension(ext_game_entities))
		{
			bool bFound = false;
			for(const CGroup &Group : m_vGroups)
			{
				for(const CLayer &Layer : Group.m_vLayers)
				{
					if(!Layer.IsGameLayer())
						continue;
					for(const CTile &Tile : Layer.m_vTiles)
						if(Tile.m_Index >= ENTITY_OFFSET)
						{
							bFound = true;
							break;
						}
					if(bFound)
						break;
				}
				if(bFound)
					break;
			}
			if(bFound)
				Warnings.push_back(std::string("this map uses DDRace only game entities, which ") + Profile.m_pName +
					" does not define: they will show as unknown tiles");
		}
		return Warnings;
	}
} // namespace game
