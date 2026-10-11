/*
	Loading and saving a map, with all the version differences in one place.

	The rules implemented here come from the reference implementations:

	  teeworlds 0.6  src/game/editor/io.cpp of tag 0.6-endofline
	  teeworlds 0.7  src/game/editor/io.cpp of tag 0.7.6
	  ddnet          src/game/editor/mapitems/map_io.cpp
	  archive 0.8    src/engine/shared/datafile.cpp (datafile version 5)

	Loading normalises everything into the model of map.h. Saving re-applies
	the conventions of the selected target profile.
*/

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include "base/endian.h"
#include "base/logger.h"
#include "base/string.h"
#include "game/map/map.h"

namespace game
{
	namespace
	{
		// a bounds checked view of an item body: an old map stores a shorter body
		// than the current struct, so every field is checked before it is read
		struct CItemView
		{
			const std::byte *m_pData = nullptr;
			int m_Size = 0;

			bool Valid() const { return m_pData != nullptr; }
			bool Has(int Offset, int Bytes) const { return m_pData != nullptr && Offset >= 0 && Offset + Bytes <= m_Size; }
			int Int(int Offset, int Default = 0) const { return Has(Offset, 4) ? base::ReadInt32(m_pData + Offset) : Default; }
		};

		std::string ReadString(const CDataFileReader &File, int Index)
		{
			if(Index < 0)
				return {};
			std::size_t Size = 0;
			const std::byte *pData = File.GetData(Index, &Size);
			if(pData == nullptr || Size == 0)
				return {};
			const char *pString = reinterpret_cast<const char *>(pData);
			std::size_t Length = 0;
			while(Length < Size && pString[Length] != '\0')
				Length++;
			return std::string(pString, Length);
		}

		byte_vector CopyBlock(const CDataFileReader &File, int Index)
		{
			if(Index < 0)
				return {};
			std::size_t Size = 0;
			const std::byte *pData = File.GetData(Index, &Size);
			if(pData == nullptr)
				return {};
			return byte_vector(pData, pData + Size);
		}

		// the run length decoding of tilemap version 4 and later
		bool ExpandTiles(const byte_vector &Stored, int Count, std::vector<CTile> &Out)
		{
			Out.assign(static_cast<std::size_t>(Count), CTile{});
			if(Stored.size() % sizeof(CTile) != 0)
				return false;

			const CTile *pStored = reinterpret_cast<const CTile *>(Stored.data());
			const std::size_t StoredTiles = Stored.size() / sizeof(CTile);
			std::size_t StoredIndex = 0;
			for(int i = 0; i < Count;)
			{
				if(StoredIndex >= StoredTiles)
					return false;
				CTile Tile = pStored[StoredIndex++];
				const unsigned RunLength = static_cast<unsigned>(Tile.m_Skip) + 1u;
				Tile.m_Skip = 0;
				Tile.m_Reserved = 0;
				for(unsigned Step = 0; Step < RunLength && i < Count; Step++)
					Out[static_cast<std::size_t>(i++)] = Tile;
			}
			return true;
		}

		void ExpandPlainTiles(const byte_vector &Stored, int Count, std::vector<CTile> &Out)
		{
			Out.assign(static_cast<std::size_t>(Count), CTile{});
			const CTile *pStored = reinterpret_cast<const CTile *>(Stored.data());
			const std::size_t StoredTiles = Stored.size() / sizeof(CTile);
			const std::size_t Copy = std::min<std::size_t>(StoredTiles, static_cast<std::size_t>(Count));
			for(std::size_t i = 0; i < Copy; i++)
			{
				Out[i] = pStored[i];
				Out[i].m_Skip = 0;
				Out[i].m_Reserved = 0;
			}
		}

		// the run length encoding of tilemap version 4 and later: a run compares
		// the index and the flags and is at most 256 tiles long
		byte_vector CompressTiles(const std::vector<CTile> &Tiles)
		{
			byte_vector Out;
			Out.reserve(Tiles.size() * sizeof(CTile));
			std::size_t i = 0;
			while(i < Tiles.size())
			{
				CTile Run = Tiles[i];
				Run.m_Skip = 0;
				Run.m_Reserved = 0;
				unsigned Length = 1;
				while(Length < 256 && i + Length < Tiles.size() &&
					Tiles[i + Length].m_Index == Run.m_Index &&
					Tiles[i + Length].m_Flags == Run.m_Flags)
					Length++;
				Run.m_Skip = static_cast<std::uint8_t>(Length - 1);
				const std::byte *pBytes = reinterpret_cast<const std::byte *>(&Run);
				Out.insert(Out.end(), pBytes, pBytes + sizeof(CTile));
				i += Length;
			}
			return Out;
		}

		byte_vector PlainTiles(const std::vector<CTile> &Tiles)
		{
			byte_vector Out(Tiles.size() * sizeof(CTile));
			std::memcpy(Out.data(), Tiles.data(), Out.size());
			return Out;
		}

