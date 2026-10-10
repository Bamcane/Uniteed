#ifndef GAME_MAP_MAP_H
#define GAME_MAP_MAP_H

/*
	The editable map model.

	Loading a map of any version normalises it into this model: tiles are
	always a plain array of CTile without run length skips, images always carry
	their raw RGBA bytes, envelope points always carry their bezier tangents.
	Saving re-applies the rules of the chosen target version.

	Items this editor does not model (the DDRaceNetwork UUID items such as the
	automapper configuration, or any future type) are preserved as raw bodies.
	They reference the data blob table by index, and preserving them means
	preserving those indices: the loader therefore keeps every original data
	block and the writer re-adds them, in order, before anything new. That is
	why a DDRaceNetwork map survives a load and save round trip.
*/

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sugar_types.h>

#include "base/storage.h"
#include "game/map/datafile.h"
#include "game/map/mapitems.h"

namespace game
{
	using byte_vector = storage::byte_vector;

	// ------------------------------------------------------------ target matrix
	enum class map_target
	{
		tee06, // Teeworlds 0.6.1 and 0.6.5, datafile 4
		tee07, // Teeworlds 0.7.x, datafile 4, run length tiles, bezier envelopes
		ddnet, // DDRaceNetwork 0.6 protocol, datafile 4, physics layers, UUID items
		tee08, // Teeworlds Archive 0.8, datafile 5 with zstd
		unitwp, // the .unitwp package profile: superset that keeps everything
	};

	struct CMapTargetProfile
	{
		const char *m_pId;    // "06", "07", "ddnet", "08"
		const char *m_pName;  // human readable
		const char *m_pDescription;
		int m_DatafileVersion = 4;
		int m_TilemapVersion = 3;
		int m_ImageVersion = 1;
		int m_GroupVersion = 3;
		int m_QuadsVersion = 2;
		int m_EnvelopeVersion = 2;
		int m_InfoSize = 20;
		bool m_DdnetTilemap = false;  // 92 byte tilemaps with the physics data indices
		bool m_RunLengthTiles = false; // tilemap version >= 4
		bool m_HasSounds = false;      // MAPITEMTYPE_SOUND exists
		bool m_BezierEnvelopes = false; // 88 byte points with tangents
		bool m_KeepExtras = false;      // UUID items may be written
	};

	const CMapTargetProfile &GetTargetProfile(map_target Target);
	// accepts "06", "0.6", "07", "0.7", "08", "0.8", "ddnet" and "unitwp"
	std::optional<map_target> TargetFromId(std::string_view Id);
	// the four export targets, without the package profile
	const std::array<map_target, 4> &AllTargets();
	// the profile a .unitwp package stores its map with, a superset of every
	// target so that a package never loses data
	map_target GetPackageTarget();

	// --------------------------------------------------------------- map objects
	struct CImage
	{
		int m_Width = 0;
		int m_Height = 0;
		int m_External = 1;
		std::string m_Name;
		byte_vector m_Data; // raw RGBA (width * height * 4) when embedded
		std::array<std::uint8_t, 256> m_aTileFlags{};
		bool m_aTileFlagsValid = false;

		bool IsEmbedded() const { return m_External == 0; }
	};

	struct CSound
	{
		std::string m_Name;
		int m_External = 0;
		byte_vector m_Data; // raw opus when embedded
	};

	struct CEnvelopePoint
	{
		int m_Time = 0;
		int m_Curvetype = CURVETYPE_LINEAR;
		int m_aValues[4]{};
		CEnvPointBezier m_Bezier;
	};

	struct CEnvelope
	{
		int m_Channels = 4;
		int m_Synchronized = 0;
		std::string m_Name;
		std::vector<CEnvelopePoint> m_vPoints;

		bool UsesBezier() const
		{
			for(const CEnvelopePoint &Point : m_vPoints)
				if(Point.m_Curvetype == CURVETYPE_BEZIER)
					return true;
			return false;
		}
	};

	struct CLayer
	{
		int m_Type = LAYERTYPE_INVALID;
		int m_LayerFlags = 0; // LAYERFLAG_DETAIL
		int m_Flags = 0;      // TILESLAYERFLAG_*
		std::string m_Name;

