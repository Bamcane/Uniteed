#include "datafile.h"

#include <algorithm>
#include <cstring>
#include <map>

#include <zlib.h>
#if defined(UNITEED_ZSTD)
#include <zstd.h>
#endif

#include "base/endian.h"
#include "base/logger.h"

namespace game
{
	namespace
	{
		constexpr std::size_t HeaderSize = 36;
		constexpr std::size_t ItemTypeSize = 12;
		constexpr std::size_t ItemHeaderSize = 8;

		const void *Offset(const std::byte *pBase, std::size_t Bytes) { return pBase + Bytes; }

		bool Deflate(const void *pData, int Size, int Level, byte_vector &Out)
		{
			uLongf Bound = compressBound(static_cast<uLong>(Size));
			Out.resize(Bound);
			const int Result = compress2(reinterpret_cast<Bytef *>(Out.data()), &Bound,
				reinterpret_cast<const Bytef *>(pData), static_cast<uLong>(Size), Level);
			if(Result != Z_OK)
				return false;
			Out.resize(Bound);
			return true;
		}

#if defined(UNITEED_ZSTD)
		bool ZstdCompress(const void *pData, int Size, int Level, byte_vector &Out)
		{
			const std::size_t Bound = ZSTD_compressBound(static_cast<std::size_t>(Size));
			Out.resize(Bound);
			const std::size_t Result = ZSTD_compress(Out.data(), Bound, pData, static_cast<std::size_t>(Size), Level);
			if(ZSTD_isError(Result))
				return false;
			Out.resize(Result);
			return true;
		}

		bool ZstdDecompress(const void *pData, std::size_t Size, int UncompressedSize, byte_vector &Out)
		{
			Out.resize(static_cast<std::size_t>(UncompressedSize));
			const std::size_t Result = ZSTD_decompress(Out.data(), Out.size(), pData, Size);
			if(ZSTD_isError(Result) || Result != Out.size())
				return false;
			return true;
		}
#endif
	} // namespace

