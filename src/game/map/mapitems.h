#ifndef GAME_MAP_MAPITEMS_H
#define GAME_MAP_MAPITEMS_H

/*
	The on-disk item layouts of a Teeworlds map.

	A map is a CDataFile container holding items, and every item body is a
	sequence of little endian 32 bit ints. The layouts below are the union of

		Teeworlds 0.6 (tag 0.6-endofline, src/game/mapitems.h)
		Teeworlds 0.7.6 (src/game/mapitems.h)
		DDRaceNetwork 20.x (src/game/mapitems.h)
		Teeworlds Archive 0.8 (src/game/mapitems.h, datafile v5)

	The versions differ in a handful of places and the differences are the
	reason this editor has version profiles:

	  datafile   3 (0.5), 4 (0.6/0.7/ddnet, zlib), 5 (0.8, zstd)
	  tilemap    2 (no name), 3 (+ name, ddnet adds 5 physics data indices),
	             4 (same layout as 3, tile data run length compressed)
	  image      1 (no m_MustBe1), 2 (+ m_MustBe1)
	  group      1 (no clipping), 2 (+ clipping), 3 (+ name)
	  envelope   1 (no m_Synchronized), 2 (+ m_Synchronized),
	             3 (points carry bezier tangents)
	  points     24 bytes (CEnvPoint_v1) or 88 bytes (CEnvPoint, bezier)

	Every item body is read and written field by field in map_io.cpp instead of
	by casting a struct, because an old map stores a shorter body than the
	current struct (a version 1 image is 24 bytes where version 2 is 28).
	The structs here exist so the layouts are checkable with static_assert and
	so the code reads like the reference.
*/

#include <cstdint>

#include <sugar_types.h>

namespace game
{
	// ---------------------------------------------------------------- constants
	enum
	{
		LAYERTYPE_INVALID = 0,
		LAYERTYPE_GAME = 1, // unused since 0.6, the game layer is a tiles layer with a flag
		LAYERTYPE_TILES = 2,
		LAYERTYPE_QUADS = 3,

		// DDRaceNetwork physics layers, the numbers sit in the gap Teeworlds left
		LAYERTYPE_FRONT = 4,
		LAYERTYPE_TELE = 5,
		LAYERTYPE_SPEEDUP = 6,
		LAYERTYPE_SWITCH = 7,
		LAYERTYPE_TUNE = 8,
		LAYERTYPE_SOUNDS_DEPRECATED = 9, // does not exist, kept for the numbering
		LAYERTYPE_SOUNDS = 10,
	};

	enum
	{
		MAPITEMTYPE_VERSION = 0,
		MAPITEMTYPE_INFO = 1,
		MAPITEMTYPE_IMAGE = 2,
		MAPITEMTYPE_ENVELOPE = 3,
		MAPITEMTYPE_GROUP = 4,
		MAPITEMTYPE_LAYER = 5,
		MAPITEMTYPE_ENVPOINTS = 6,
		MAPITEMTYPE_SOUND = 7, // 0.7 and later
	};

	enum
	{
		CURVETYPE_STEP = 0,
		CURVETYPE_LINEAR = 1,
		CURVETYPE_SLOW = 2,
		CURVETYPE_FAST = 3,
		CURVETYPE_SMOOTH = 4,
		CURVETYPE_BEZIER = 5,
		NUM_CURVETYPES = 6,
	};

	// game layer entities, the vanilla set of every version
	enum
	{
		ENTITY_NULL = 0,
		ENTITY_SPAWN = 1,
		ENTITY_SPAWN_RED = 2,
		ENTITY_SPAWN_BLUE = 3,
		ENTITY_FLAGSTAND_RED = 4,
		ENTITY_FLAGSTAND_BLUE = 5,
		ENTITY_ARMOR_1 = 6,
		ENTITY_HEALTH_1 = 7,
		ENTITY_WEAPON_SHOTGUN = 8,
		ENTITY_WEAPON_GRENADE = 9,
		ENTITY_POWERUP_NINJA = 10,
		ENTITY_WEAPON_LASER = 11,
		NUM_VANILLA_ENTITIES = 12,

