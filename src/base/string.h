#ifndef BASE_STRING_H
#define BASE_STRING_H

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace base
{
	inline std::string Format(const char *pFormat, ...)
	{
		char aBuffer[1024];
		va_list Args;
		va_start(Args, pFormat);
		std::vsnprintf(aBuffer, sizeof(aBuffer), pFormat, Args);
		va_end(Args);
		return aBuffer;
	}

	inline bool StartsWith(const std::string &String, const std::string &Prefix)
	{
		return String.size() >= Prefix.size() && String.compare(0, Prefix.size(), Prefix) == 0;
	}

	inline bool EndsWith(const std::string &String, const std::string &Suffix)
	{
		return String.size() >= Suffix.size() && String.compare(String.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
	}

	inline std::string ToLower(std::string String)
	{
		for(char &c : String)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return String;
	}

	inline std::string Trim(const std::string &String)
	{
		const auto Begin = String.find_first_not_of(" \t\r\n");
		if(Begin == std::string::npos)
			return "";
		const auto End = String.find_last_not_of(" \t\r\n");
		return String.substr(Begin, End - Begin + 1);
	}

	inline std::vector<std::string> Split(const std::string &String, char Separator)
	{
		std::vector<std::string> Parts;
		std::size_t Start = 0;
		while(Start <= String.size())
		{
			const std::size_t Pos = String.find(Separator, Start);
			if(Pos == std::string::npos)
			{
				Parts.push_back(String.substr(Start));
				break;
			}
			Parts.push_back(String.substr(Start, Pos - Start));
			Start = Pos + 1;
		}
		return Parts;
	}

	inline std::string ReplaceAll(std::string String, const std::string &From, const std::string &To)
	{
		if(From.empty())
			return String;
		std::size_t Pos = 0;
		while((Pos = String.find(From, Pos)) != std::string::npos)
		{
			String.replace(Pos, From.size(), To);
			Pos += To.size();
		}
		return String;
	}

	/*
		The map format stores short names in int arrays. Every byte is biased by
		+128 so that a signed char round trips, four bytes go into one int,
		most significant byte first, and the last byte of the array is unused.
		This is exactly the reference StrToInts/IntsToStr pair (gamecore.cpp),
		which matters because the strings of existing maps were written with it.
	*/
	inline void IntsToStr(const int *pInts, int NumInts, std::string &Out)
	{
		Out.clear();
		for(int i = 0; i < NumInts; i++)
		{
			const int Value = pInts[i];
			for(int Byte = 0; Byte < 4; Byte++)
			{
				const char c = static_cast<char>(((Value >> (24 - Byte * 8)) & 0xff) - 128);
				if(c == '\0')
					return;
				Out.push_back(c);
			}
		}
	}

	inline std::string IntsToStr(const int *pInts, int NumInts)
	{
		std::string Result;
		IntsToStr(pInts, NumInts, Result);
		return Result;
	}

	inline void StrToInts(int *pInts, int NumInts, std::string_view String)
	{
		std::memset(pInts, 0, static_cast<std::size_t>(NumInts) * sizeof(int));
		const std::size_t Size = String.size() + 1;
		for(int i = 0; i < NumInts; i++)
		{
			char aBuf[4] = {0, 0, 0, 0};
			for(std::size_t c = 0; c < 4 && static_cast<std::size_t>(i) * 4 + c < Size; c++)
			{
				const std::size_t Index = static_cast<std::size_t>(i) * 4 + c;
				aBuf[c] = Index < String.size() ? String[Index] : '\0';
			}
			const int Value = ((aBuf[0] + 128) << 24) | ((aBuf[1] + 128) << 16) |
				((aBuf[2] + 128) << 8) | (aBuf[3] + 128);
			pInts[i] = Value;
		}
		// the last byte is always zero and unused in this format
		pInts[NumInts - 1] &= 0xFFFFFF00;
	}
} // namespace base

#endif // BASE_STRING_H