	// --------------------------------------------------------------------- read
	bool CDataFileReader::Open(const byte_vector &Bytes, std::string *pError)
	{
		Close();

		const auto Fail = [&](const char *pMessage) {
			if(pError != nullptr)
				*pError = pMessage;
			LOG_ERROR("datafile", "%s", pMessage);
			return false;
		};

		if(Bytes.size() < HeaderSize)
			return Fail("file is too small to be a map");

		const std::byte *pBase = Bytes.data();
		(void)pBase;
		if(!(pBase[0] == std::byte('D') && pBase[1] == std::byte('A') && pBase[2] == std::byte('T') && pBase[3] == std::byte('A')))
		{
			if(pBase[0] == std::byte('A') && pBase[1] == std::byte('T') && pBase[2] == std::byte('A') && pBase[3] == std::byte('D'))
				return Fail("the map is stored in big endian byte order, which is not supported");
			return Fail("wrong map signature, expected DATA");
		}

		const int Version = base::ReadInt32(Offset(pBase, 4));
		if(Version != 3 && Version != 4 && Version != 5)
			return Fail("unsupported datafile version (expected 3, 4 or 5)");

		const int NumItemTypes = base::ReadInt32(Offset(pBase, 16));
		const int NumItems = base::ReadInt32(Offset(pBase, 20));
		const int NumRawData = base::ReadInt32(Offset(pBase, 24));
		const int ItemSize = base::ReadInt32(Offset(pBase, 28));
		const int DataSize = base::ReadInt32(Offset(pBase, 32));

		if(NumItemTypes < 0 || NumItems < 0 || NumRawData < 0 || ItemSize < 0 || DataSize < 0)
			return Fail("invalid datafile header");

		const std::int64_t TableSize = static_cast<std::int64_t>(NumItemTypes) * ItemTypeSize +
			static_cast<std::int64_t>(NumItems) * 4 +
			static_cast<std::int64_t>(NumRawData) * 4 +
			(Version >= 4 ? static_cast<std::int64_t>(NumRawData) * 4 : 0) +
			(Version >= 5 ? 4 : 0);

		if(static_cast<std::int64_t>(HeaderSize) + TableSize > static_cast<std::int64_t>(Bytes.size()))
			return Fail("the datafile tables are truncated");

		// keep our own copy and take every view from it, so no pointer ends up
		// pointing into the caller's buffer
		m_Bytes = Bytes;
		m_Version = Version;
		m_NumItemTypes = NumItemTypes;
		m_NumItems = NumItems;
		m_NumRawData = NumRawData;
		m_ItemSize = ItemSize;
		m_DataSize = DataSize;

		pBase = m_Bytes.data();
		const std::byte *pTables = pBase + HeaderSize;
		const std::byte *pItems = pTables + TableSize;
		m_pItemOffsets = reinterpret_cast<const int *>(pTables + static_cast<std::size_t>(NumItemTypes) * ItemTypeSize);
		m_pDataOffsets = m_pItemOffsets + NumItems;
		m_pDataSizes = (Version >= 4) ? m_pDataOffsets + NumRawData : nullptr;

		if(Version >= 5)
		{
#if defined(UNITEED_ZSTD)
			const int CompressedItemSize = base::ReadInt32(pTables + TableSize - 4);
			if(CompressedItemSize < 0 || static_cast<std::int64_t>(HeaderSize) + TableSize + CompressedItemSize > static_cast<std::int64_t>(Bytes.size()))
				return Fail("invalid compressed item section size");
			if(ItemSize > 0)
			{
				if(!ZstdDecompress(pItems, static_cast<std::size_t>(CompressedItemSize), ItemSize, m_ItemBuffer))
					return Fail("failed to decompress the item section");
			}
			m_pItemStart = m_ItemBuffer.data();
			m_ItemStartOffset = HeaderSize + static_cast<std::size_t>(TableSize) + static_cast<std::size_t>(CompressedItemSize);
			// the data blocks follow the compressed item section directly, the
			// uncompressed item size is not a file offset here
			m_DataStartOffset = m_ItemStartOffset;
#else
			return Fail("this build has no zstd support and cannot read datafile version 5");
#endif
		}
		else
		{
			if(static_cast<std::int64_t>(HeaderSize) + TableSize + ItemSize + DataSize > static_cast<std::int64_t>(Bytes.size()))
				return Fail("the datafile items or data are truncated");
			m_pItemStart = pItems;
			m_ItemStartOffset = HeaderSize + static_cast<std::size_t>(TableSize);
			m_DataStartOffset = m_ItemStartOffset + static_cast<std::size_t>(ItemSize);
		}

		m_pDataStart = m_Bytes.data() + m_DataStartOffset;
		m_aData.assign(static_cast<std::size_t>(NumRawData), std::nullopt);
		m_Open = true;
		return true;
	}

	void CDataFileReader::Close()
	{
		m_Bytes.clear();
		m_Bytes.shrink_to_fit();
		m_ItemBuffer.clear();
		m_ItemBuffer.shrink_to_fit();
		m_Open = false;
		m_Version = 0;
		m_NumItemTypes = 0;
		m_NumItems = 0;
		m_NumRawData = 0;
		m_ItemSize = 0;
		m_DataSize = 0;
		m_ItemStartOffset = 0;
		m_DataStartOffset = 0;
		m_pItemOffsets = nullptr;
		m_pDataOffsets = nullptr;
		m_pDataSizes = nullptr;
		m_pItemStart = nullptr;
		m_pDataStart = nullptr;
		m_aData.clear();
	}

	bool CDataFileReader::GetType(int Type, int *pStart, int *pNum) const
	{
		if(pStart != nullptr)
			*pStart = 0;
		if(pNum != nullptr)
			*pNum = 0;
		if(!m_Open)
			return false;

		const std::byte *pTypes = m_Bytes.data() + HeaderSize;
		for(int i = 0; i < m_NumItemTypes; i++)
		{
			const std::byte *pType = pTypes + static_cast<std::size_t>(i) * ItemTypeSize;
			if(base::ReadInt32(pType) != Type)
				continue;

			const int Start = base::ReadInt32(pType + 4);
			const int Num = base::ReadInt32(pType + 8);
			if(Start < 0 || Num < 0 || static_cast<std::int64_t>(Start) + Num > m_NumItems)
				return false;

			if(pStart != nullptr)
				*pStart = Start;
			if(pNum != nullptr)
				*pNum = Num;
			return true;
		}
		return false;
	}

