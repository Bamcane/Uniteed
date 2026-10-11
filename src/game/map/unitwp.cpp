#include "unitwp.h"

#include <cstring>
#include <map>

#include "base/define.h"
#include "base/endian.h"
#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "base/uuid.h"
#include "game/assets.h"

namespace game
{
	namespace
	{
		constexpr char Magic[8] = {'U', 'N', 'I', 'T', 'W', 'P', '\0', '\x01'};

		// chunk ids are four characters, no terminator
		constexpr std::string_view ChunkMeta = "META";
		constexpr std::string_view ChunkMap = "MAP ";
		constexpr std::string_view ChunkEdit = "EDIT";
		constexpr std::string_view ChunkRules = "RULE";
		constexpr std::string_view ChunkResource = "RSRC";
		constexpr std::string_view ChunkThumbnail = "THUM";

		void WriteU32(byte_vector &Out, std::uint32_t Value)
		{
			std::byte Buffer[4];
			base::WriteInt32(Buffer, static_cast<std::int32_t>(Value));
			Out.insert(Out.end(), Buffer, Buffer + 4);
		}

		void WriteChunk(byte_vector &Out, std::string_view Id, std::string_view Data)
		{
			const std::byte *pId = reinterpret_cast<const std::byte *>(Id.data());
			Out.insert(Out.end(), pId, pId + Id.size());
			WriteU32(Out, static_cast<std::uint32_t>(Data.size()));
			const std::byte *pData = reinterpret_cast<const std::byte *>(Data.data());
			Out.insert(Out.end(), pData, pData + Data.size());
		}

		std::uint32_t ReadU32(const std::byte *pData) { return base::ReadUInt32(pData); }

		std::string MakePackageId()
		{
			return base::NewUuid();
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
		Line("description", m_Description);
		Line("target", m_ExportTarget);
		Line("assets", m_Assets);
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
			else if(Key == "description") Meta.m_Description = Value;
			else if(Key == "target") Meta.m_ExportTarget = Value;
			else if(Key == "assets") Meta.m_Assets = Value;
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
		m_Rules.clear();
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

	void CUnitwpPackage::AttachMapResources()
	{
		m_vResources.clear();
		for(const CImage &Image : m_Map.m_vImages)
		{
			if(Image.IsEmbedded())
				continue;
			const std::optional<storage::path> File = FindImageFile(Image.m_Name);
			if(!File.has_value())
				continue;
			const std::optional<byte_vector> Data = storage::read(*File);
			if(!Data.has_value())
				continue;
			CUnitwpResource Resource;
			Resource.m_Name = "mapres/" + File->filename().string();
			Resource.m_Data = *Data;
			m_vResources.push_back(std::move(Resource));
		}
	}

	bool CUnitwpPackage::IsPackage(const byte_vector &Bytes)
	{
		return Bytes.size() >= 8 && std::memcmp(Bytes.data(), Magic, 8) == 0;
	}

	bool CUnitwpPackage::FindChunk(const byte_vector &Bytes, std::string_view Id, std::string *pOut)
	{
		if(!IsPackage(Bytes) || Bytes.size() < 16 || Id.size() != 4)
			return false;
		std::size_t Offset = 16;
		while(Offset + 8 <= Bytes.size())
		{
			const char *pId = reinterpret_cast<const char *>(Bytes.data() + Offset);
			const std::uint32_t Size = ReadU32(Bytes.data() + Offset + 4);
			Offset += 8;
			if(Offset + Size > Bytes.size())
				return false;
			if(std::memcmp(pId, Id.data(), 4) == 0)
			{
				if(pOut != nullptr)
					pOut->assign(reinterpret_cast<const char *>(Bytes.data() + Offset), Size);
				return true;
			}
			Offset += Size;
		}
		return false;
	}

	bool CUnitwpPackage::SetChunk(byte_vector &Bytes, std::string_view Id, std::string_view Data)
	{
		if(!IsPackage(Bytes) || Bytes.size() < 16 || Id.size() != 4)
			return false;
		byte_vector Out(Bytes.begin(), Bytes.begin() + 16);
		std::size_t Offset = 16;
		bool bReplaced = false;
		while(Offset + 8 <= Bytes.size())
		{
			const char *pId = reinterpret_cast<const char *>(Bytes.data() + Offset);
			const std::uint32_t Size = ReadU32(Bytes.data() + Offset + 4);
			const std::size_t Header = Offset;
			Offset += 8;
			if(Offset + Size > Bytes.size())
				return false;
			if(std::memcmp(pId, Id.data(), 4) == 0)
			{
				// the first occurrence wins, exactly like Deserialise
				if(!bReplaced)
				{
					WriteChunk(Out, Id, Data);
					bReplaced = true;
				}
			}
			else
				Out.insert(Out.end(), Bytes.begin() + static_cast<std::ptrdiff_t>(Header),
					Bytes.begin() + static_cast<std::ptrdiff_t>(Offset + Size));
			Offset += Size;
		}
		if(!bReplaced)
			WriteChunk(Out, Id, Data);
		Bytes = std::move(Out);
		return true;
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

		WriteChunk(Out, ChunkMeta, m_Meta.ToText());
		WriteChunk(Out, ChunkMap, std::string_view(reinterpret_cast<const char *>(MapBytes.data()), MapBytes.size()));
		WriteChunk(Out, ChunkEdit, m_EditorState);
		if(!m_Rules.empty())
			WriteChunk(Out, ChunkRules, m_Rules);

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
			WriteChunk(Out, ChunkResource, std::string_view(reinterpret_cast<const char *>(Table.data()), Table.size()));
		}

		LOG_INFO("unitwp", "serialised package: {:d} bytes (map {:d} bytes, {:d} resources)",
			Out.size(), MapBytes.size(), m_vResources.size());
		return true;
	}

	bool CUnitwpPackage::Deserialise(const byte_vector &Bytes, std::string *pError)
	{
		const auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = Message;
			LOG_ERROR("unitwp", "{}", Message.c_str());
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
			if(std::memcmp(pId, ChunkMeta.data(), 4) == 0)
			{
				m_Meta = CUnitwpMeta::FromText(std::string(reinterpret_cast<const char *>(pChunk), Size));
			}
			else if(std::memcmp(pId, ChunkMap.data(), 4) == 0)
			{
				byte_vector MapBytes(pChunk, pChunk + Size);
				if(!m_Map.LoadFromBytes(MapBytes, pError))
					return false;
				HaveMap = true;
			}
			else if(std::memcmp(pId, ChunkEdit.data(), 4) == 0)
			{
				m_EditorState.assign(reinterpret_cast<const char *>(pChunk), Size);
			}
			else if(std::memcmp(pId, ChunkRules.data(), 4) == 0)
			{
				m_Rules.assign(reinterpret_cast<const char *>(pChunk), Size);
			}
			else if(std::memcmp(pId, ChunkResource.data(), 4) == 0)
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

		LOG_INFO("unitwp", "loaded package '{}' ({:d} resources)", m_Meta.m_Name.c_str(), m_vResources.size());
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
