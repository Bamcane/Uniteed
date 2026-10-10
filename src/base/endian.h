#ifndef BASE_ENDIAN_H
#define BASE_ENDIAN_H

/*
	The map format is a stream of little endian 32 bit integers. The byte order
	conversion itself comes from libsugarx (sugar_endian.h), this header only
	adds the unaligned peeks and pokes the map layer needs.
*/

#include <cstdint>
#include <cstring>

#include <sugar_endian.h>
#include <sugar_types.h>

namespace base
{
	using libsugarx::is_big_endian;
	using libsugarx::is_little_endian;
	using libsugarx::to_big_endian;
	using libsugarx::to_little_endian;

	inline std::int32_t ReadInt32(const void *pData)
	{
		std::int32_t Value;
		std::memcpy(&Value, pData, sizeof(Value));
		return to_little_endian(Value);
	}

	inline std::uint32_t ReadUInt32(const void *pData)
	{
		std::uint32_t Value;
		std::memcpy(&Value, pData, sizeof(Value));
		return to_little_endian(Value);
	}

	inline std::int16_t ReadInt16(const void *pData)
	{
		std::int16_t Value;
		std::memcpy(&Value, pData, sizeof(Value));
		return to_little_endian(Value);
	}

	inline void WriteInt32(void *pData, std::int32_t Value)
	{
		Value = to_little_endian(Value);
		std::memcpy(pData, &Value, sizeof(Value));
	}

	inline void WriteInt16(void *pData, std::int16_t Value)
	{
		Value = to_little_endian(Value);
		std::memcpy(pData, &Value, sizeof(Value));
	}

	// the item bodies of a map are stored as int structs; on a little endian
	// host they can be reinterpreted directly
	template<typename T>
	inline T LoadStruct(const void *pData)
	{
		T Value{};
		std::memcpy(&Value, pData, sizeof(T));
		if constexpr(is_big_endian())
		{
			std::byte *pBytes = reinterpret_cast<std::byte *>(&Value);
			for(std::size_t i = 0; i + 4 <= sizeof(T); i += 4)
			{
				std::uint32_t Word;
				std::memcpy(&Word, pBytes + i, sizeof(Word));
				Word = to_little_endian(Word);
				std::memcpy(pBytes + i, &Word, sizeof(Word));
			}
		}
		return Value;
	}

	template<typename T>
	inline void StoreStruct(void *pData, const T &Value)
	{
		T Copy = Value;
		if constexpr(is_big_endian())
		{
			std::byte *pBytes = reinterpret_cast<std::byte *>(&Copy);
			for(std::size_t i = 0; i + 4 <= sizeof(T); i += 4)
			{
				std::uint32_t Word;
				std::memcpy(&Word, pBytes + i, sizeof(Word));
				Word = to_little_endian(Word);
				std::memcpy(pBytes + i, &Word, sizeof(Word));
			}
		}
		std::memcpy(pData, &Copy, sizeof(T));
	}
} // namespace base

#endif // BASE_ENDIAN_H