	const std::byte *CDataFileReader::GetItem(int Index, int *pType, int *pID) const
	{
		if(pType != nullptr)
			*pType = 0;
		if(pID != nullptr)
			*pID = 0;
		if(!m_Open || Index < 0 || Index >= m_NumItems)
			return nullptr;

		// the offsets are relative to the item blob, stored as raw ints
		const std::int64_t ItemOffset = base::ReadInt32(&m_pItemOffsets[Index]);
		if(ItemOffset < 0 || ItemOffset + static_cast<std::int64_t>(ItemHeaderSize) > m_ItemSize)
			return nullptr;

		const std::byte *pItem = m_pItemStart + ItemOffset;
		const std::int32_t TypeAndID = base::ReadInt32(pItem);
		if(pType != nullptr)
			*pType = (TypeAndID >> 16) & 0xffff;
		if(pID != nullptr)
			*pID = TypeAndID & 0xffff;

		return pItem + ItemHeaderSize;
	}

	int CDataFileReader::GetItemSize(int Index) const
	{
		if(!m_Open || Index < 0 || Index >= m_NumItems)
			return 0;

		const std::int64_t ItemOffset = base::ReadInt32(&m_pItemOffsets[Index]);
		if(ItemOffset < 0 || ItemOffset + static_cast<std::int64_t>(ItemHeaderSize) > m_ItemSize)
			return 0;

		const int Size = base::ReadInt32(m_pItemStart + ItemOffset + 4);
		if(Size < 0 || ItemOffset + static_cast<std::int64_t>(ItemHeaderSize) + Size > m_ItemSize)
			return 0;

		return Size;
	}

	const std::byte *CDataFileReader::GetData(int Index, std::size_t *pSize) const
	{
		if(pSize != nullptr)
			*pSize = 0;
		if(!m_Open || Index < 0 || Index >= m_NumRawData)
			return nullptr;

		std::optional<byte_vector> &Slot = m_aData[static_cast<std::size_t>(Index)];
		if(!Slot.has_value())
		{
			const std::int64_t Begin = base::ReadInt32(&m_pDataOffsets[Index]);
			const std::int64_t End = (Index + 1 < m_NumRawData) ? base::ReadInt32(&m_pDataOffsets[Index + 1]) : m_DataSize;
			if(Begin < 0 || End < Begin || End > m_DataSize)
				return nullptr;

			const std::byte *pCompressed = m_pDataStart + Begin;
			const std::size_t CompressedSize = static_cast<std::size_t>(End - Begin);
			byte_vector Data;

			if(m_Version >= 4)
			{
				if(m_pDataSizes == nullptr)
					return nullptr;
				const int UncompressedSize = base::ReadInt32(&m_pDataSizes[Index]);
				if(UncompressedSize < 0)
					return nullptr;
				if(UncompressedSize == 0)
				{
					Data.clear();
				}
				else if(m_Version >= 5)
				{
#if defined(UNITEED_ZSTD)
					if(!ZstdDecompress(pCompressed, CompressedSize, UncompressedSize, Data))
						return nullptr;
#else
					return nullptr;
#endif
				}
				else
				{
					Data.resize(static_cast<std::size_t>(UncompressedSize));
					uLongf Length = static_cast<uLongf>(UncompressedSize);
					if(uncompress(reinterpret_cast<Bytef *>(Data.data()), &Length, reinterpret_cast<const Bytef *>(pCompressed), static_cast<uLong>(CompressedSize)) != Z_OK || Length != static_cast<uLongf>(UncompressedSize))
						return nullptr;
				}
			}
			else
			{
				// version 3 stores the data blocks uncompressed
				Data.assign(pCompressed, pCompressed + CompressedSize);
			}

			Slot = std::move(Data);
		}

		if(pSize != nullptr)
			*pSize = Slot->size();
		return Slot->data();
	}

