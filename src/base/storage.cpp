#include "storage.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include <sugar_path.h>

#include "define.h"
#include "logger.h"

namespace storage
{
	namespace
	{
		path g_DataDirectory;
		std::vector<path> g_vResourcePaths;

		path ExecutableDirectory()
		{
#if defined(__linux__)
			std::error_code Error;
			const path Self = std::filesystem::read_symlink("/proc/self/exe", Error);
			if(!Error)
				return Self.parent_path();
#elif defined(_WIN32)
			// the data directory baked in at build time is always available
#endif
			return std::filesystem::current_path();
		}
	} // namespace

	path GetAppDataDirectory()
	{
		// libsugarx::get_data_home_directory() already implements the
		// LOCALAPPDATA / XDG_DATA_HOME / ~/.local/share / Application Support
		// order of the reference projects
		return libsugarx::get_data_home_directory() / GAME_NAME;
	}

	namespace
	{
		// an environment override, an absolute path the caller has already
		// resolved, or the application data directory plus the leaf name
		path SubDirectory(const char *pEnv, const char *pLeaf)
		{
			if(const char *pValue = std::getenv(pEnv))
				if(*pValue != '\0')
					return path(pValue);
			return GetAppDataDirectory() / pLeaf;
		}
	} // namespace

	path GetProjectsDirectory() { return SubDirectory("UNITEED_PROJECTS_DIR", "projects"); }
	path GetExportDirectory() { return SubDirectory("UNITEED_EXPORT_DIR", "export"); }

	path GetExportDirectoryForTarget(const std::string &TargetId)
	{
		const path Directory = GetExportDirectory() / TargetId;
		std::error_code Error;
		std::filesystem::create_directories(Directory, Error);
		return Directory;
	}

	path ExportPath(const std::string &TargetId, std::string_view ProjectName)
	{
		std::string Name = SanitiseName(ProjectName);
		// a caller that hands in "demo.unitwp" means the project demo, not a
		// file named "demo.unitwp.ddnet06.map"
		const std::size_t Dot = Name.find_last_of('.');
		if(Dot != std::string::npos && Dot > 0)
			Name = Name.substr(0, Dot);
		return GetExportDirectoryForTarget(TargetId) / (Name + "." + TargetId + ".map");
	}

	path PackageExportPath(const std::string &TargetId, std::string_view ProjectName)
	{
		std::string Name = SanitiseName(ProjectName);
		const std::size_t Dot = Name.find_last_of('.');
		if(Dot != std::string::npos && Dot > 0)
			Name = Name.substr(0, Dot);
		return GetExportDirectoryForTarget(TargetId) / (Name + "." + TargetId + ".unitwp");
	}

	std::string SanitiseName(std::string_view Name)
	{
		std::string Out;
		Out.reserve(Name.size());
		for(const char Character : Name)
		{
			const unsigned char Byte = static_cast<unsigned char>(Character);
			// keep it readable: only the bytes a filesystem may refuse go
			if(Byte < 0x20 || Byte == 0x7f || Character == '/' || Character == '\\' ||
				Character == ':' || Character == '*' || Character == '?' || Character == '"' ||
				Character == '<' || Character == '>' || Character == '|')
				Out.push_back('_');
			else
				Out.push_back(Character);
		}
		// a name that was nothing but separators, or a dot only name, is not a
		// useful file name on any platform
		const auto Begin = Out.find_first_not_of(" \t\r\n");
		if(Begin == std::string::npos)
			return "untitled";
		const auto End = Out.find_last_not_of(" \t\r\n");
		const std::string Trimmed = Out.substr(Begin, End - Begin + 1);
		if(Trimmed == "." || Trimmed == "..")
			return "untitled";
		return Trimmed;
	}

	const path &GetDataDirectory()
	{
		if(!g_DataDirectory.empty())
			return g_DataDirectory;

		if(const char *pEnv = std::getenv("UNITEED_DATA_DIR"))
		{
			if(*pEnv != '\0' && std::filesystem::exists(pEnv))
			{
				g_DataDirectory = pEnv;
				return g_DataDirectory;
			}
		}

		// the installed layout: <exe dir>/../share/uniteed
		const path Installed = ExecutableDirectory() / ".." / "share" / GAME_NAME;
		if(std::filesystem::exists(Installed))
		{
			g_DataDirectory = std::filesystem::weakly_canonical(Installed);
			return g_DataDirectory;
		}

		g_DataDirectory = UNITEED_DATA_DIR;
		return g_DataDirectory;
	}

	void SetDataDirectory(const path &Directory) { g_DataDirectory = Directory; }

	void SetResourcePaths(std::vector<path> Paths) { g_vResourcePaths = std::move(Paths); }
	const std::vector<path> &ResourcePaths() { return g_vResourcePaths; }

	void AddResourcePath(const path &Directory, bool Front)
	{
		if(Front)
			g_vResourcePaths.insert(g_vResourcePaths.begin(), Directory);
		else
			g_vResourcePaths.push_back(Directory);
	}

