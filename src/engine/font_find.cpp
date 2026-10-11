#include "font_find.h"

#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <system_error>
#include <vector>

#include "base/logger.h"
#include "base/storage.h"

#if defined(_WIN32)
	// windows.h pulls in min/max macros that clash with the rest of the project.
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <cstdio>
#endif

namespace engine::fonts
{
	namespace
	{
#if defined(_WIN32)
		// defined further down, used by Exists() and the registry reader
		std::wstring ToWide(const std::string &Text);
#endif

		// Expands a leading "~" so an override can be written as a path.
		std::string ExpandPath(std::string Path)
		{
			if(Path.empty())
				return Path;
			if(Path[0] == '~' && (Path.size() == 1 || Path[1] == '/' || Path[1] == '\\'))
			{
				const char *pHome = std::getenv("HOME");
#if defined(_WIN32)
				if(pHome == nullptr || *pHome == '\0')
					pHome = std::getenv("USERPROFILE");
#endif
				if(pHome != nullptr && *pHome != '\0')
					Path = std::string(pHome) + Path.substr(1);
			}
			return Path;
		}

#if defined(_WIN32)
		std::wstring ToWide(const std::string &Text)
		{
			if(Text.empty())
				return {};
			const int Size = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), nullptr, 0);
			if(Size <= 0)
				return {};
			std::wstring Wide(static_cast<std::size_t>(Size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), Wide.data(), Size);
			return Wide;
		}

		std::string ToUtf8(const std::wstring &Text)
		{
			if(Text.empty())
				return {};
			const int Size =
				WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
			if(Size <= 0)
				return {};
			std::string Utf8(static_cast<std::size_t>(Size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), Utf8.data(), Size, nullptr, nullptr);
			return Utf8;
		}

		// %WINDIR%\Fonts, plus the per user directory; the literal path is the
		// last resort for a Windows installed somewhere other than C:.
		std::vector<std::string> WindowsFontDirectories()
		{
			std::vector<std::string> Directories;
			const auto Add = [&Directories](const std::string &Directory) {
				// WINDIR and SystemRoot usually hold the same value
				for(const std::string &Existing : Directories)
					if(Existing == Directory)
						return;
				Directories.push_back(Directory);
			};
			const auto AddVariable = [&Add](const char *pVariable, const char *pSuffix) {
				const char *pValue = std::getenv(pVariable);
				if(pValue != nullptr && *pValue != '\0')
					Add(std::string(pValue) + pSuffix);
			};
			AddVariable("WINDIR", "\\Fonts");
			AddVariable("SystemRoot", "\\Fonts");
			AddVariable("LOCALAPPDATA", "\\Microsoft\\Windows\\Fonts");
			Add("C:\\Windows\\Fonts");
			return Directories;
		}

		// The Fonts key is the only record of a font installed without copying a
		// file into a font directory.
		std::vector<std::string> RegistryFontFiles()
		{
			std::vector<std::string> Files;
			HKEY hKey = nullptr;
			if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &hKey) !=
				ERROR_SUCCESS)
				return Files;

			DWORD Index = 0;
			// RegEnumValueW needs a place for the value name, which is not wanted
			wchar_t aValueName[512];
			BYTE aData[1024];
			while(true)
			{
				DWORD NameLength = static_cast<DWORD>(std::size(aValueName));
				DWORD DataLength = sizeof(aData);
				DWORD Type = 0;
				const LONG Result = RegEnumValueW(hKey, Index, aValueName, &NameLength, nullptr, &Type, aData, &DataLength);
				Index++;
				if(Result == ERROR_NO_MORE_ITEMS)
					break;
				// a value that did not fit is skipped; the index still advanced
				if(Result != ERROR_SUCCESS)
					continue;
				if((Type == REG_SZ || Type == REG_EXPAND_SZ) && DataLength > sizeof(wchar_t))
				{
					// DataLength counts bytes and includes the terminator
					std::wstring Value(reinterpret_cast<const wchar_t *>(aData), DataLength / sizeof(wchar_t));
					while(!Value.empty() && Value.back() == L'\0')
						Value.pop_back();
					if(!Value.empty())
					{
						// A bare file name is completed with the font directories;
						// a drive or UNC path is already complete.
						const bool DriveAbsolute =
							Value.size() > 2 && Value[1] == L':' && (Value[2] == L'\\' || Value[2] == L'/');
						const bool UncAbsolute = Value.size() > 2 && Value[0] == L'\\' && Value[1] == L'\\';
						if(DriveAbsolute || UncAbsolute)
							Files.push_back(ToUtf8(Value));
						else
						{
							const std::string Name = ToUtf8(Value);
							for(const std::string &Directory : WindowsFontDirectories())
								Files.push_back(Directory + "\\" + Name);
						}
					}
				}
			}
			RegCloseKey(hKey);
			return Files;
		}
#else
		// asks fontconfig for the file of a pattern; the tool may be absent
		std::string FontconfigMatch(const char *pPattern)
		{
			const std::string Command = std::string("fc-match -f '%{file}' '") + pPattern + "' 2>/dev/null";
			std::FILE *pPipe = popen(Command.c_str(), "r");
			if(pPipe == nullptr)
				return {};
			char aBuffer[1024] = {0};
			const std::size_t Read = std::fread(aBuffer, 1, sizeof(aBuffer) - 1, pPipe);
			pclose(pPipe);
			if(Read == 0)
				return {};
			std::string Path(aBuffer, Read);
			while(!Path.empty() && (Path.back() == '\n' || Path.back() == '\r'))
				Path.pop_back();
			return Exists(Path) ? Path : std::string{};
		}