	// -------------------------------------------------------------------- write
	bool CDataFileWriter::Open(int Version, std::string *pError)
	{
		if(Version != 4 && Version != 5)
		{
			if(pError != nullptr)
				*pError = "the writer only supports datafile version 4 and 5";
			return false;
		}
#if !defined(UNITEED_ZSTD)
		if(Version == 5)
		{
			if(pError != nullptr)
				*pError = "this build has no zstd support";
			return false;
		}
#endif
		m_Version = Version;
		m_vItems.clear();
		m_vData.clear();
		return true;
	}

	int CDataFileWriter::AddItem(int Type, int ID, const void *pData, int Size)
	{
		if(Type < 0 || Type >= 0xffff || Size < 0)
			return -1;

		CItem Item;
		Item.m_Type = Type;
		Item.m_ID = ID & 0xffff;
		// the reference asserts on unaligned sizes, pad instead so a caller with
		// a short body cannot corrupt the container
		const int Padded = (Size + 3) & ~3;
		Item.m_Data.assign(static_cast<std::size_t>(Padded), std::byte(0));
		if(Size > 0 && pData != nullptr)
			std::memcpy(Item.m_Data.data(), pData, static_cast<std::size_t>(Size));

		m_vItems.push_back(std::move(Item));
		return static_cast<int>(m_vItems.size()) - 1;
	}

	int CDataFileWriter::AddData(const void *pData, int Size)
	{
		CData Data;
		Data.m_UncompressedSize = Size;

		if(Size <= 0)
		{
			Data.m_Compressed.clear();
		}
		else if(m_Version >= 5)
		{
#if defined(UNITEED_ZSTD)
			if(!ZstdCompress(pData, Size, m_CompressLevel, Data.m_Compressed))
			{
				LOG_ERROR("datafile", "zstd compression failed");
				return -1;
			}
#else
			return -1;
#endif
		}
		else
		{
			if(!Deflate(pData, Size, m_CompressLevel, Data.m_Compressed))
			{
				LOG_ERROR("datafile", "zlib compression failed");
				return -1;
			}
		}

		m_vData.push_back(std::move(Data));
		return static_cast<int>(m_vData.size()) - 1;
	}

	int CDataFileWriter::AddDataString(std::string_view String)
	{
		byte_vector Buffer(String.size() + 1, std::byte(0));
		std::memcpy(Buffer.data(), String.data(), String.size());
		return AddData(Buffer.data(), static_cast<int>(Buffer.size()));
	}