		// the physics payload of a DDRace layer, element size per layer type
		int PhysicsElementSize(int Flags)
		{
			if(Flags & TILESLAYERFLAG_TELE)
				return sizeof(CTeleTile);
			if(Flags & TILESLAYERFLAG_SPEEDUP)
				return sizeof(CSpeedupTile);
			if(Flags & TILESLAYERFLAG_FRONT)
				return sizeof(CTile);
			if(Flags & TILESLAYERFLAG_SWITCH)
				return sizeof(CSwitchTile);
			if(Flags & TILESLAYERFLAG_TUNE)
				return sizeof(CTuneTile);
			return 0;
		}

		// DDRace keeps the visible tile index in the physics payload, the editor
		// mirrors it so the layer can be drawn
		void MirrorPhysicsTiles(const byte_vector &Payload, int Flags, std::vector<CTile> &Tiles)
		{
			const int ElementSize = PhysicsElementSize(Flags);
			if(ElementSize <= 0)
				return;
			const std::size_t Count = std::min(Tiles.size(), Payload.size() / static_cast<std::size_t>(ElementSize));
			for(std::size_t i = 0; i < Count; i++)
			{
				std::uint8_t Type = 0;
				switch(Flags)
				{
				case TILESLAYERFLAG_TELE: Type = std::to_integer<std::uint8_t>(Payload[i * ElementSize + 1]); break;
				case TILESLAYERFLAG_SPEEDUP: Type = std::to_integer<std::uint8_t>(Payload[i * ElementSize + 2]); break;
				case TILESLAYERFLAG_FRONT: Type = std::to_integer<std::uint8_t>(Payload[i * ElementSize + 0]); break;
				case TILESLAYERFLAG_SWITCH: Type = std::to_integer<std::uint8_t>(Payload[i * ElementSize + 1]); break;
				case TILESLAYERFLAG_TUNE: Type = std::to_integer<std::uint8_t>(Payload[i * ElementSize + 1]); break;
				default: break;
				}
				Tiles[i].m_Index = Type;
			}
		}
	} // namespace

	// --------------------------------------------------------------------- read
	bool CMap::LoadFromDataFile(const CDataFileReader &File, std::string *pError)
	{
		const auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = Message;
			LOG_ERROR("map", "%s", Message.c_str());
			return false;
		};

		if(!File.IsOpen())
			return Fail("the map file is not open");

		Clear();
		m_SourceDatafileVersion = File.Version();

		int Start = 0, Num = 0;

		// the version item decides whether this is a map at all
		if(!File.GetType(MAPITEMTYPE_VERSION, &Start, &Num) || Num < 1)
			return Fail("the file has no map version item");
		const CItemView VersionItem{File.GetItem(Start), File.GetItemSize(Start)};
		if(VersionItem.Int(0, -1) != CMapItemVersion::CURRENT_VERSION)
			return Fail("unsupported map version " + std::to_string(VersionItem.Int(0, -1)));

		// keep the raw data blocks if there are items we do not understand
		bool HasExtras = false;
		for(int i = 0; i < File.NumItems(); i++)
		{
			int Type = 0;
			File.GetItem(i, &Type, nullptr);
			if(Type != MAPITEMTYPE_VERSION && Type != MAPITEMTYPE_INFO && Type != MAPITEMTYPE_IMAGE &&
				Type != MAPITEMTYPE_ENVELOPE && Type != MAPITEMTYPE_GROUP && Type != MAPITEMTYPE_LAYER &&
				Type != MAPITEMTYPE_ENVPOINTS && Type != MAPITEMTYPE_SOUND)
			{
				HasExtras = true;
				m_SourceHasDdnetItems = true;
			}
		}

		// -------------------------------------------------------------- info
		if(File.GetType(MAPITEMTYPE_INFO, &Start, &Num) && Num >= 1)
		{
			const CItemView Info{File.GetItem(Start), File.GetItemSize(Start)};
			m_aAuthor = ReadString(File, Info.Int(4, -1));
			m_aVersion = ReadString(File, Info.Int(8, -1));
			m_aCredits = ReadString(File, Info.Int(12, -1));
			m_aLicense = ReadString(File, Info.Int(16, -1));
		}

		// ------------------------------------------------------------ images
		if(File.GetType(MAPITEMTYPE_IMAGE, &Start, &Num))
		{
			for(int i = 0; i < Num; i++)
			{
				const int Index = Start + i;
				const CItemView Item{File.GetItem(Index), File.GetItemSize(Index)};
				if(!Item.Valid())
					continue;

				CImage Image;
				Image.m_Width = Item.Int(4);
				Image.m_Height = Item.Int(8);
				Image.m_External = Item.Int(12, 1);
				const int NameIndex = Item.Int(16, -1);
				const int DataIndex = Item.Int(20, -1);
				Image.m_Name = ReadString(File, NameIndex);

				if(Image.m_External == 0 && DataIndex >= 0)
				{
					Image.m_Data = CopyBlock(File, DataIndex);
					const std::size_t Expected = static_cast<std::size_t>(std::max(Image.m_Width, 0)) * std::max(Image.m_Height, 0) * 4;
					if(Expected != 0 && Image.m_Data.size() != Expected)
						LOG_WARN("map", "image '%s' has %zu bytes, expected %zu", Image.m_Name.c_str(), Image.m_Data.size(), Expected);
				}
				if(Image.m_Name.empty())
					Image.m_Name = "image " + std::to_string(i);
				m_vImages.push_back(std::move(Image));
			}
		}