		// DDRaceNetwork entities, written into the same game layer
		ENTITY_LASER_FAST_CCW = 12,
		ENTITY_LASER_NORMAL_CCW = 13,
		ENTITY_LASER_SLOW_CCW = 14,
		ENTITY_LASER_STOP = 15,
		ENTITY_LASER_SLOW_CW = 16,
		ENTITY_LASER_NORMAL_CW = 17,
		ENTITY_LASER_FAST_CW = 18,
		ENTITY_LASER_SHORT = 19,
		ENTITY_LASER_MEDIUM = 20,
		ENTITY_LASER_LONG = 21,
		ENTITY_LASER_C_SLOW = 22,
		ENTITY_LASER_C_NORMAL = 23,
		ENTITY_LASER_C_FAST = 24,
		ENTITY_LASER_O_SLOW = 25,
		ENTITY_LASER_O_NORMAL = 26,
		ENTITY_LASER_O_FAST = 27,
		ENTITY_PLASMAE = 29,
		ENTITY_PLASMAF = 30,
		ENTITY_PLASMA = 31,
		ENTITY_PLASMAU = 32,
		ENTITY_CRAZY_SHOTGUN_EX = 33,
		ENTITY_CRAZY_SHOTGUN = 34,
		ENTITY_ARMOR_SHOTGUN = 35,
		ENTITY_ARMOR_GRENADE = 36,
		ENTITY_ARMOR_NINJA = 37,
		ENTITY_ARMOR_LASER = 38,
		ENTITY_DRAGGER_WEAK = 42,
		ENTITY_DRAGGER_NORMAL = 43,
		ENTITY_DRAGGER_STRONG = 44,
		ENTITY_DRAGGER_WEAK_NW = 45,
		ENTITY_DRAGGER_NORMAL_NW = 46,
		ENTITY_DRAGGER_STRONG_NW = 47,
		ENTITY_DOOR = 49,

		ENTITY_OFFSET = 255 - 16 * 4,
	};

	enum
	{
		TILE_AIR = 0,
		TILE_HOOKABLE = 1,
		TILE_DEATH = 2,
		TILE_UNHOOKABLE = 3,
	};

	enum
	{
		TILEFLAG_XFLIP = 1 << 0,
		TILEFLAG_VFLIP = 1 << 1, // TILEFLAG_YFLIP in DDRaceNetwork
		TILEFLAG_YFLIP = 1 << 1,
		TILEFLAG_HFLIP = 1 << 0,
		TILEFLAG_OPAQUE = 1 << 2,
		TILEFLAG_ROTATE = 1 << 3,
	};

	enum
	{
		LAYERFLAG_DETAIL = 1,
		TILESLAYERFLAG_GAME = 1 << 0,
		TILESLAYERFLAG_TELE = 1 << 1,
		TILESLAYERFLAG_SPEEDUP = 1 << 2,
		TILESLAYERFLAG_FRONT = 1 << 3,
		TILESLAYERFLAG_SWITCH = 1 << 4,
		TILESLAYERFLAG_TUNE = 1 << 5,

		// every flag that marks a DDRaceNetwork physics layer
		TILESLAYERFLAG_PHYSICS = TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP | TILESLAYERFLAG_FRONT | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE,
	};

	enum
	{
		MAX_MAPIMAGES = 64,
		MAX_MAPSOUNDS = 64,
	};

	// coordinates, colors and envelope values are 22.10 fixed point
	inline constexpr int FixedPointScale = 1 << 10;
	inline float FixedToFloat(int Value) { return Value / static_cast<float>(FixedPointScale); }
	inline int FloatToFixed(float Value) { return static_cast<int>(Value * FixedPointScale); }

	// ------------------------------------------------------------- item bodies
	struct CPoint
	{
		int x = 0;
		int y = 0;
	};

	struct CColor
	{
		int r = 0;
		int g = 0;
		int b = 0;
		int a = 255;
	};

	/*
		A quad stores its four corners as a freeform patch, not as a perimeter
		walk: 0 and 1 are the top edge, 2 and 3 the bottom one, and the colours
		and texture coordinates are indexed the same way. Drawing them in
		stored order would cross the shape into a bowtie, so anything that walks
		the outline has to go 0 -> 1 -> 3 -> 2.
	*/
	struct CQuad
	{
		CPoint m_aPoints[5];
		CColor m_aColors[4];
		CPoint m_aTexcoords[4];

		int m_PosEnv = -1;
		int m_PosEnvOffset = 0;

		int m_ColorEnv = -1;
		int m_ColorEnvOffset = 0;

		// the corners in the order they must be walked to trace the outline
		static constexpr int s_aPerimeter[4] = {0, 1, 3, 2};
		// the corner indices of the four (u,v) positions: (0,0) (1,0) (0,1) (1,1)
		static constexpr int s_aCornerAt[2][2] = {{0, 2}, {1, 3}};
	};

	struct CTile
	{
		std::uint8_t m_Index = 0;
		std::uint8_t m_Flags = 0;
		std::uint8_t m_Skip = 0;
		std::uint8_t m_Reserved = 0;
	};

	struct CMapItemVersion
	{
		enum
		{
			CURRENT_VERSION = 1
		};
		int m_Version = 1;
	};

	struct CMapItemInfo
	{
		enum
		{
			CURRENT_VERSION = 1
		};
		int m_Version = 1;
		int m_Author = -1;
		int m_MapVersion = -1;
		int m_Credits = -1;
		int m_License = -1;
	};