	bool CDataFileWriter::Finish(byte_vector &Out, std::string *pError)
	{
		Out.clear();

		// group the items by type, ascending, like the reference writer
		std::map<int, std::vector<int>> Types;
		for(int i = 0; i < static_cast<int>(m_vItems.size()); i++)
			Types[m_vItems[i].m_Type].push_back(i);

		int ItemSize = 0;
		for(const CItem &Item : m_vItems)
			ItemSize += static_cast<int>(Item.m_Data.size()) + static_cast<int>(ItemHeaderSize);
		int DataSize = 0;
		for(const CData &Data : m_vData)
			DataSize += static_cast<int>(Data.m_Compressed.size());

		// version 5 stores the whole item section as one compressed block
		byte_vector CompressedItems;
		int CompressedItemSize = 0;
		if(m_Version >= 5)
		{
#if defined(UNITEED_ZSTD)
			byte_vector ItemChunk(static_cast<std::size_t>(ItemSize));
			std::size_t Offset = 0;
			for(const auto &[Type, Indices] : Types)
			{
				for(const int Index : Indices)
				{
					const CItem &Item = m_vItems[Index];
					base::WriteInt32(ItemChunk.data() + Offset, (Type << 16) | Item.m_ID);
					base::WriteInt32(ItemChunk.data() + Offset + 4, static_cast<int>(Item.m_Data.size()));
					Offset += ItemHeaderSize;
					std::memcpy(ItemChunk.data() + Offset, Item.m_Data.data(), Item.m_Data.size());
					Offset += Item.m_Data.size();
				}
			}
			if(!ZstdCompress(ItemChunk.data(), ItemSize, m_CompressLevel, CompressedItems))
			{
				if(pError != nullptr)
					*pError = "failed to compress the item section";
				return false;
			}
			CompressedItemSize = static_cast<int>(CompressedItems.size());
#else
			if(pError != nullptr)
				*pError = "this build has no zstd support";
			return false;
#endif
		}

		const int NumItemTypes = static_cast<int>(Types.size());
		const int NumItems = static_cast<int>(m_vItems.size());
		const int NumDatas = static_cast<int>(m_vData.size());

		const int TypesSize = NumItemTypes * static_cast<int>(ItemTypeSize);
		const int OffsetSize = (NumItems + NumDatas + NumDatas) * 4;
		const int StoredItemSize = (m_Version >= 5) ? CompressedItemSize + 4 : ItemSize;
		const int FileSize = static_cast<int>(HeaderSize) + TypesSize + OffsetSize + StoredItemSize + DataSize;
		const int SwapSize = FileSize - DataSize;

		Out.reserve(static_cast<std::size_t>(FileSize) + 4);

		// header
		{
			std::byte Header[HeaderSize]{};
			std::memcpy(Header, "DATA", 4);
			base::WriteInt32(Header + 4, m_Version);
			base::WriteInt32(Header + 8, FileSize - 16);
			base::WriteInt32(Header + 12, SwapSize - 16);
			base::WriteInt32(Header + 16, NumItemTypes);
			base::WriteInt32(Header + 20, NumItems);
			base::WriteInt32(Header + 24, NumDatas);
			base::WriteInt32(Header + 28, ItemSize);
			base::WriteInt32(Header + 32, DataSize);
			Out.insert(Out.end(), Header, Header + HeaderSize);
		}

		const auto PushInt = [&Out](int Value) {
			std::byte Buffer[4];
			base::WriteInt32(Buffer, Value);
			Out.insert(Out.end(), Buffer, Buffer + 4);
		};

		// item types
		{
			int Count = 0;
			for(const auto &[Type, Indices] : Types)
			{
				PushInt(Type);
				PushInt(Count);
				PushInt(static_cast<int>(Indices.size()));
				Count += static_cast<int>(Indices.size());
			}
		}

		// item offsets
		{
			int Offset = 0;
			for(const auto &[Type, Indices] : Types)
			{
				for(const int Index : Indices)
				{
					PushInt(Offset);
					Offset += static_cast<int>(m_vItems[Index].m_Data.size()) + static_cast<int>(ItemHeaderSize);
				}
			}
		}

		// data offsets and uncompressed sizes
		{
			int Offset = 0;
			for(const CData &Data : m_vData)
			{
				PushInt(Offset);
				Offset += static_cast<int>(Data.m_Compressed.size());
			}
			for(const CData &Data : m_vData)
				PushInt(Data.m_UncompressedSize);
		}

		// item blob, or its compressed size and the compressed block
		if(m_Version >= 5)
		{
			PushInt(CompressedItemSize);
			Out.insert(Out.end(), CompressedItems.begin(), CompressedItems.end());
		}
		else
		{
			for(const auto &[Type, Indices] : Types)
			{
				for(const int Index : Indices)
				{
					const CItem &Item = m_vItems[Index];
					PushInt((Type << 16) | Item.m_ID);
					PushInt(static_cast<int>(Item.m_Data.size()));
					Out.insert(Out.end(), Item.m_Data.begin(), Item.m_Data.end());
				}
			}
		}

		// data
		for(const CData &Data : m_vData)
			Out.insert(Out.end(), Data.m_Compressed.begin(), Data.m_Compressed.end());

		if(static_cast<int>(Out.size()) != FileSize)
		{
			if(pError != nullptr)
				*pError = "internal error: the written size does not match the header";
			return false;
		}
		return true;
	}

	bool CDataFileWriter::Save(const storage::path &FileName, std::string *pError)
	{
		byte_vector Out;
		if(!Finish(Out, pError))
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