#endif

		// the first candidate that exists, so the list order is the preference
		std::string FirstExisting(const std::vector<std::string> &Candidates)
		{
			for(const std::string &Path : Candidates)
				if(Exists(Path))
					return Path;
			return {};
		}

		std::vector<std::string> PlatformCandidates(face Face)
		{
			std::vector<std::string> Paths;

#if defined(_WIN32)
			// The file name of each face, best first. The preference is expressed
			// by the name list, so the first name that exists anywhere wins.
			std::vector<std::string> Names;
			switch(Face)
			{
			case face::primary:
				Names = {"segoeui.ttf", "segoeuib.ttf", "tahoma.ttf", "verdana.ttf", "arial.ttf", "calibri.ttf"};
				break;
			case face::bold:
				Names = {"segoeuib.ttf", "segoeuisl.ttf", "tahomabd.ttf", "verdanab.ttf", "arialbd.ttf", "calibrib.ttf"};
				break;
			case face::cjk:
				// YaHei, then JhengHei, SimSun and DengXian on trimmed installs
				Names = {"msyh.ttc", "msyh.ttf", "msjh.ttc", "simsun.ttc", "Deng.ttf", "msgothic.ttc"};
				break;
			}
			// the registry is consulted too: it also records fonts outside a
			// font directory
			const std::vector<std::string> Registry = RegistryFontFiles();
			for(const std::string &Name : Names)
			{
				for(const std::string &Directory : WindowsFontDirectories())
					Paths.push_back(Directory + "\\" + Name);
				for(const std::string &File : Registry)
				{
					const std::size_t Slash = File.find_last_of("\\/");
					const std::string Base = Slash == std::string::npos ? File : File.substr(Slash + 1);
					if(Base == Name)
						Paths.push_back(File);
				}
			}
#elif defined(__APPLE__)
			switch(Face)
			{
			case face::primary:
				Paths = {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc",
					"/System/Library/Fonts/Geneva.ttf", "/Library/Fonts/Arial.ttf"};
				break;
			case face::bold:
				Paths = {"/System/Library/Fonts/HelveticaNeue.ttc", "/System/Library/Fonts/Helvetica.ttc",
					"/Library/Fonts/Arial Bold.ttf"};
				break;
			case face::cjk:
				Paths = {"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/STHeiti Light.ttc",
					"/Library/Fonts/Arial Unicode.ttf"};
				break;
			}
#else
			// fontconfig knows the distribution's layout, so it goes first
			switch(Face)
			{
			case face::primary:
			{
				const std::string Matched = FontconfigMatch("sans");
				if(!Matched.empty())
					Paths.push_back(Matched);
				for(const char *pPath : {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
						"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
						"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/TTF/Hack-Regular.ttf"})
					Paths.push_back(pPath);
				break;
			}
			case face::bold:
			{
				const std::string Matched = FontconfigMatch("sans:bold");
				if(!Matched.empty())
					Paths.push_back(Matched);
				for(const char *pPath : {"/usr/share/fonts/noto/NotoSans-Bold.ttf",
						"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
						"/usr/share/fonts/TTF/Hack-Bold.ttf"})
					Paths.push_back(pPath);
				break;
			}
			case face::cjk:
			{
				const std::string Matched = FontconfigMatch("sans:lang=zh-cn");
				if(!Matched.empty())
					Paths.push_back(Matched);
				for(const char *pPath : {"/usr/share/fonts/noto/NotoSansCJK-Regular.ttc",
						"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
						"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"})
					Paths.push_back(pPath);
				break;
			}
			}
#endif
			return Paths;
		}
	} // namespace

	bool Exists(const std::string &Path)
	{
		if(Path.empty())
			return false;
#if defined(_WIN32)
		const std::wstring Wide = ToWide(Path);
		if(Wide.empty())
			return false;
		const DWORD Attributes = GetFileAttributesW(Wide.c_str());
		return Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
		std::error_code Error;
		return std::filesystem::is_regular_file(storage::path(Path), Error) && !Error;
#endif
	}

	std::vector<std::string> Candidates(face Face, const std::string &Override)
	{
		std::vector<std::string> Paths;
		if(!Override.empty())
			Paths.push_back(ExpandPath(Override));
		for(std::string &Path : PlatformCandidates(Face))
			Paths.push_back(std::move(Path));
		return Paths;
	}

	result Resolve(const request &Request)
	{
		result Result;

		// an override that does not exist warns, so a typo is not mistaken for
		// the setting being ignored
		const auto Pick = [](face Face, const std::string &Override, const char *pLabel) {
			if(!Override.empty() && !Exists(ExpandPath(Override)))
				LOG_WARN("fonts", "the configured %s font '%s' does not exist, looking for a system one", pLabel,
					Override.c_str());
			const std::string Path = FirstExisting(Candidates(Face, Override));
			if(Path.empty())
				LOG_WARN("fonts", "no %s font found", pLabel);
			else
				LOG_INFO("fonts", "%s font: %s", pLabel, Path.c_str());
			return Path;
		};

		Result.m_Primary = Pick(face::primary, Request.m_Primary, "interface");
		Result.m_Bold = Pick(face::bold, Request.m_Bold, "bold");
		Result.m_Cjk = Pick(face::cjk, Request.m_Cjk, "CJK fallback");
		return Result;
	}
} // namespace engine::fonts