		// ------------------------------------------------------------ sounds
		if(File.GetType(MAPITEMTYPE_SOUND, &Start, &Num))
		{
			for(int i = 0; i < Num; i++)
			{
				const int Index = Start + i;
				const CItemView Item{File.GetItem(Index), File.GetItemSize(Index)};
				if(!Item.Valid())
					continue;
				CSound Sound;
				Sound.m_External = Item.Int(4, 0);
				Sound.m_Name = ReadString(File, Item.Int(8, -1));
				const int DataIndex = Item.Int(12, -1);
				if(DataIndex >= 0)
					Sound.m_Data = CopyBlock(File, DataIndex);
				m_vSounds.push_back(std::move(Sound));
			}
		}

		// --------------------------------------------------------- envelopes
		{
			bool BezierPoints = false;
			int EnvelopeStart = 0, EnvelopeNum = 0;
			if(File.GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum) && EnvelopeNum > 0)
			{
				for(int i = 0; i < EnvelopeNum; i++)
				{
					const int Index = EnvelopeStart + i;
					const CItemView Item{File.GetItem(Index), File.GetItemSize(Index)};
					if(Item.Int(0, 1) >= CMapItemEnvelope_v1::VERSION_TEEWORLDS_BEZIER)
						BezierPoints = true;
				}
			}

			// the points of every envelope live in the single ENVPOINTS item
			std::vector<CEnvPoint> Points;
			int PointsStart = 0, PointsNum = 0;
			if(File.GetType(MAPITEMTYPE_ENVPOINTS, &PointsStart, &PointsNum) && PointsNum >= 1)
			{
				const int Index = PointsStart;
				const std::byte *pBody = File.GetItem(Index);
				const int BodySize = File.GetItemSize(Index);
				if(pBody != nullptr && BodySize > 0)
				{
					const int PointSize = BezierPoints ? static_cast<int>(sizeof(CEnvPoint)) : static_cast<int>(sizeof(CEnvPoint_v1));
					const int Count = BodySize / PointSize;
					Points.resize(static_cast<std::size_t>(Count));
					for(int i = 0; i < Count; i++)
					{
						const std::byte *pPoint = pBody + static_cast<std::size_t>(i) * PointSize;
						CEnvPoint_v1 Base{};
						std::memcpy(&Base, pPoint, sizeof(CEnvPoint_v1));
						Points[i].m_Time = Base.m_Time;
						Points[i].m_Curvetype = Base.m_Curvetype;
						std::copy(std::begin(Base.m_aValues), std::end(Base.m_aValues), Points[i].m_aValues);
						if(BezierPoints)
							std::memcpy(&Points[i].m_Bezier, pPoint + sizeof(CEnvPoint_v1), sizeof(CEnvPointBezier));
					}
				}
			}