	path FindResource(const path &Relative)
	{
		std::error_code Error;
		for(const path &Root : g_vResourcePaths)
		{
			const path Candidate = Root / Relative;
			if(std::filesystem::is_regular_file(Candidate, Error))
				return Candidate;
		}
		return {};
	}

	path VersionDirectory(const std::string &Version)
	{
		return GetDataDirectory() / "versions" / Version;
	}

	std::optional<byte_vector> read(const path &target)
	{
		std::ifstream stream(target, std::ios::binary);
		if(!stream.is_open())
			return std::nullopt;

		stream.seekg(0, std::ios::end);
		const std::streamoff size = stream.tellg();
		if(size < 0)
			return std::nullopt;

		stream.seekg(0, std::ios::beg);
		if(!stream)
			return std::nullopt;

		byte_vector data(static_cast<std::size_t>(size));
		if(size > 0)
		{
			stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
			if(stream.gcount() != static_cast<std::streamsize>(size))
				return std::nullopt;
		}

		return data;
	}

	std::optional<std::string> read_text(const path &target)
	{
		const std::optional<byte_vector> Data = read(target);
		if(!Data.has_value())
			return std::nullopt;
		return std::string(reinterpret_cast<const char *>(Data->data()), Data->size());
	}

	bool exists(const path &target)
	{
		std::error_code Error;
		return std::filesystem::exists(target, Error);
	}

	bool is_file(const path &target)
	{
		std::error_code Error;
		return std::filesystem::is_regular_file(target, Error);
	}

	std::vector<path> list_directory(const path &Directory, const std::string &Extension)
	{
		std::vector<path> Files;
		std::error_code Error;
		for(const auto &Entry : std::filesystem::directory_iterator(Directory, Error))
		{
			if(!Entry.is_regular_file())
				continue;
			const std::string Name = Entry.path().filename().string();
			if(!Name.empty() && Name[0] == '.')
				continue;
			if(!Extension.empty() && Entry.path().extension() != Extension)
				continue;
			Files.push_back(Entry.path());
		}
		std::sort(Files.begin(), Files.end());
		return Files;
	}

	std::vector<path> list_subdirectories(const path &Directory)
	{
		std::vector<path> Directories;
		std::error_code Error;
		for(const auto &Entry : std::filesystem::directory_iterator(Directory, Error))
		{
			if(!Entry.is_directory())
				continue;
			const std::string Name = Entry.path().filename().string();
			if(!Name.empty() && Name[0] == '.')
				continue;
			Directories.push_back(Entry.path());
		}
		std::sort(Directories.begin(), Directories.end());
		return Directories;
	}

	bool write(const path &target, const_data_span Data)
	{
		try
		{
			filewriter Writer(target, mode::binary, false);
			Writer.write(Data);
			return !Writer.error();
		}
		catch(const std::runtime_error &Error)
		{
			LOG_ERROR("storage", "failed to write '%s': %s", target.c_str(), Error.what());
			return false;
		}
	}

	bool write_text(const path &target, std::string_view Text)
	{
		return write(target, const_data_span(reinterpret_cast<const std::byte *>(Text.data()), Text.size()));
	}

	filewriter::filewriter(const path &target, mode filemode, bool append)
	{
		if(std::filesystem::is_directory(target))
			throw std::runtime_error("target is a directory");
		if(target.has_parent_path())
		{
			if(!std::filesystem::exists(target.parent_path()))
				std::filesystem::create_directories(target.parent_path());
			if(!std::filesystem::is_directory(target.parent_path()))
				throw std::runtime_error("target's parent directory is not a directory");
		}

		std::ios::openmode openmode = std::ios::out;
		if(filemode == mode::binary)
			openmode |= std::ios::binary;
		if(append)
			openmode |= std::ios::app;
		else
			openmode |= std::ios::trunc;
		m_Stream = std::ofstream(target, openmode);
		if(!m_Stream)
			throw std::runtime_error("failed to write file");
	}

	void filewriter::write(std::string_view string)
	{
		if(!is_open())
			throw std::runtime_error("file has been closed");
		m_Stream.write(string.data(), static_cast<std::streamsize>(string.size()));
	}

	void filewriter::write(const_data_span bytes)
	{
		if(!is_open())
			throw std::runtime_error("file has been closed");
		m_Stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

	void filewriter::write(const void *pData, std::size_t Size)
	{
		if(!is_open())
			throw std::runtime_error("file has been closed");
		m_Stream.write(static_cast<const char *>(pData), static_cast<std::streamsize>(Size));
	}

	void filewriter::close()
	{
		if(m_Stream.is_open())
			m_Stream.close();
	}

	void filewriter::flush() { m_Stream.flush(); }
	bool filewriter::error() const { return m_Stream.fail(); }
	bool filewriter::is_open() const { return m_Stream.is_open(); }
} // namespace storage
