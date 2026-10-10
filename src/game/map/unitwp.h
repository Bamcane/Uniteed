#ifndef GAME_MAP_UNITWP_H
#define GAME_MAP_UNITWP_H

/*
	.unitwp - the Uniteed map package.

	A Teeworlds .map file can only hold what the map format allows: the map
	itself. A package adds what an editor wants to keep around it, and it does
	so with a container that is trivial to read and extend:

		magic        "UNITWP\0\1"                     8 bytes
		version      int32, currently 1
		flags        int32, reserved
		chunks       { char id[4]; int32 size; bytes }

	chunk ids
		META   text, key=value lines: package id, name, author, target,
		       timestamps, the map file it came from
		MAP    the map, serialised as a datafile with the package profile
		       (see map.cpp: DDRace tilemaps, every item preserved)
		EDIT   text, the editor state (camera, selection, brush, layer flags);
		       opaque to this module so the editor can grow it freely
		RSRC   binary resource table, embedded mapres files for a package that
		       has to travel on its own
		THUM   PNG, a map thumbnail (written by the editor, optional)

	Everything is little endian. The map inside a package is normalised when
	the package is loaded, so a package can be exported to any target version.
*/

#include <optional>
#include <string>
#include <vector>

#include <sugar_types.h>

#include "base/storage.h"
#include "game/map/map.h"

namespace game
{
	struct CUnitwpResource
	{
		std::string m_Name; // relative path, for example "mapres/grass_main.png"
		byte_vector m_Data;
	};

	struct CUnitwpMeta
	{
		std::string m_PackageId; // a UUID, used to recognise the package
		std::string m_Name;
		std::string m_Author;
		std::string m_ExportTarget = "08"; // the target of the last export
		std::string m_Created;             // unix time
		std::string m_Updated;             // unix time
		std::string m_EditorVersion;
		std::string m_SourceMap; // the .map file the package was created from

		std::string ToText() const;
		static CUnitwpMeta FromText(const std::string &Text);
	};

	class CUnitwpPackage
	{
	public:
		static constexpr const char *Extension = ".unitwp";
		static constexpr std::uint32_t ContainerVersion = 1;

		CMap m_Map;
		CUnitwpMeta m_Meta;
		std::string m_EditorState; // key=value text owned by the editor
		std::vector<CUnitwpResource> m_vResources;

		void Clear();

		// builds a package around an already loaded map
		void Adopt(const CMap &Source, std::string_view Name, std::string_view SourcePath = {});

		bool Serialise(byte_vector &Out, std::string *pError = nullptr) const;
		bool Deserialise(const byte_vector &Bytes, std::string *pError = nullptr);

		bool SaveToFile(const storage::path &FileName, std::string *pError = nullptr) const;
		bool LoadFromFile(const storage::path &FileName, std::string *pError = nullptr);

		const CUnitwpResource *FindResource(std::string_view Name) const;

		static bool IsPackage(const byte_vector &Bytes);
	};
} // namespace game

#endif // GAME_MAP_UNITWP_H
