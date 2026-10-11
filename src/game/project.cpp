#include "base/define.h"
#include "project.h"

#include <algorithm>
#include <ctime>
#include <system_error>

#include "base/logger.h"
#include "base/string.h"
#include "base/uuid.h"

namespace game
{
	namespace
	{
		constexpr std::string_view ChunkMeta = "META";
		constexpr std::string_view ChunkEdit = "EDIT";
		constexpr std::string_view ChunkRules = "RULE";

		std::string NowStamp()
		{
			return std::to_string(static_cast<long long>(std::time(nullptr)));
		}

		// the metadata as the package container holds it, so that a project and
		// a plain package have one identity
		CUnitwpMeta ToUnitwpMeta(const CProjectMeta &Meta)
		{
			CUnitwpMeta Out;
			Out.m_PackageId = Meta.m_Id;
			Out.m_Name = Meta.m_Name;
			Out.m_Author = Meta.m_Author;
			Out.m_Description = Meta.m_Description;
			Out.m_ExportTarget = Meta.m_Target;
			Out.m_Assets = Meta.m_Assets;
			Out.m_Created = Meta.m_Created;
			Out.m_Updated = Meta.m_Updated;
			Out.m_EditorVersion = GAME_VERSION;
			return Out;
		}

		CProjectMeta FromUnitwpMeta(const CUnitwpMeta &Meta)
		{
			CProjectMeta Out;
			Out.m_Id = Meta.m_PackageId;
			Out.m_Name = Meta.m_Name;
			Out.m_Author = Meta.m_Author;
			Out.m_Description = Meta.m_Description;
			Out.m_Target = Meta.m_ExportTarget.empty() ? "08" : Meta.m_ExportTarget;
			Out.m_Assets = Meta.m_Assets.empty() ? Out.m_Target : Meta.m_Assets;
			Out.m_Created = Meta.m_Created;
			Out.m_Updated = Meta.m_Updated;
			return Out;
		}

		// a chunk that is meant to be JSON: an unreadable one is a warning and
		// an empty document rather than a failed load, because the map is the
		// part that matters and the editor can rebuild the rest
		base::json JsonFromChunk(const std::string &Text, const char *pWhat)
		{
			if(Text.empty())
				return base::json();
			std::string Error;
			const base::json Document = base::json::parse(Text, &Error);
			if(!Document.is_object() && !Document.is_array())
			{
				LOG_WARN("project", "the {} in this project did not parse: {}", pWhat,
					Error.empty() ? "not a JSON document" : Error.c_str());
				return base::json();
			}
			return Document;
		}
	} // namespace

	void CProject::Clear()
	{
		m_Meta = CProjectMeta{};
		m_Map.Clear();
		m_Editor = base::json();
		m_Rules = base::json();
		m_Baseline = base::json();
		m_bHasBaseline = false;
		m_vResources.clear();
	}

	void CProject::InitNew(std::string_view Name, std::string_view TargetId)
	{
		Clear();
		const std::string Target = TargetId.empty() ? std::string("08") : std::string(TargetId);
		// the identity is made here, so a project that is saved without ever
		// being loaded again still carries a recognizable id
		m_Meta.m_Id = base::NewUuid();
		m_Meta.m_Name = std::string(Name.empty() ? "untitled" : Name);
		m_Meta.m_Target = Target;
		m_Meta.m_Assets = Target;
		m_Meta.m_Created = NowStamp();
		m_Meta.m_Updated = m_Meta.m_Created;
		m_Map.InitNew(m_Meta.m_Name);
	}

	base::json CProject::MergeEditorState(base::json State) const
	{
		if(!State.is_object())
			State = base::json::object();
		if(!m_bHasBaseline || !m_Baseline.is_object())
			return State;
		// a key this build does not write is carried over untouched, so a file
		// written by a newer editor is not silently stripped by an older one
		for(const std::string &Key : m_Baseline.keys())
			if(!State.has(Key))
				State[Key] = m_Baseline[Key];
		return State;
	}

	// ------------------------------------------------------------- container
	bool CProject::SaveToBytes(byte_vector &Out, std::string *pError) const
	{
		CUnitwpPackage Package;
		Package.m_Map = m_Map;
		Package.m_Meta = ToUnitwpMeta(m_Meta);
		Package.m_EditorState = m_Editor.is_null() ? std::string() : m_Editor.dump(1);
		Package.m_Rules = m_Rules.is_null() ? std::string() : m_Rules.dump(1);
		Package.m_vResources = m_vResources;
		return Package.Serialise(Out, pError);
	}

	bool CProject::LoadFromBytes(const byte_vector &Bytes, std::string *pError)
	{
		CUnitwpPackage Package;
		if(!Package.Deserialise(Bytes, pError))
			return false;

		Clear();
		m_Meta = FromUnitwpMeta(Package.m_Meta);
		if(m_Meta.m_Name.empty())
			m_Meta.m_Name = "untitled";
		m_Map = std::move(Package.m_Map);
		m_Editor = JsonFromChunk(Package.m_EditorState, "editor state");
		m_Rules = JsonFromChunk(Package.m_Rules, "autorules");
		m_Baseline = m_Editor;
		m_bHasBaseline = true;
		m_vResources = std::move(Package.m_vResources);
		return true;
	}