			for(int i = 0; i < EnvelopeNum; i++)
			{
				const int Index = EnvelopeStart + i;
				const CItemView Item{File.GetItem(Index), File.GetItemSize(Index)};
				if(!Item.Valid())
					continue;

				CEnvelope Envelope;
				Envelope.m_Channels = Item.Int(4, 4);
				const int StartPoint = Item.Int(8, 0);
				const int NumPoints = Item.Int(12, 0);
				int aName[8]{};
				for(int k = 0; k < 8; k++)
					aName[k] = Item.Int(16 + k * 4, 0);
				base::IntsToStr(aName, 8, Envelope.m_Name);
				Envelope.m_Synchronized = Item.Int(48, 0);

				for(int p = 0; p < NumPoints; p++)
				{
					const int PointIndex = StartPoint + p;
					if(PointIndex < 0 || PointIndex >= static_cast<int>(Points.size()))
						break;
					const CEnvPoint &Point = Points[static_cast<std::size_t>(PointIndex)];
					CEnvelopePoint Out;
					Out.m_Time = Point.m_Time;
					Out.m_Curvetype = Point.m_Curvetype;
					std::copy(std::begin(Point.m_aValues), std::end(Point.m_aValues), Out.m_aValues);
					Out.m_Bezier = Point.m_Bezier;
					Envelope.m_vPoints.push_back(Out);
				}
				m_vEnvelopes.push_back(std::move(Envelope));
			}
		}

		// ------------------------------------------------- groups and layers
		{
			int GroupsStart = 0, GroupsNum = 0;
			int LayersStart = 0, LayersNum = 0;
			File.GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
			File.GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

			for(int g = 0; g < GroupsNum; g++)
			{
				const int GroupIndex = GroupsStart + g;
				const CItemView Item{File.GetItem(GroupIndex), File.GetItemSize(GroupIndex)};
				if(!Item.Valid())
					continue;

				CGroup Group;
				Group.m_SourceVersion = Item.Int(0, 1);
				Group.m_OffsetX = Item.Int(4);
				Group.m_OffsetY = Item.Int(8);
				Group.m_ParallaxX = Item.Int(12, 100);
				Group.m_ParallaxY = Item.Int(16, 100);
				const int StartLayer = Item.Int(20);
				const int NumLayers = Item.Int(24);
				if(Item.Has(28, 20) && Group.m_SourceVersion >= 2)
				{
					Group.m_UseClipping = Item.Int(28);
					Group.m_ClipX = Item.Int(32);
					Group.m_ClipY = Item.Int(36);
					Group.m_ClipW = Item.Int(40);
					Group.m_ClipH = Item.Int(44);
				}
				if(Item.Has(48, 12) && Group.m_SourceVersion >= 3)
				{
					int aName[3]{};
					for(int k = 0; k < 3; k++)
						aName[k] = Item.Int(48 + k * 4, 0);
					base::IntsToStr(aName, 3, Group.m_Name);
				}

				for(int l = 0; l < NumLayers; l++)
				{
					const int LayerIndex = LayersStart + StartLayer + l;
					if(LayerIndex < 0 || LayerIndex >= File.NumItems())
						break;
					const CItemView LayerItem{File.GetItem(LayerIndex), File.GetItemSize(LayerIndex)};
					if(!LayerItem.Valid())
						continue;

					const int LayerType = LayerItem.Int(4, LAYERTYPE_INVALID);
					CLayer Layer;
					Layer.m_Type = LayerType;
					Layer.m_LayerFlags = LayerItem.Int(8, 0);

					if(LayerType == LAYERTYPE_TILES)
					{
						Layer.m_SourceTilemapVersion = LayerItem.Int(12, 2);
						Layer.m_Width = LayerItem.Int(16);
						Layer.m_Height = LayerItem.Int(20);
						Layer.m_Flags = LayerItem.Int(24, 0);
						Layer.m_Color.r = LayerItem.Int(28, 255);
						Layer.m_Color.g = LayerItem.Int(32, 255);
						Layer.m_Color.b = LayerItem.Int(36, 255);
						Layer.m_Color.a = LayerItem.Int(40, 255);
						Layer.m_ColorEnv = LayerItem.Int(44, -1);
						Layer.m_ColorEnvOffset = LayerItem.Int(48);
						Layer.m_Image = LayerItem.Int(52, -1);
						const int DataIndex = LayerItem.Int(56, -1);
						if(LayerItem.Has(60, 12))
						{
							int aName[3]{};
							for(int k = 0; k < 3; k++)
								aName[k] = LayerItem.Int(60 + k * 4, 0);
							base::IntsToStr(aName, 3, Layer.m_Name);
						}
						if(LayerItem.Has(72, 20))
						{
							Layer.m_SourceHadPhysics = true;
							Layer.m_vTele = CopyBlock(File, LayerItem.Int(72, -1));
							Layer.m_vSpeedup = CopyBlock(File, LayerItem.Int(76, -1));
							Layer.m_vFront = CopyBlock(File, LayerItem.Int(80, -1));
							Layer.m_vSwitch = CopyBlock(File, LayerItem.Int(84, -1));
							Layer.m_vTune = CopyBlock(File, LayerItem.Int(88, -1));
						}

						if(Layer.m_Width <= 0 || Layer.m_Height <= 0 || Layer.TileCount() > (1u << 26))
						{
							LOG_WARN("map", "skipping a tile layer with invalid dimensions %dx%d", Layer.m_Width, Layer.m_Height);
							continue;
						}

						if(Layer.IsPhysicsLayer() && Layer.m_SourceHadPhysics)
						{
							// the visible index sits in the physics payload, the
							// m_Data block is a zeroed dummy
							const byte_vector *pPayload = nullptr;
							if(Layer.m_Flags & TILESLAYERFLAG_TELE)
								pPayload = &Layer.m_vTele;
							else if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP)
								pPayload = &Layer.m_vSpeedup;
							else if(Layer.m_Flags & TILESLAYERFLAG_FRONT)
								pPayload = &Layer.m_vFront;
							else if(Layer.m_Flags & TILESLAYERFLAG_SWITCH)
								pPayload = &Layer.m_vSwitch;
							else if(Layer.m_Flags & TILESLAYERFLAG_TUNE)
								pPayload = &Layer.m_vTune;
							Layer.m_vTiles.assign(Layer.TileCount(), CTile{});
							if(pPayload != nullptr)
								MirrorPhysicsTiles(*pPayload, Layer.m_Flags, Layer.m_vTiles);
						}
						else
						{
							const byte_vector Stored = CopyBlock(File, DataIndex);
							if(Layer.m_SourceTilemapVersion >= 4)
							{
								if(!ExpandTiles(Stored, static_cast<int>(Layer.TileCount()), Layer.m_vTiles))
									LOG_WARN("map", "tile layer %d has a malformed run length blob", LayerIndex);
							}
							else
							{
								ExpandPlainTiles(Stored, static_cast<int>(Layer.TileCount()), Layer.m_vTiles);
							}
						}
					}
					else if(LayerType == LAYERTYPE_QUADS)
					{
						const int NumQuads = LayerItem.Int(16, 0);
						const int DataIndex = LayerItem.Int(20, -1);
						Layer.m_Image = LayerItem.Int(24, -1);
						if(LayerItem.Has(28, 12))
						{
							int aName[3]{};
							for(int k = 0; k < 3; k++)
								aName[k] = LayerItem.Int(28 + k * 4, 0);
							base::IntsToStr(aName, 3, Layer.m_Name);
						}
						const byte_vector Data = CopyBlock(File, DataIndex);
						const int Count = std::min<int>(NumQuads, static_cast<int>(Data.size() / sizeof(CQuad)));
						Layer.m_vQuads.resize(static_cast<std::size_t>(std::max(Count, 0)));
						if(Count > 0)
							std::memcpy(Layer.m_vQuads.data(), Data.data(), static_cast<std::size_t>(Count) * sizeof(CQuad));
					}
					else if(LayerType == LAYERTYPE_SOUNDS)
					{
						const int NumSources = LayerItem.Int(16, 0);
						const int DataIndex = LayerItem.Int(20, -1);
						Layer.m_Sound = LayerItem.Int(24, -1);
						if(LayerItem.Has(28, 12))
						{
							int aName[3]{};
							for(int k = 0; k < 3; k++)
								aName[k] = LayerItem.Int(28 + k * 4, 0);
							base::IntsToStr(aName, 3, Layer.m_Name);
						}
						const byte_vector Data = CopyBlock(File, DataIndex);
						const int Count = std::min<int>(NumSources, static_cast<int>(Data.size() / sizeof(CSoundSource)));
						Layer.m_vSources.resize(static_cast<std::size_t>(std::max(Count, 0)));
						if(Count > 0)
							std::memcpy(Layer.m_vSources.data(), Data.data(), static_cast<std::size_t>(Count) * sizeof(CSoundSource));
					}
					else
					{
						LOG_WARN("map", "skipping a layer of unknown type %d", LayerType);
						continue;
					}

					Group.m_vLayers.push_back(std::move(Layer));
				}

				m_vGroups.push_back(std::move(Group));
			}
		}

		// ------------------------------------------------------------ extras
		for(int i = 0; i < File.NumItems(); i++)
		{
			int Type = 0, ID = 0;
			const std::byte *pBody = File.GetItem(i, &Type, &ID);
			if(pBody == nullptr)
				continue;
			const int Size = File.GetItemSize(i);
			if(Type == MAPITEMTYPE_VERSION || Type == MAPITEMTYPE_INFO || Type == MAPITEMTYPE_IMAGE ||
				Type == MAPITEMTYPE_ENVELOPE || Type == MAPITEMTYPE_GROUP || Type == MAPITEMTYPE_LAYER ||
				Type == MAPITEMTYPE_ENVPOINTS || Type == MAPITEMTYPE_SOUND)
				continue;

			CExtraItem Extra;
			Extra.m_Type = Type;
			Extra.m_ID = ID;
			Extra.m_Data.assign(pBody, pBody + Size);
			m_vExtras.push_back(std::move(Extra));
		}

		/*
			A preserved item stores data indices as plain ints in its body, which
			is all the editor needs to know: every block an extra can reach is
			kept, the others become empty placeholders so no index shifts. That
			keeps a DDRaceNetwork map intact without the file growing on every
			save (the generated blocks of the previous save are not reachable
			from any extra and are dropped again).
		*/
		if(HasExtras && File.NumRawData() > 0)
		{
			std::vector<bool> Referenced(static_cast<std::size_t>(File.NumRawData()), false);
			for(const CExtraItem &Extra : m_vExtras)
			{
				const std::size_t Fields = Extra.m_Data.size() / 4;
				for(std::size_t i = 0; i < Fields; i++)
				{
					const std::int32_t Value = base::ReadInt32(Extra.m_Data.data() + i * 4);
					if(Value >= 0 && Value < File.NumRawData())
						Referenced[static_cast<std::size_t>(Value)] = true;
				}
			}

			// only the range up to the last referenced index has to exist, the
			// trailing blocks can be dropped, which keeps a repeated save from
			// growing the file
			int Last = -1;
			for(int i = 0; i < File.NumRawData(); i++)
				if(Referenced[static_cast<std::size_t>(i)])
					Last = i;

			if(Last >= 0)
			{
				m_vRawData.resize(static_cast<std::size_t>(Last) + 1);
				int Kept = 0;
				std::size_t KeptBytes = 0;
				for(int i = 0; i <= Last; i++)
				{
					if(!Referenced[static_cast<std::size_t>(i)])
						continue;
					m_vRawData[static_cast<std::size_t>(i)] = CopyBlock(File, i);
					KeptBytes += m_vRawData[static_cast<std::size_t>(i)].size();
					Kept++;
				}
				LOG_INFO("map", "preserving %d data blocks (%zu bytes) out of %d for %zu items this editor does not model",
					Kept, KeptBytes, File.NumRawData(), m_vExtras.size());
			}
		}

		const CMapStatistics Stats = Statistics();
		LOG_INFO("map", "loaded datafile v%d: %d groups, %d layers, %d images, %d envelopes, %d preserved items",
			m_SourceDatafileVersion, Stats.m_Groups, Stats.m_Layers, Stats.m_Images, Stats.m_Envelopes, Stats.m_Extras);
		return true;
	}

	bool CMap::LoadFromBytes(const byte_vector &Bytes, std::string *pError)
	{
		CDataFileReader File;
		if(!File.Open(Bytes, pError))
			return false;
		return LoadFromDataFile(File, pError);
	}

	bool CMap::LoadFromFile(const storage::path &FileName, std::string *pError)
	{
		const std::optional<byte_vector> Bytes = storage::read(FileName);
		if(!Bytes.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + FileName.string() + "'";
			return false;
		}
		m_SourcePath = FileName.string();
		return LoadFromBytes(*Bytes, pError);
	}

	// -------------------------------------------------------------------- write
	bool CMap::SaveToBytes(byte_vector &Out, map_target Target, std::string *pError) const
	{
		const CMapTargetProfile &Profile = GetTargetProfile(Target);
		Out.clear();

		CDataFileWriter Writer;
		if(!Writer.Open(Profile.m_DatafileVersion, pError))
			return false;

		/*
			The extensions switch widens what is written. A target states what
			it can carry on its own; the switch says the mapmaker knows what they
			are doing and wants a DDRace feature in a target that does not define
			it. Without the switch the target's own capability is the answer, and
			the export warns about what that costs.
		*/
		const auto Allows = [&](map_extension Extension) {
			return Profile.HasExtension(Extension) || (m_bAllowExtensions && (Extension == ext_physics));
		};

		// the preserved items reference the data table by index, so every
		// original block goes in first and keeps its index
		const bool WriteExtras = Profile.HasExtension(ext_extras) && !m_vExtras.empty();
		if(WriteExtras)
		{
			for(const byte_vector &Block : m_vRawData)
				Writer.AddData(Block.data(), static_cast<int>(Block.size()));
		}

		// -------------------------------------------------------------- version
		{
			CMapItemVersion Item;
			Writer.AddItem(MAPITEMTYPE_VERSION, 0, &Item, sizeof(Item));
		}

		// ----------------------------------------------------------------- info
		{
			CMapItemInfo Item;
			const int Author = Writer.AddDataString(m_aAuthor);
			const int Version = Writer.AddDataString(m_aVersion);
			const int Credits = Writer.AddDataString(m_aCredits);
			const int License = Writer.AddDataString(m_aLicense);
			Item.m_Version = 1;
			Item.m_Author = Author;
			Item.m_MapVersion = Version;
			Item.m_Credits = Credits;
			Item.m_License = License;
			if(Profile.m_InfoSize >= static_cast<int>(sizeof(CMapItemInfoSettings)))
			{
				CMapItemInfoSettings Settings;
				static_cast<CMapItemInfo &>(Settings) = Item;
				Settings.m_Settings = -1;
				Writer.AddItem(MAPITEMTYPE_INFO, 0, &Settings, sizeof(Settings));
			}
			else
			{
				Writer.AddItem(MAPITEMTYPE_INFO, 0, &Item, sizeof(Item));
			}
		}

		// --------------------------------------------------------------- images
		for(std::size_t i = 0; i < m_vImages.size(); i++)
		{
			const CImage &Image = m_vImages[i];
			CMapItemImage Item;
			Item.m_Version = Profile.m_ImageVersion;
			Item.m_Width = Image.m_Width;
			Item.m_Height = Image.m_Height;
			Item.m_External = Image.IsEmbedded() ? 0 : 1;
			Item.m_ImageName = Writer.AddDataString(Image.m_Name);
			Item.m_ImageData = -1;
			if(Image.IsEmbedded() && !Image.m_Data.empty())
				Item.m_ImageData = Writer.AddData(Image.m_Data.data(), static_cast<int>(Image.m_Data.size()));
			Item.m_MustBe1 = 1;
			const int Size = (Profile.m_ImageVersion >= CMapItemImage::CURRENT_VERSION) ? sizeof(CMapItemImage) : sizeof(CMapItemImage_v1);
			Writer.AddItem(MAPITEMTYPE_IMAGE, static_cast<int>(i), &Item, Size);
		}

		// --------------------------------------------------------------- sounds
		if(Profile.HasExtension(ext_sounds))
		{
			for(std::size_t i = 0; i < m_vSounds.size(); i++)
			{
				const CSound &Sound = m_vSounds[i];
				CMapItemSound Item;
				Item.m_Version = 1;
				Item.m_External = Sound.m_External;
				Item.m_SoundName = Writer.AddDataString(Sound.m_Name);
				Item.m_SoundData = Sound.m_Data.empty() ? -1 : Writer.AddData(Sound.m_Data.data(), static_cast<int>(Sound.m_Data.size()));
				Item.m_SoundDataSize = static_cast<int>(Sound.m_Data.size());
				Writer.AddItem(MAPITEMTYPE_SOUND, static_cast<int>(i), &Item, sizeof(Item));
			}
		}

		// ------------------------------------------------- groups and layers
		int LayerCount = 0;
		int GroupCount = 0;
		for(const CGroup &Group : m_vGroups)
		{
			CMapItemGroup Item;
			Item.m_Version = Profile.m_GroupVersion;
			Item.m_OffsetX = Group.m_OffsetX;
			Item.m_OffsetY = Group.m_OffsetY;
			Item.m_ParallaxX = Group.m_ParallaxX;
			Item.m_ParallaxY = Group.m_ParallaxY;
			Item.m_StartLayer = LayerCount;
			Item.m_NumLayers = 0;
			Item.m_UseClipping = Group.m_UseClipping;
			Item.m_ClipX = Group.m_ClipX;
			Item.m_ClipY = Group.m_ClipY;
			Item.m_ClipW = Group.m_ClipW;
			Item.m_ClipH = Group.m_ClipH;
			base::StrToInts(Item.m_aName, 3, Group.m_Name);

			int InGroup = 0;
			for(const CLayer &Layer : Group.m_vLayers)
			{
				/*
					A physics layer needs the DDRace tilemap item, so it stays
					behind the target's ext_physics bit. A map that was loaded
					from a DDRace file keeps its layers in the model even when
					the editor is aimed at a vanilla target, and the export then
					drops them with the warning CompatibilityWarnings() already
					produced.
				*/
				if(Layer.IsPhysicsLayer() && !Allows(ext_physics))
					continue;
				// sound layers need the sound item type
				if(Layer.IsSounds() && !Profile.HasExtension(ext_sounds))
					continue;

				if(Layer.IsTiles())
				{
					CMapItemLayerTilemap Item;
					Item.m_Layer.m_Version = 0;
					Item.m_Layer.m_Type = LAYERTYPE_TILES;
					Item.m_Layer.m_Flags = Layer.m_LayerFlags;
					Item.m_Version = Profile.m_TilemapVersion;
					Item.m_Width = Layer.m_Width;
					Item.m_Height = Layer.m_Height;
					Item.m_Flags = Layer.m_Flags;
					Item.m_Color = Layer.m_Color;
					Item.m_ColorEnv = Layer.m_ColorEnv;
					Item.m_ColorEnvOffset = Layer.m_ColorEnvOffset;
					Item.m_Image = Layer.m_Image;
					base::StrToInts(Item.m_aName, 3, Layer.m_Name);

					if(Layer.IsPhysicsLayer())
					{
						// the visible data block stays empty, the payload sits in
						// the physics index of the layer
						byte_vector Empty(Layer.TileCount() * sizeof(CTile));
						Item.m_Data = Writer.AddData(Empty.data(), static_cast<int>(Empty.size()));
						Item.m_Tele = -1;
						Item.m_Speedup = -1;
						Item.m_Front = -1;
						Item.m_Switch = -1;
						Item.m_Tune = -1;
						const byte_vector *pPayload = nullptr;
						int *pTarget = nullptr;
						if(Layer.m_Flags & TILESLAYERFLAG_TELE) { pPayload = &Layer.m_vTele; pTarget = &Item.m_Tele; }
						else if(Layer.m_Flags & TILESLAYERFLAG_SPEEDUP) { pPayload = &Layer.m_vSpeedup; pTarget = &Item.m_Speedup; }
						else if(Layer.m_Flags & TILESLAYERFLAG_FRONT) { pPayload = &Layer.m_vFront; pTarget = &Item.m_Front; }
						else if(Layer.m_Flags & TILESLAYERFLAG_SWITCH) { pPayload = &Layer.m_vSwitch; pTarget = &Item.m_Switch; }
						else if(Layer.m_Flags & TILESLAYERFLAG_TUNE) { pPayload = &Layer.m_vTune; pTarget = &Item.m_Tune; }
						if(pPayload != nullptr && !pPayload->empty())
							*pTarget = Writer.AddData(pPayload->data(), static_cast<int>(pPayload->size()));
					}
					else
					{
						const byte_vector Data = Profile.m_RunLengthTiles ? CompressTiles(Layer.m_vTiles) : PlainTiles(Layer.m_vTiles);
						Item.m_Data = Writer.AddData(Data.data(), static_cast<int>(Data.size()));
						Item.m_Tele = -1;
						Item.m_Speedup = -1;
						Item.m_Front = -1;
						Item.m_Switch = -1;
						Item.m_Tune = -1;
					}

					/*
						A physics layer needs the DDRace sized item, whose body
						carries the five payload indices; writing it as the
						shorter item would cut them off. A vanilla tile layer in
						the same map still has to be the short one, which is why
						this is decided per layer and not per profile.
					*/
					const bool bDdnetItem = Profile.IsDdnetTilemap() || Layer.IsPhysicsLayer();
					const int Size = bDdnetItem ? sizeof(CMapItemLayerTilemap) : sizeof(CMapItemLayerTilemap_v3);
					Writer.AddItem(MAPITEMTYPE_LAYER, LayerCount, &Item, Size);
				}
				else if(Layer.IsQuads())
				{
					CMapItemLayerQuads Item;
					Item.m_Layer.m_Version = 0;
					Item.m_Layer.m_Type = LAYERTYPE_QUADS;
					Item.m_Layer.m_Flags = Layer.m_LayerFlags;
					Item.m_Version = Profile.m_QuadsVersion;
					Item.m_NumQuads = static_cast<int>(Layer.m_vQuads.size());
					if(Layer.m_vQuads.empty())
					{
						// the reference writes a dummy block so old readers do not
						// see a null pointer
						CQuad Dummy{};
						Item.m_Data = Writer.AddDataSwapped(&Dummy, sizeof(Dummy));
					}
					else
					{
						Item.m_Data = Writer.AddDataSwapped(Layer.m_vQuads.data(), static_cast<int>(Layer.m_vQuads.size() * sizeof(CQuad)));
					}
					Item.m_Image = Layer.m_Image;
					base::StrToInts(Item.m_aName, 3, Layer.m_Name);
					Writer.AddItem(MAPITEMTYPE_LAYER, LayerCount, &Item, sizeof(Item));
				}
				else if(Layer.IsSounds())
				{
					CMapItemLayerSounds Item;
					Item.m_Layer.m_Version = 0;
					Item.m_Layer.m_Type = LAYERTYPE_SOUNDS;
					Item.m_Layer.m_Flags = Layer.m_LayerFlags;
					Item.m_Version = 2;
					Item.m_NumSources = static_cast<int>(Layer.m_vSources.size());
					if(Layer.m_vSources.empty())
					{
						CSoundSource Dummy{};
						Item.m_Data = Writer.AddDataSwapped(&Dummy, sizeof(Dummy));
					}
					else
					{
						Item.m_Data = Writer.AddDataSwapped(Layer.m_vSources.data(), static_cast<int>(Layer.m_vSources.size() * sizeof(CSoundSource)));
					}
					Item.m_Sound = Layer.m_Sound;
					base::StrToInts(Item.m_aName, 3, Layer.m_Name);
					Writer.AddItem(MAPITEMTYPE_LAYER, LayerCount, &Item, sizeof(Item));
				}
				else
				{
					continue;
				}

				LayerCount++;
				InGroup++;
			}

			Item.m_NumLayers = InGroup;
			Writer.AddItem(MAPITEMTYPE_GROUP, GroupCount, &Item, sizeof(Item));
			GroupCount++;
		}

		// ------------------------------------------------------------ envelopes
		bool Bezier = false;
		if(Profile.HasExtension(ext_bezier_envelopes))
			for(const CEnvelope &Envelope : m_vEnvelopes)
				if(Envelope.UsesBezier())
					Bezier = true;

		int PointCount = 0;
		for(std::size_t e = 0; e < m_vEnvelopes.size(); e++)
		{
			const CEnvelope &Envelope = m_vEnvelopes[e];
			CMapItemEnvelope Item;
			// The point size follows the item version, so only a target that can
			// carry the tangents gets the version 3 item; the two constants are
			// the same value, which is why the cast keeps the comparison quiet.
			Item.m_Version = Bezier ? static_cast<int>(CMapItemEnvelope::CURRENT_VERSION) :
				static_cast<int>(CMapItemEnvelope_v2::CURRENT_VERSION);
			if(!Profile.HasExtension(ext_bezier_envelopes))
				Item.m_Version = CMapItemEnvelope_v2::CURRENT_VERSION;
			Item.m_Channels = Envelope.m_Channels;
			Item.m_StartPoint = PointCount;
			Item.m_NumPoints = static_cast<int>(Envelope.m_vPoints.size());
			Item.m_Synchronized = Envelope.m_Synchronized;
			base::StrToInts(Item.m_aName, 8, Envelope.m_Name);
			Writer.AddItem(MAPITEMTYPE_ENVELOPE, static_cast<int>(e), &Item, sizeof(Item));
			PointCount += Item.m_NumPoints;
		}

		{
			byte_vector Points;
			if(PointCount > 0)
			{
				Points.resize(static_cast<std::size_t>(PointCount) * (Bezier ? sizeof(CEnvPoint) : sizeof(CEnvPoint_v1)));
				std::size_t Offset = 0;
				for(const CEnvelope &Envelope : m_vEnvelopes)
				{
					for(const CEnvelopePoint &Point : Envelope.m_vPoints)
					{
						CEnvPoint_v1 Base{};
						Base.m_Time = Point.m_Time;
						Base.m_Curvetype = Point.m_Curvetype;
						std::copy(std::begin(Point.m_aValues), std::end(Point.m_aValues), Base.m_aValues);
						std::memcpy(Points.data() + Offset, &Base, sizeof(Base));
						Offset += sizeof(Base);
						if(Bezier)
						{
							std::memcpy(Points.data() + Offset, &Point.m_Bezier, sizeof(CEnvPointBezier));
							Offset += sizeof(CEnvPointBezier);
						}
					}
				}
			}
			Writer.AddItem(MAPITEMTYPE_ENVPOINTS, 0, Points.data(), static_cast<int>(Points.size()));
		}

		// --------------------------------------------------------------- extras
		if(WriteExtras)
		{
			for(const CExtraItem &Extra : m_vExtras)
				Writer.AddItem(Extra.m_Type, Extra.m_ID, Extra.m_Data.data(), static_cast<int>(Extra.m_Data.size()));
		}

		if(!Writer.Finish(Out, pError))
			return false;

		LOG_INFO("map", "serialised for %s: datafile v%d, %zu bytes", Profile.m_pName, Profile.m_DatafileVersion, Out.size());
		return true;
	}

	bool CMap::SaveToFile(const storage::path &FileName, map_target Target, std::string *pError) const
	{
		byte_vector Out;
		if(!SaveToBytes(Out, Target, pError))
			return false;
		if(!storage::write(FileName, Out))
		{
			if(pError != nullptr)
				*pError = "failed to write '" + FileName.string() + "'";
			return false;
		}
		return true;
	}
} // namespace game
