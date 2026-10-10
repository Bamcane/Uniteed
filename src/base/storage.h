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
	method GetAppDataDirectory
	returns AppData directory path;
	on Linux: ~/.local/share/uniteed
	*/
	path GetAppDataDirectory();

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