	// ------------------------------------------------------------------ files
	bool CProject::SaveToFile(const storage::path &FileName, std::string *pError) const
	{
		byte_vector Bytes;
		if(!SaveToBytes(Bytes, pError))
			return false;
		if(!storage::write(FileName, Bytes))
		{
			if(pError != nullptr)
				*pError = "failed to write '" + FileName.string() + "'";
			return false;
		}
		return true;
	}

	bool CProject::LoadFromFile(const storage::path &FileName, std::string *pError)
	{
		const std::optional<byte_vector> Bytes = storage::read(FileName);
		if(!Bytes.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + FileName.string() + "'";
			return false;
		}
		return LoadFromBytes(*Bytes, pError);
	}

	// ------------------------------------------------------------- discovery
	std::string ProjectFileName(std::string_view Name)
	{
		return storage::SanitiseName(Name) + CUnitwpPackage::Extension;
	}

	storage::path ProjectPath(std::string_view Name)
	{
		return storage::GetProjectsDirectory() / ProjectFileName(Name);
	}

	bool IsProjectFile(const storage::path &FileName)
	{
		const std::string Name = FileName.filename().string();
		if(!base::EndsWith(Name, std::string(CUnitwpPackage::Extension)))
			return false;
		if(!storage::is_file(FileName))
			return false;
		/*
			The extension alone is not proof: the metadata of a real project
			begins with "UNITWP", so a text peek that finds it is enough, and a
			file that happens to be named .unitwp but is not a container is not
			handed to the project loader.
		*/
		const std::optional<std::string> Head = storage::read_text(FileName);
		return Head.has_value() && Head->size() >= 8 && Head->compare(0, 6, "UNITWP") == 0;
	}

	storage::path ResolveProjectPath(std::string_view NameOrPath)
	{
		if(NameOrPath.empty())
			return ProjectPath("untitled");
		const storage::path Given{std::string(NameOrPath)};
		// an explicit path that exists, or one that names a directory
		std::error_code Error;
		if(IsProjectFile(Given))
			return Given;
		if(std::filesystem::is_directory(Given, Error))
			return Given / ProjectFileName("untitled");
		if(Given.has_parent_path() && Given.parent_path() != ".")
		{
			// an explicit path that does not exist yet: it is where a save goes,
			// but the extension is still added when it is missing
			if(base::EndsWith(Given.string(), std::string(CUnitwpPackage::Extension)))
				return Given;
			return storage::path(Given.string() + CUnitwpPackage::Extension);
		}
		// a bare name: an existing project of that name wins, otherwise a new
		// file in the projects directory
		const std::string Stem = Given.stem().string();
		const storage::path ByStem = ProjectPath(Stem);
		if(IsProjectFile(ByStem))
			return ByStem;
		const storage::path ByFull = ProjectPath(Given.string());
		if(IsProjectFile(ByFull))
			return ByFull;
		return ByFull;
	}

	std::vector<CProjectMeta> ListProjects()
	{
		struct CEntry
		{
			CProjectMeta m_Meta;
			std::filesystem::file_time_type m_Time;
		};
		std::vector<CEntry> Entries;
		for(const storage::path &File : storage::list_directory(storage::GetProjectsDirectory(), CUnitwpPackage::Extension))
		{
			/*
				Listing only reads the META and EDIT chunks, so a directory full
				of large maps is not decoded to fill a picker. A file whose
				metadata cannot be read is skipped rather than failing the whole
				listing: it is still on disk for the user to look at.
			*/
			const std::optional<byte_vector> Bytes = storage::read(File);
			if(!Bytes.has_value() || !CUnitwpPackage::IsPackage(*Bytes))
			{
				LOG_WARN("project", "skipping '{}': not a .unitwp package", File.filename().string().c_str());
				continue;
			}
			std::string MetaText;
			if(!CUnitwpPackage::FindChunk(*Bytes, ChunkMeta, &MetaText))
			{
				LOG_WARN("project", "skipping '{}': the package has no metadata", File.filename().string().c_str());
				continue;
			}
			CEntry Entry;
			Entry.m_Meta = FromUnitwpMeta(CUnitwpMeta::FromText(MetaText));
			if(Entry.m_Meta.m_Name.empty())
				Entry.m_Meta.m_Name = base::ReplaceAll(File.filename().string(), CUnitwpPackage::Extension, "");
			std::error_code FileError;
			Entry.m_Time = std::filesystem::last_write_time(File, FileError);
			Entries.push_back(std::move(Entry));
		}
		std::sort(Entries.begin(), Entries.end(), [](const CEntry &A, const CEntry &B) { return A.m_Time > B.m_Time; });

		std::vector<CProjectMeta> Out;
		Out.reserve(Entries.size());
		for(CEntry &Entry : Entries)
			Out.push_back(std::move(Entry.m_Meta));
		return Out;
	}
} // namespace game