	// DDRaceNetwork extends the info item with server settings
	struct CMapItemInfoSettings : CMapItemInfo
	{
		int m_Settings = -1;
	};

	struct CMapItemImage_v1
	{
		enum
		{
			CURRENT_VERSION = 1
		};
		int m_Version = 1;
		int m_Width = 0;
		int m_Height = 0;
		int m_External = 0;
		int m_ImageName = -1;
		int m_ImageData = -1;
	};

	struct CMapItemImage : CMapItemImage_v1
	{
		enum
		{
			CURRENT_VERSION = 2
		};
		int m_MustBe1 = 1;
	};

	struct CMapItemGroup_v1
	{
		int m_Version = 1;
		int m_OffsetX = 0;
		int m_OffsetY = 0;
		int m_ParallaxX = 100;
		int m_ParallaxY = 100;

		int m_StartLayer = 0;
		int m_NumLayers = 0;
	};

	struct CMapItemGroup_v2 : CMapItemGroup_v1
	{
		enum
		{
			CURRENT_VERSION = 2
		};
		int m_UseClipping = 0;
		int m_ClipX = 0;
		int m_ClipY = 0;
		int m_ClipW = 0;
		int m_ClipH = 0;
	};

	struct CMapItemGroup : CMapItemGroup_v2
	{
		enum
		{
			CURRENT_VERSION = 3
		};
		int m_aName[3]{};
	};

	struct CMapItemLayer
	{
		int m_Version = 0;
		int m_Type = LAYERTYPE_INVALID;
		int m_Flags = 0;
	};

	// Teeworlds version 2 tiles layer, the earliest supported one
	struct CMapItemLayerTilemap_v2
	{
		CMapItemLayer m_Layer;
		int m_Version = 2;

		int m_Width = 0;
		int m_Height = 0;
		int m_Flags = 0;

		CColor m_Color;
		int m_ColorEnv = -1;
		int m_ColorEnvOffset = 0;

		int m_Image = -1;
		int m_Data = -1;
	};

	// DDRaceNetwork added its physics data indices to the version 2 item
	struct CMapItemLayerTilemap_v2Legacy : CMapItemLayerTilemap_v2
	{
		int m_Tele = -1;
		int m_Speedup = -1;
		int m_Front = -1;
		int m_Switch = -1;
		int m_Tune = -1;
	};

	// version 3 added the layer name
	struct CMapItemLayerTilemap_v3 : CMapItemLayerTilemap_v2
	{
		enum
		{
			CURRENT_VERSION = 3
		};
		int m_aName[3]{};
	};

	// version 4 keeps the layout and compresses the tile data with run lengths
	struct CMapItemLayerTilemap_v4 : CMapItemLayerTilemap_v3
	{
		enum
		{
			CURRENT_VERSION = 4
		};
	};

	// DDRaceNetwork added the physics data indices to the version 3 item
	struct CMapItemLayerTilemap : CMapItemLayerTilemap_v3
	{
		int m_Tele = -1;
		int m_Speedup = -1;
		int m_Front = -1;
		int m_Switch = -1;
		int m_Tune = -1;
	};

	struct CMapItemLayerQuads
	{
		enum
		{
			CURRENT_VERSION = 2
		};
		CMapItemLayer m_Layer;
		int m_Version = 2;

		int m_NumQuads = 0;
		int m_Data = -1;
		int m_Image = -1;

		int m_aName[3]{};
	};

	struct CEnvPoint_v1
	{
		int m_Time = 0; // in ms
		int m_Curvetype = CURVETYPE_LINEAR;
		int m_aValues[4]{}; // 1-4 channels, 22.10 fixed point
	};

	struct CEnvPointBezier
	{
		int m_aInTangentdx[4]{};
		int m_aInTangentdy[4]{};
		int m_aOutTangentdx[4]{};
		int m_aOutTangentdy[4]{};
	};

	// upstream Teeworlds packs the bezier tangents into the envelope points
	struct CEnvPoint : CEnvPoint_v1
	{
		CEnvPointBezier m_Bezier;
	};

	struct CMapItemEnvelope_v1
	{
		enum
		{
			VERSION_TEEWORLDS_BEZIER = 3
		};
		int m_Version = 1;
		int m_Channels = 0;
		int m_StartPoint = 0;
		int m_NumPoints = 0;
		int m_aName[8]{};
	};

	struct CMapItemEnvelope_v2 : CMapItemEnvelope_v1
	{
		enum
		{
			CURRENT_VERSION = 2
		};
		int m_Synchronized = 0;
	};

	struct CMapItemEnvelope : CMapItemEnvelope_v2
	{
		enum
		{
			CURRENT_VERSION = 3
		};
	};

