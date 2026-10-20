#ifndef BASE_STORAGE_H
#define BASE_STORAGE_H

/*
	Filesystem access, modelled on TWICE's src/base/storage: a free function
	module in the storage namespace, std::filesystem::path everywhere, a
	std::optional<std::vector<std::byte>> read() and a small filewriter class
	that creates the parent directories and throws on failure.

	On top of that Uniteed needs the resource search order that the reference
	projects get out of Teeworlds' IStorage: the map images of a version must
	win over the shared ones, and the 0.6 and 0.7 tiles of the same name live in
	different directories. That is SetResourcePaths() + FindResource().
*/

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sugar_types.h>

namespace storage
{
	using std::filesystem::path;
	using libsugarx::const_data_span;
	using libsugarx::data_span;
	using byte_vector = std::vector<std::byte>;

	/*
		A path as the UTF-8 text a log line or a message wants. A path's native
		string type is wchar_t on Windows, so path::c_str() there is a
		const wchar_t* that std::format has no formatter for; u8string() is a
		plain std::string on every platform, so this one spelling compiles and
		prints the same everywhere.
	*/
	inline std::string path_text(const path &Path)
	{
		const std::u8string Text = Path.u8string();
		return std::string(Text.begin(), Text.end());
	}
	// the same for a path that was already turned into a std::string, so a log
	// line can take either spelling without the caller having to care
	inline const std::string &path_text(const std::string &Path) { return Path; }

	/*
	method GetAppDataDirectory
	returns AppData directory path;
	on Linux: ~/.local/share/uniteed
	*/
	path GetAppDataDirectory();

	/*
		The two directories the editor keeps its own work in, both under the
		application data directory so a project never has to be saved by hand:

			projects/   the .unitwp projects, created on first use
			export/     every map this editor exports, one directory per target

		Both can be pointed elsewhere with UNITEED_PROJECTS_DIR and
		UNITEED_EXPORT_DIR, which is what a test or a portable install wants.
		The default paths are also the ones the command line and the agent mode
		report, so an agent never has to guess where a file landed.
	*/
	path GetProjectsDirectory();
	path GetExportDirectory();
	// the directory an export of this target goes into, created on demand
	path GetExportDirectoryForTarget(const std::string &TargetId);
	// <export>/<target>/<name>.<target>.map, the name the editor hands to the
	// OS save dialog as its default
	path ExportPath(const std::string &TargetId, std::string_view ProjectName);
	// the same, for a .unitwp package: <export>/<target>/<name>.<target>.unitwp
	path PackageExportPath(const std::string &TargetId, std::string_view ProjectName);

	// a filesystem safe version of a project name: no separators, no control
	// characters, never empty. "My Map!" stays "My Map!".
	std::string SanitiseName(std::string_view Name);

	/*
	The resource root of the installation. It is normally the data directory
	next to the executable or the one baked in at build time, and can be
	overridden with the UNITEED_DATA_DIR environment variable.
	*/
	const path &GetDataDirectory();
	void SetDataDirectory(const path &Directory);

	/*
	The ordered list of directories FindResource() searches. The editor sets
	this up from the active map version, for example
		<data>/versions/07/mapres, <data>/shared/mapres, <data>/versions/06/mapres
	so that the 0.7 tiles win, shared tiles are found, and 0.6 tiles are still
	reachable as a fallback.
	*/
	void SetResourcePaths(std::vector<path> Paths);
	const std::vector<path> &ResourcePaths();
	void AddResourcePath(const path &Directory, bool Front = false);

	// the first existing file among the resource paths, an empty path when the
	// file is not there. A relative path is expected.
	path FindResource(const path &Relative);

	// joins a resource directory of a version with a file name
	path VersionDirectory(const std::string &Version);

	/*
	Reads a whole file into memory. Returns std::nullopt when the file cannot be
	opened or cannot be read completely.
	*/
	std::optional<byte_vector> read(const path &target);

	// convenience wrapper for the text resources of the editor
	std::optional<std::string> read_text(const path &target);

	bool exists(const path &target);
	bool is_file(const path &target);

	// sorted list of regular files in a directory, optionally filtered by
	// extension (".png"); returns the full paths
	std::vector<path> list_directory(const path &Directory, const std::string &Extension = "");

	// sorted list of the subdirectories of a directory
	std::vector<path> list_subdirectories(const path &Directory);

	/*
	Writes a whole file, creating the parent directories. Returns false when the
	file cannot be written.
	*/
	bool write(const path &target, const_data_span Data);
	bool write_text(const path &target, std::string_view Text);

	enum class mode
	{
		text = 0,
		binary,
	};

	/*
	class filewriter,
	it would create directories to prepare for writing file.
	*/
	class filewriter
	{
		std::ofstream m_Stream;

	public:
		filewriter() = default;
		filewriter(const path &target, mode filemode, bool append);
		~filewriter() { close(); }
		void write(std::string_view string);
		void write(const_data_span bytes);
		void write(const void *pData, std::size_t Size);
		void close();
		void flush();
		bool error() const;
		bool is_open() const;
	};
} // namespace storage

#endif // BASE_STORAGE_H
