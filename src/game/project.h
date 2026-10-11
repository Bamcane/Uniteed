#ifndef GAME_PROJECT_H
#define GAME_PROJECT_H

/*
	A Uniteed project: everything the editor needs to continue where it left
	off, in one file under the application data directory's projects/.

	The container is the binary `.unitwp` package (game/map/unitwp.h), not a
	bespoke text document, because a project and a package hold exactly the
	same things:

		META   the project's identity: id, name, author, description, the
		       export target, the mapres version, the timestamps
		MAP    the map itself, as a datafile written for the package profile,
		       which is a superset of every export target
		EDIT   the editor session as JSON: the camera, the brush, the
		       selection, the grid flags, the dock layout
		RULE   the autorules document as JSON (see game/rules/rules.h)
		RSRC   the embedded mapres files, when the project carries its assets
		THUM   a map thumbnail, when one has been made

	Keeping the map inside a real datafile rather than a bespoke object graph
	means a project round trips through exactly the same format layer the
	export uses, so nothing the editor does not model can be lost.

	`project.*` on the agent interface, `file.open` and the editor's own open
	and save all go through this module. A single `.unitwp` file is therefore
	both the project a mapmaker keeps working in and the package they hand to
	someone else; `--unpack` writes the map out of either.
*/

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/json.h"
#include "base/storage.h"
#include "game/map/map.h"
#include "game/map/unitwp.h"

namespace game
{
	// the projection of a project's META chunk, cheap enough to read while
	// listing a directory full of projects
	struct CProjectMeta
	{
		std::string m_Id; // a UUID, so a project is recognisable
		std::string m_Name; // the file stem, and what the save dialog offers
		std::string m_Author;
		std::string m_Description;
		std::string m_Created; // unix seconds as text
		std::string m_Updated;
		std::string m_Target = "08";
		// the mapres directory the map was built against: 06, 07 or ddnet
		std::string m_Assets = "08";
	};

	struct CProject
	{
		CProjectMeta m_Meta;
		CMap m_Map;
		base::json m_Editor; // the editor state, owned by the editor
		base::json m_Rules;  // the autorules document, owned by rules.h
		base::json m_Baseline; // the last loaded editor state, for merging
		bool m_bHasBaseline = false;
		// mapres files carried along, so an imported package keeps them
		std::vector<CUnitwpResource> m_vResources;

		void Clear();
		bool IsEmpty() const { return m_Map.m_vGroups.empty(); }

		// a fresh project with the standard two groups
		void InitNew(std::string_view Name, std::string_view TargetId);

		// the editor state to write out: the keys of `State`, and every key of
		// the last loaded state that this version of the editor does not know
		base::json MergeEditorState(base::json State) const;

		// --- container ----------------------------------------------------
		bool SaveToBytes(byte_vector &Out, std::string *pError = nullptr) const;
		bool LoadFromBytes(const byte_vector &Bytes, std::string *pError = nullptr);

		// --- files --------------------------------------------------------
		bool SaveToFile(const storage::path &FileName, std::string *pError = nullptr) const;
		bool LoadFromFile(const storage::path &FileName, std::string *pError = nullptr);
	};

	// the projects in the application data's projects directory, newest first
	std::vector<CProjectMeta> ListProjects();
	// true when the file is a project: a .unitwp container on disk
	bool IsProjectFile(const storage::path &FileName);
	// the file name of a project, without a directory
	std::string ProjectFileName(std::string_view Name);
	// the full path of the default project file for a name
	storage::path ProjectPath(std::string_view Name);
	// the project that a name, a path or a bare stem refers to: an existing
	// project file, or the default location for a new one
	storage::path ResolveProjectPath(std::string_view NameOrPath);
} // namespace game

#endif // GAME_PROJECT_H
