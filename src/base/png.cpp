#include "png.h"

#include <cstring>

#include <zlib.h>

#include "base/endian.h"

namespace base
{
	namespace
	{
		void PushU32(storage::byte_vector &Out, std::uint32_t Value)
		{
			std::byte Buffer[4];
			base::WriteInt32(Buffer, static_cast<std::int32_t>(Value));
			// PNG stores integers big endian, which is the opposite of the map
			Out.push_back(Buffer[3]);
			Out.push_back(Buffer[2]);
			Out.push_back(Buffer[1]);
			Out.push_back(Buffer[0]);
		}

		void PushChunk(storage::byte_vector &Out, const char Type[4], const storage::byte_vector &Data)
		{
			PushU32(Out, static_cast<std::uint32_t>(Data.size()));
			const std::size_t Start = Out.size();
			Out.insert(Out.end(), reinterpret_cast<const std::byte *>(Type), reinterpret_cast<const std::byte *>(Type) + 4);
			Out.insert(Out.end(), Data.begin(), Data.end());
			const std::uint32_t Crc = static_cast<std::uint32_t>(crc32(0L,
				reinterpret_cast<const Bytef *>(Out.data() + Start), static_cast<uInt>(4 + Data.size())));
			PushU32(Out, Crc);
		}
	} // namespace

	bool WritePng(const storage::path &FileName, int Width, int Height, const storage::byte_vector &Pixels)
	{
		if(Width <= 0 || Height <= 0)
			return false;
		if(Pixels.size() < static_cast<std::size_t>(Width) * Height * 4)
			return false;

		// raw scanlines, every one prefixed with filter type 0
		storage::byte_vector Raw;
		Raw.reserve(static_cast<std::size_t>(Height) * (static_cast<std::size_t>(Width) * 4 + 1));
		for(int y = 0; y < Height; y++)
		{
			Raw.push_back(std::byte(0));
			const std::byte *pRow = Pixels.data() + static_cast<std::size_t>(y) * Width * 4;
			Raw.insert(Raw.end(), pRow, pRow + static_cast<std::size_t>(Width) * 4);
		}

		uLongf CompressedSize = compressBound(static_cast<uLong>(Raw.size()));
		storage::byte_vector Compressed(CompressedSize);
		if(compress2(reinterpret_cast<Bytef *>(Compressed.data()), &CompressedSize,
			   reinterpret_cast<const Bytef *>(Raw.data()), static_cast<uLong>(Raw.size()), 6) != Z_OK)
			return false;
		Compressed.resize(CompressedSize);

		storage::byte_vector Out;
		const std::byte Signature[8] = {std::byte(0x89), std::byte('P'), std::byte('N'), std::byte('G'),
			std::byte(0x0d), std::byte(0x0a), std::byte(0x1a), std::byte(0x0a)};
		Out.insert(Out.end(), Signature, Signature + 8);

		storage::byte_vector Header;
		PushU32(Header, static_cast<std::uint32_t>(Width));
		PushU32(Header, static_cast<std::uint32_t>(Height));
		Header.push_back(std::byte(8)); // bit depth
		Header.push_back(std::byte(6)); // colour type RGBA
		Header.push_back(std::byte(0)); // compression
		Header.push_back(std::byte(0)); // filter
		Header.push_back(std::byte(0)); // interlace
		PushChunk(Out, "IHDR", Header);
		PushChunk(Out, "IDAT", Compressed);
		PushChunk(Out, "IEND", {});

		return storage::write(FileName, Out);
	}
} // namespace base
