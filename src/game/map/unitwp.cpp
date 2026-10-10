#include "unitwp.h"

#include <cstring>
#include <map>

#include "base/define.h"
#include "base/endian.h"
#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"

#if defined(UNITEED_OPENSSL)
#define LIBSUGARX_UUID_GENERATION_IMPL
#include <sugar_uuid.h>
#endif

namespace game
{
	namespace
	{
		constexpr char Magic[8] = {'U', 'N', 'I', 'T', 'W', 'P', '\0', '\x01'};

		// chunk ids are four characters, no terminator
		constexpr char ChunkMeta[4] = {'M', 'E', 'T', 'A'};
		constexpr char ChunkMap[4] = {'M', 'A', 'P', ' '};
		constexpr char ChunkEdit[4] = {'E', 'D', 'I', 'T'};
		constexpr char ChunkResource[4] = {'R', 'S', 'R', 'C'};
		constexpr char ChunkThumbnail[4] = {'T', 'H', 'U', 'M'};

		void WriteU32(byte_vector &Out, std::uint32_t Value)
		{
			std::byte Buffer[4];
			base::WriteInt32(Buffer, static_cast<std::int32_t>(Value));
			Out.insert(Out.end(), Buffer, Buffer + 4);
		}

		void WriteChunk(byte_vector &Out, const char Id[4], const byte_vector &Data)
		{
			const std::byte *pId = reinterpret_cast<const std::byte *>(Id);
			Out.insert(Out.end(), pId, pId + 4);
			WriteU32(Out, static_cast<std::uint32_t>(Data.size()));
			Out.insert(Out.end(), Data.begin(), Data.end());
		}

		void WriteTextChunk(byte_vector &Out, const char Id[4], const std::string &Text)
		{
			const std::byte *pData = reinterpret_cast<const std::byte *>(Text.data());
			WriteChunk(Out, Id, byte_vector(pData, pData + Text.size()));
		}

		std::uint32_t ReadU32(const std::byte *pData) { return base::ReadUInt32(pData); }

		std::string MakePackageId()
		{
#if defined(UNITEED_OPENSSL)
			const libsugarx::uuid Id = libsugarx::uuid::generate_v4_nullable();
			return std::string(Id.to_string().view());
#else
			return base::Format("local-%lld", static_cast<long long>(base::UnixTime()));
#endif
		}
	} // namespace

	std::string CUnitwpMeta::ToText() const
	{
		std::string Text;
		const auto Line = [&Text](const char *pKey, const std::string &Value) {
			if(!Value.empty())
				Text += std::string(pKey) + "=" + Value + "\n";
		};
		Line("id", m_PackageId);
		Line("name", m_Name);
		Line("author", m_Author);
		Line("target", m_ExportTarget);
		Line("created", m_Created);
		Line("updated", m_Updated);
		Line("editor", m_EditorVersion);
		Line("source", m_SourceMap);
		return Text;
	}

	CUnitwpMeta CUnitwpMeta::FromText(const std::string &Text)
	{
		CUnitwpMeta Meta;
		for(const std::string &RawLine : base::Split(Text, '\n'))
		{
			const std::string Line = base::Trim(RawLine);
			if(Line.empty() || Line[0] == '#')
				continue;
			const std::size_t Pos = Line.find('=');
			if(Pos == std::string::npos)
				continue;
			const std::string Key = base::Trim(Line.substr(0, Pos));
			const std::string Value = base::Trim(Line.substr(Pos + 1));
			if(Key == "id") Meta.m_PackageId = Value;
			else if(Key == "name") Meta.m_Name = Value;
			else if(Key == "author") Meta.m_Author = Value;
			else if(Key == "target") Meta.m_ExportTarget = Value;
			else if(Key == "created") Meta.m_Created = Value;
			else if(Key == "updated") Meta.m_Updated = Value;
			else if(Key == "editor") Meta.m_EditorVersion = Value;
			else if(Key == "source") Meta.m_SourceMap = Value;
		}
		return Meta;
	}

	void CUnitwpPackage::Clear()
	{
		m_Map.Clear();
		m_Meta = CUnitwpMeta{};
		m_EditorState.clear();
		m_vResources.clear();
	}

	void CUnitwpPackage::Adopt(const CMap &Source, std::string_view Name, std::string_view SourcePath)
	{
		Clear();
		m_Map = Source;
		m_Meta.m_PackageId = MakePackageId();
		m_Meta.m_Name = std::string(Name);
		m_Meta.m_Author = Source.m_aAuthor;
		m_Meta.m_Created = std::to_string(base::UnixTime());
		m_Meta.m_Updated = m_Meta.m_Created;
		m_Meta.m_EditorVersion = GAME_VERSION;
		m_Meta.m_SourceMap = std::string(SourcePath);
	}

	const CUnitwpResource *CUnitwpPackage::FindResource(std::string_view Name) const
	{
		for(const CUnitwpResource &Resource : m_vResources)
			if(Resource.m_Name == Name)
				return &Resource;
		return nullptr;
	}

	bool CUnitwpPackage::IsPackage(const byte_vector &Bytes)
	{
		return Bytes.size() >= 8 && std::memcmp(Bytes.data(), Magic, 8) == 0;
	}