	// ------------------------------------------------------------------ sounds
	struct CSoundShape
	{
		enum
		{
			SHAPE_RECTANGLE = 0,
			SHAPE_CIRCLE = 1,
			NUM_SHAPES = 2,
		};

		int m_Type = SHAPE_RECTANGLE;
		int m_Width = 0; // rectangle, 22.10 fixed point
		int m_Height = 0;
	};

	struct CSoundSource
	{
		CPoint m_Position;
		int m_Loop = 0;
		int m_Pan = 0;
		int m_TimeDelay = 0;
		int m_Falloff = 0;

		int m_PosEnv = -1;
		int m_PosEnvOffset = 0;
		int m_SoundEnv = -1;
		int m_SoundEnvOffset = 0;

		CSoundShape m_Shape;
	};

	struct CMapItemLayerSounds
	{
		enum
		{
			CURRENT_VERSION = 2
		};
		CMapItemLayer m_Layer;
		int m_Version = 2;

		int m_NumSources = 0;
		int m_Data = -1;
		int m_Sound = -1;

		int m_aName[3]{};
	};

	struct CMapItemSound
	{
		enum
		{
			CURRENT_VERSION = 1
		};
		int m_Version = 1;
		int m_External = 0;
		int m_SoundName = -1;
		int m_SoundData = -1;
		int m_SoundDataSize = 0;
	};

	// ------------------------------------------------- DDRaceNetwork tile types
	struct CTeleTile
	{
		std::uint8_t m_Number = 0;
		std::uint8_t m_Type = 0;
	};

	struct CSpeedupTile
	{
		std::uint8_t m_Force = 0;
		std::uint8_t m_MaxSpeed = 0;
		std::uint8_t m_Type = 0;
		std::uint8_t m_MustBe0 = 0;
		std::int16_t m_Angle = 0;
	};

	struct CSwitchTile
	{
		std::uint8_t m_Number = 0;
		std::uint8_t m_Type = 0;
		std::uint8_t m_Flags = 0;
		std::uint8_t m_Delay = 0;
	};

	struct CDoorTile
	{
		std::uint8_t m_Index = 0;
		std::uint8_t m_Flags = 0;
		std::uint8_t m_Number = 0;
	};

	struct CTuneTile
	{
		std::uint8_t m_Number = 0;
		std::uint8_t m_Type = 0;
	};

	// -------------------------------------------------------------- size checks
	// the item bodies are cast from the raw map data in the reference
	// implementations, so the base classes must not add padding
	static_assert(sizeof(CPoint) == 8);
	static_assert(sizeof(CColor) == 16);
	static_assert(sizeof(CQuad) == 152);
	static_assert(sizeof(CTile) == 4);
	static_assert(sizeof(CMapItemVersion) == 4);
	static_assert(sizeof(CMapItemInfo) == 20);
	static_assert(sizeof(CMapItemInfoSettings) == 24);
	static_assert(sizeof(CMapItemImage_v1) == 24);
	static_assert(sizeof(CMapItemImage) == 28);
	static_assert(sizeof(CMapItemGroup_v1) == 28);
	static_assert(sizeof(CMapItemGroup_v2) == 48);
	static_assert(sizeof(CMapItemGroup) == 60);
	static_assert(sizeof(CMapItemLayer) == 12);
	static_assert(sizeof(CMapItemLayerTilemap_v2) == 60);
	static_assert(sizeof(CMapItemLayerTilemap_v3) == 72);
	static_assert(sizeof(CMapItemLayerTilemap_v4) == 72);
	static_assert(sizeof(CMapItemLayerTilemap_v2Legacy) == 80);
	static_assert(sizeof(CMapItemLayerTilemap) == 92);
	static_assert(sizeof(CMapItemLayerQuads) == 40);
	static_assert(sizeof(CEnvPoint_v1) == 24);
	static_assert(sizeof(CEnvPointBezier) == 64);
	static_assert(sizeof(CEnvPoint) == 88);
	static_assert(sizeof(CMapItemEnvelope_v1) == 48);
	static_assert(sizeof(CMapItemEnvelope_v2) == 52);
	static_assert(sizeof(CMapItemEnvelope) == 52);
	static_assert(sizeof(CMapItemLayerSounds) == 40);
	static_assert(sizeof(CMapItemSound) == 20);
	static_assert(sizeof(CSoundShape) == 12);
	static_assert(sizeof(CSoundSource) == 52);
	static_assert(sizeof(CTeleTile) == 2);
	static_assert(sizeof(CSpeedupTile) == 6);
	static_assert(sizeof(CSwitchTile) == 4);
	static_assert(sizeof(CDoorTile) == 3);
	static_assert(sizeof(CTuneTile) == 2);
} // namespace game

#endif // GAME_MAP_MAPITEMS_H
