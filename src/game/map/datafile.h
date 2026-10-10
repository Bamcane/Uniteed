#ifndef GAME_MAP_DATAFILE_H
#define GAME_MAP_DATAFILE_H

/*
	Reader and writer of the CDataFile container a Teeworlds map is.

	Layout (all integers little endian 32 bit), from
	engine/shared/datafile.cpp of the reference implementations:

		header        magic "DATA", version, size, swaplen,
		              num item types, num items, num raw data,
		              item blob size, data blob size           (36 bytes)
		item types    numItemTypes * {int type; int start; int num;}
		item offsets  numItems * int
		data offsets  numRawData * int
		data sizes    numRawData * int      (version >= 4, uncompressed size)
		item size     int                   (version >= 5, compressed item size)
		items         item blob, every item {int typeAndID; int size; body}
		data          compressed data blocks

	Version 3 (Teeworlds 0.5) has no data sizes and stores its data blocks
	uncompressed. Version 4 (0.6, 0.7 and DDRaceNetwork) adds the uncompressed
	data sizes and compresses every data block with zlib. Version 5 (0.8)
	compresses the whole item blob and every data block with zstd, and stores
	the compressed size of the item blob in the last int of the table area.

	Reading an item body returns a view into the file. Reading a data block
	inflates it once and keeps the result.
*/

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <sugar_types.h>

#include "base/storage.h"

namespace game
{
	using byte_vector = storage::byte_vector;

	struct CDataFileItemTypeInfo
	{
		int m_Type = 0;
		int m_Start = 0;
		int m_Num = 0;
	};

	class CDataFileReader
	{
	public:
		bool Open(const byte_vector &Bytes, std::string *pError = nullptr);
		void Close();

		bool IsOpen() const { return m_Open; }
		int Version() const { return m_Version; }
		int NumItems() const { return m_NumItems; }
		int NumRawData() const { return m_NumRawData; }

		// the run of items of one type, false when the map has no such type
		bool GetType(int Type, int *pStart, int *pNum) const;

		// the item body, nullptr when the index is out of range. Type and ID are
		// the two halves of the item header.
		const std::byte *GetItem(int Index, int *pType = nullptr, int *pID = nullptr) const;
		int GetItemSize(int Index) const;

		// the inflated data block, nullptr on a bad index or corrupt data. The
		// pointer stays valid until Close.
		const std::byte *GetData(int Index, std::size_t *pSize = nullptr) const;

		// every data block inflated, used by the map loader to preserve the
		// blocks unknown items point at
		const std::vector<std::optional<byte_vector>> &AllData() const { return m_aData; }

	private:
		byte_vector m_Bytes;
		bool m_Open = false;
		int m_Version = 0;

		int m_NumItemTypes = 0;
		int m_NumItems = 0;
		int m_NumRawData = 0;
		int m_ItemSize = 0;
		int m_DataSize = 0;
		std::size_t m_ItemStartOffset = 0;
		std::size_t m_DataStartOffset = 0;

		// views into m_Bytes for the tables that are read directly
		const int *m_pItemOffsets = nullptr;
		const int *m_pDataOffsets = nullptr;
		const int *m_pDataSizes = nullptr;
		const std::byte *m_pItemStart = nullptr;
		const std::byte *m_pDataStart = nullptr;

		// the version 5 item blob is inflated once on Open into this buffer
		byte_vector m_ItemBuffer;

		// inflated data blocks, filled on demand
		mutable std::vector<std::optional<byte_vector>> m_aData;
	};

	class CDataFileWriter
	{
	public:
		struct CItem
		{
			int m_Type = 0;
			int m_ID = 0;
			byte_vector m_Data;
		};

		struct CData
		{
			byte_vector m_Compressed;
			int m_UncompressedSize = 0;
		};

		CDataFileWriter() = default;

		// version is 4 or 5
		bool Open(int Version, std::string *pError = nullptr);

		// the item body is copied, Size must be a multiple of 4
		int AddItem(int Type, int ID, const void *pData, int Size);

		// compressed with zlib (v4) or zstd (v5)
		int AddData(const void *pData, int Size);

		// zlib and zstd both work on bytes, the reference wrapper only exists
		// for big endian hosts
		int AddDataSwapped(const void *pData, int Size) { return AddData(pData, Size); }

		// a NUL terminated string as its own data block
		int AddDataString(std::string_view String);

		int NumItems() const { return static_cast<int>(m_vItems.size()); }
		int NumData() const { return static_cast<int>(m_vData.size()); }

		// writes the container, item types are grouped and sorted like the
		// reference writer does
		bool Save(const storage::path &FileName, std::string *pError = nullptr);

		// the serialised container, used by the .unitwp package
		bool Finish(byte_vector &Out, std::string *pError = nullptr);

	private:
		int m_Version = 4;
		int m_CompressLevel = 9;
		std::vector<CItem> m_vItems;
		std::vector<CData> m_vData;
	};
} // namespace game

#endif // GAME_MAP_DATAFILE_H
