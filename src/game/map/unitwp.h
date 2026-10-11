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
		META   text, key=value lines: package id, name, author, description,
		       target, assets, timestamps, the map file it came from
		MAP    the map, serialised as a datafile with the package profile
		       (see map.cpp: DDRace tilemaps, every item preserved)
		EDIT   text, JSON: the editor state (camera, selection, brush, dock
		       layout, layer flags); opaque to this module so the editor can
		       grow it freely
		RULE   text, JSON: the autorules document (see game/rules/rules.h)
		RSRC   binary resource table, embedded mapres files for a package that
		       has to travel on its own
		THUM   PNG, a map thumbnail (written by the editor, optional)

	Everything is little endian. The map inside a package is normalised when
	the package is loaded, so a package can be exported to any target version.

	A package *is* a Uniteed project: one file that holds the map, the editor
	session and the autorules. game/project.h is the editor's view of it, and
	--pack / --unpack read and write the same container.
*/

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
		std::string m_Description;
		std::string m_ExportTarget = "08"; // the target of the last export
		std::string m_Assets = "08";       // the mapres version the map was built against
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
		std::string m_EditorState; // JSON text owned by the editor
		std::string m_Rules;       // JSON text owned by game/rules/rules.h
		std::vector<CUnitwpResource> m_vResources;

		void Clear();

		// builds a package around an already loaded map
		void Adopt(const CMap &Source, std::string_view Name, std::string_view SourcePath = {});

		bool Serialise(byte_vector &Out, std::string *pError = nullptr) const;
		bool Deserialise(const byte_vector &Bytes, std::string *pError = nullptr);

		bool SaveToFile(const storage::path &FileName, std::string *pError = nullptr) const;
		bool LoadFromFile(const storage::path &FileName, std::string *pError = nullptr);

		const CUnitwpResource *FindResource(std::string_view Name) const;

		// embeds the mapres files the map references, so that a package travels
		// on its own; a mapres that is already embedded is skipped
		void AttachMapResources();

		/*
			Reading and replacing a single chunk without decoding the rest of
			the container: listing the projects in a directory only wants the
			metadata, and saving a project wants to keep the resources and the
			thumbnail that were already there.
		*/
		static bool FindChunk(const byte_vector &Bytes, std::string_view Id, std::string *pOut);
		static bool SetChunk(byte_vector &Bytes, std::string_view Id, std::string_view Data);

		static bool IsPackage(const byte_vector &Bytes);
	};
} // namespace game

#endif // GAME_MAP_UNITWP_H