	bool CUnitwpPackage::Serialise(byte_vector &Out, std::string *pError) const
	{
		Out.clear();

		// the map inside the package uses the package profile so that nothing is
		// lost, whatever the package is exported to later
		byte_vector MapBytes;
		if(!m_Map.SaveToBytes(MapBytes, GetPackageTarget(), pError))
			return false;

		Out.insert(Out.end(), reinterpret_cast<const std::byte *>(Magic), reinterpret_cast<const std::byte *>(Magic) + 8);
		WriteU32(Out, ContainerVersion);
		WriteU32(Out, 0);

		WriteTextChunk(Out, ChunkMeta, m_Meta.ToText());
		WriteChunk(Out, ChunkMap, MapBytes);
		WriteTextChunk(Out, ChunkEdit, m_EditorState);

		if(!m_vResources.empty())
		{
			byte_vector Table;
			WriteU32(Table, static_cast<std::uint32_t>(m_vResources.size()));
			for(const CUnitwpResource &Resource : m_vResources)
			{
				WriteU32(Table, static_cast<std::uint32_t>(Resource.m_Name.size()));
				Table.insert(Table.end(), reinterpret_cast<const std::byte *>(Resource.m_Name.data()),
					reinterpret_cast<const std::byte *>(Resource.m_Name.data()) + Resource.m_Name.size());
				WriteU32(Table, static_cast<std::uint32_t>(Resource.m_Data.size()));
				Table.insert(Table.end(), Resource.m_Data.begin(), Resource.m_Data.end());
			}
			WriteChunk(Out, ChunkResource, Table);
		}

		LOG_INFO("unitwp", "serialised package: %zu bytes (map %zu bytes, %zu resources)",
			Out.size(), MapBytes.size(), m_vResources.size());
		return true;
	}

	bool CUnitwpPackage::Deserialise(const byte_vector &Bytes, std::string *pError)
	{
		const auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = Message;
			LOG_ERROR("unitwp", "%s", Message.c_str());
			return false;
		};

		if(!IsPackage(Bytes))
			return Fail("not a .unitwp package");
		if(Bytes.size() < 16)
			return Fail("the package header is truncated");

		const std::uint32_t Version = ReadU32(Bytes.data() + 8);
		if(Version != ContainerVersion)
			return Fail("unsupported package version " + std::to_string(Version));

		Clear();

		std::size_t Offset = 16;
		bool HaveMap = false;
		while(Offset + 8 <= Bytes.size())
		{
			const char *pId = reinterpret_cast<const char *>(Bytes.data() + Offset);
			const std::uint32_t Size = ReadU32(Bytes.data() + Offset + 4);
			Offset += 8;
			if(Offset + Size > Bytes.size())
				return Fail("a package chunk is truncated");

			const std::byte *pChunk = Bytes.data() + Offset;
			if(std::memcmp(pId, ChunkMeta, 4) == 0)
			{
				m_Meta = CUnitwpMeta::FromText(std::string(reinterpret_cast<const char *>(pChunk), Size));
			}
			else if(std::memcmp(pId, ChunkMap, 4) == 0)
			{
				byte_vector MapBytes(pChunk, pChunk + Size);
				if(!m_Map.LoadFromBytes(MapBytes, pError))
					return false;
				HaveMap = true;
			}
			else if(std::memcmp(pId, ChunkEdit, 4) == 0)
			{
				m_EditorState.assign(reinterpret_cast<const char *>(pChunk), Size);
			}
			else if(std::memcmp(pId, ChunkResource, 4) == 0)
			{
				std::size_t ResourceOffset = 0;
				if(Size < 4)
					return Fail("the resource chunk is truncated");
				const std::uint32_t Count = ReadU32(pChunk);
				ResourceOffset = 4;
				for(std::uint32_t i = 0; i < Count; i++)
				{
					if(ResourceOffset + 4 > Size)
						return Fail("the resource table is truncated");
					const std::uint32_t NameLength = ReadU32(pChunk + ResourceOffset);
					ResourceOffset += 4;
					if(ResourceOffset + NameLength + 4 > Size)
						return Fail("the resource table is truncated");
					CUnitwpResource Resource;
					Resource.m_Name.assign(reinterpret_cast<const char *>(pChunk + ResourceOffset), NameLength);
					ResourceOffset += NameLength;
					const std::uint32_t DataLength = ReadU32(pChunk + ResourceOffset);
					ResourceOffset += 4;
					if(ResourceOffset + DataLength > Size)
						return Fail("the resource table is truncated");
					Resource.m_Data.assign(pChunk + ResourceOffset, pChunk + ResourceOffset + DataLength);
					ResourceOffset += DataLength;
					m_vResources.push_back(std::move(Resource));
				}
			}

			Offset += Size;
		}

		if(!HaveMap)
			return Fail("the package has no map chunk");

		LOG_INFO("unitwp", "loaded package '%s' (%zu resources)", m_Meta.m_Name.c_str(), m_vResources.size());
		return true;
	}

	bool CUnitwpPackage::SaveToFile(const storage::path &FileName, std::string *pError) const
	{
		byte_vector Out;
		if(!Serialise(Out, pError))
			return false;
		if(!storage::write(FileName, Out))
		{
			if(pError != nullptr)
				*pError = "failed to write '" + FileName.string() + "'";
			return false;
		}
		return true;
	}

	bool CUnitwpPackage::LoadFromFile(const storage::path &FileName, std::string *pError)
	{
		const std::optional<byte_vector> Bytes = storage::read(FileName);
		if(!Bytes.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + FileName.string() + "'";
			return false;
		}
		return Deserialise(*Bytes, pError);
	}
} // namespace game