		// tiles layers
		int m_Width = 0;
		int m_Height = 0;
		CColor m_Color;
		int m_ColorEnv = -1;
		int m_ColorEnvOffset = 0;
		int m_Image = -1;
		std::vector<CTile> m_vTiles; // always plain, m_Skip is recomputed on save
		// DDRaceNetwork physics payloads, raw, one element per tile
		byte_vector m_vTele;
		byte_vector m_vSpeedup;
		byte_vector m_vFront;
		byte_vector m_vSwitch;
		byte_vector m_vTune;

		// quads layers
		std::vector<CQuad> m_vQuads;

		// sounds layers
		int m_Sound = -1;
		std::vector<CSoundSource> m_vSources;

		// what the source map used, so the editor can warn about a downgrade
		int m_SourceTilemapVersion = 0;
		bool m_SourceHadPhysics = false;

		bool IsTiles() const { return m_Type == LAYERTYPE_TILES; }
		bool IsQuads() const { return m_Type == LAYERTYPE_QUADS; }
		bool IsSounds() const { return m_Type == LAYERTYPE_SOUNDS || m_Type == LAYERTYPE_SOUNDS_DEPRECATED; }
		bool IsGameLayer() const { return IsTiles() && (m_Flags & TILESLAYERFLAG_GAME) != 0; }
		bool IsPhysicsLayer() const { return IsTiles() && (m_Flags & TILESLAYERFLAG_PHYSICS) != 0; }
		bool IsDetailLayer() const { return (m_LayerFlags & LAYERFLAG_DETAIL) != 0; }

		CTile *Tile(int x, int y);
		const CTile *Tile(int x, int y) const;
		bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < m_Width && y < m_Height; }
		void Resize(int Width, int Height);
		std::size_t TileCount() const { return static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height); }
	};

	struct CGroup
	{
		int m_OffsetX = 0;
		int m_OffsetY = 0;
		int m_ParallaxX = 100;
		int m_ParallaxY = 100;
		int m_UseClipping = 0;
		int m_ClipX = 0;
		int m_ClipY = 0;
		int m_ClipW = 0;
		int m_ClipH = 0;
		std::string m_Name;
		std::vector<CLayer> m_vLayers;

		int m_SourceVersion = 0;
	};

	// an item the editor does not model, kept verbatim
	struct CExtraItem
	{
		int m_Type = 0;
		int m_ID = 0;
		byte_vector m_Data;
	};

	struct CMapStatistics
	{
		int m_Groups = 0;
		int m_Layers = 0;
		int m_Tiles = 0;
		int m_Images = 0;
		int m_EmbeddedImages = 0;
		int m_Envelopes = 0;
		int m_Quads = 0;
		int m_Extras = 0;
		int m_TilesWithContent = 0;
	};

	class CMap
	{
	public:
		// map info
		std::string m_aAuthor;
		std::string m_aVersion;
		std::string m_aCredits;
		std::string m_aLicense;

		std::vector<CImage> m_vImages;
		std::vector<CSound> m_vSounds;
		std::vector<CEnvelope> m_vEnvelopes;
		std::vector<CGroup> m_vGroups;
		std::vector<CExtraItem> m_vExtras;

		// every data block of the source file, re-added on save so that the
		// preserved extra items keep pointing at the right block
		std::vector<byte_vector> m_vRawData;

		// where the model came from
		int m_SourceDatafileVersion = 0;
		bool m_SourceHasDdnetItems = false;
		std::string m_SourcePath;

		void Clear();

		// a blank map: the standard two groups of the reference editor
		void InitNew(std::string_view Name = "unnamed");

		bool LoadFromDataFile(const CDataFileReader &File, std::string *pError = nullptr);
		bool LoadFromBytes(const byte_vector &Bytes, std::string *pError = nullptr);
		bool LoadFromFile(const storage::path &FileName, std::string *pError = nullptr);

		bool SaveToBytes(byte_vector &Out, map_target Target, std::string *pError = nullptr) const;
		bool SaveToFile(const storage::path &FileName, map_target Target, std::string *pError = nullptr) const;

		CImage *FindImage(std::string_view Name);
		int FindImageIndex(std::string_view Name) const;
		CLayer *FindGameLayer();

		CGroup &NewGroup();
		CLayer &NewTileLayer(CGroup &Group, int Width, int Height, int Image = -1);
		CLayer &NewQuadLayer(CGroup &Group, int Image = -1);
		CLayer &NewSoundLayer(CGroup &Group, int Sound = -1);

		CMapStatistics Statistics() const;

		// true when the model contains something the target cannot express
		std::vector<std::string> CompatibilityWarnings(map_target Target) const;
	};
} // namespace game

#endif // GAME_MAP_MAP_H
