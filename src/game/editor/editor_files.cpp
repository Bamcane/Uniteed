/*
	The editor's file dialogs and the project/package operations behind them.

	Opening and saving use the platform's own dialog: a list drawn inside the
	window cannot show the bookmarks, the network shares or the recent folders
	the operating system knows about, and every mapmaker already has those.
	The dialogs are asynchronous - SDL hands the answer back from inside its
	event pump - so a request is remembered here and completed on a later
	frame by PollFileDialog().

	The default locations are the ones the request named: a project lives in
	the application data's projects directory, and an export lands in export/,
	so a mapmaker who never touches a path still finds their files.
*/

#include <algorithm>
#include <cstring>

#include "base/i18n.h"
#include "base/logger.h"
#include "base/string.h"
#include "base/time.h"
#include "engine/graphics.h"
#include "game/editor/editor.h"

#if UNITEED_GUI

namespace game
{
	using base::i18n::Tr;
	namespace
	{
		// what the platform dialog is told to look for, per purpose
		std::vector<engine::CFileFilter> FiltersFor(bool bProjects)
		{
			std::vector<engine::CFileFilter> Filters;
			if(bProjects)
			{
				// a project is a .unitwp container: the same file the editor
				// saves, opens and exports from
				Filters.push_back({Tr("Uniteed project"), "*" + std::string(CUnitwpPackage::Extension)});
				Filters.push_back({Tr("Teeworlds map"), "*.map"});
			}
			else
			{
				Filters.push_back({Tr("Teeworlds map"), "*.map"});
				Filters.push_back({Tr("Uniteed project"), "*" + std::string(CUnitwpPackage::Extension)});
			}
			Filters.push_back({"every file", "*"});
			return Filters;
		}

		// the directory a dialog should start in: where the last one ended, then
		// the project's own directory
		std::string StartDirectory(const engine::CFileDialog &Dialog)
		{
			if(!Dialog.LastDirectory().empty() && storage::exists(Dialog.LastDirectory()))
				return Dialog.LastDirectory();
			return storage::GetProjectsDirectory().string();
		}

		// the directory an asset import should start in: the version's own
		// mapres or audio directory, which is where a map maker's material is
		std::string AssetStartDirectory()
		{
			for(const storage::path &Directory : storage::ResourcePaths())
				if(storage::exists(Directory))
					return Directory.string();
			return storage::GetDataDirectory().string();
		}
	} // namespace

	bool CEditor::ImportImageDialog()
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;
		std::vector<engine::CFileFilter> Filters;
		Filters.push_back({"Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.gif"});
		Filters.push_back({"every file", "*"});
		if(!m_FileDialog.Open(engine::dialog_kind::open_file, m_pGraphics->Window(), "Import an image",
			AssetStartDirectory(), std::string(), Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = file_dialog_purpose::import_image;
		return true;
	}

	bool CEditor::ImportSoundDialog()
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;
		std::vector<engine::CFileFilter> Filters;
		Filters.push_back({"Sound", "*.opus;*.ogg;*.wv;*.wav"});
		Filters.push_back({"every file", "*"});
		if(!m_FileDialog.Open(engine::dialog_kind::open_file, m_pGraphics->Window(), "Import a sound",
			AssetStartDirectory(), std::string(), Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = file_dialog_purpose::import_sound;
		return true;
	}

	bool CEditor::ExportAssetDialog(bool bSound)
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;
		const int Index = bSound ? m_SelectedSound : m_SelectedImage;
		if(Index < 0)
			return false;
		const std::string Name = bSound ? m_Map.m_vSounds[static_cast<std::size_t>(Index)].m_Name :
			m_Map.m_vImages[static_cast<std::size_t>(Index)].m_Name;
		std::vector<engine::CFileFilter> Filters;
		Filters.push_back(bSound ? engine::CFileFilter{Tr("Opus sound"), "*.opus"} : engine::CFileFilter{Tr("PNG image"), "*.png"});
		Filters.push_back({"every file", "*"});
		if(!m_FileDialog.Open(engine::dialog_kind::save_file, m_pGraphics->Window(),
			bSound ? "Export the sound" : "Export the image", storage::GetExportDirectory().string(),
			Name + (bSound ? ".opus" : ".png"), Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = bSound ? file_dialog_purpose::export_sound : file_dialog_purpose::export_image;
		m_FileDialogAsset = Index;
		return true;
	}

	bool CEditor::OpenFileDialog()
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;
		const std::vector<engine::CFileFilter> Filters = FiltersFor(true);
		if(!m_FileDialog.Open(engine::dialog_kind::open_file, m_pGraphics->Window(), "Open a map or a project",
			StartDirectory(m_FileDialog), std::string(), Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = file_dialog_purpose::open;
		return true;
	}

	bool CEditor::SaveFileDialog(bool bAs)
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;

		// an existing project saves without asking, which is what Ctrl+S means
		const std::string Name = ProjectName().empty() ? "untitled" : ProjectName();
		const std::string Existing = !m_SourcePath.empty() && base::EndsWith(m_SourcePath, CUnitwpPackage::Extension) ?
			m_SourcePath : ProjectPath(Name).string();
		if(!bAs && !Existing.empty())
			return SaveProject(Existing);

		const std::vector<engine::CFileFilter> Filters = FiltersFor(true);
		if(!m_FileDialog.Open(engine::dialog_kind::save_file, m_pGraphics->Window(), "Save the project",
			StartDirectory(m_FileDialog), Name + CUnitwpPackage::Extension, Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = bAs ? file_dialog_purpose::save_as : file_dialog_purpose::save;
		m_FileDialogDefaultName = Name;
		return true;
	}

	bool CEditor::ExportFileDialog(map_target Target)
	{
		if(m_pGraphics == nullptr || m_pGraphics->Window() == nullptr)
			return false;
		const CMapTargetProfile &Profile = GetTargetProfile(Target);
		const std::string Name = ProjectName().empty() ? "untitled" : ProjectName();
		std::vector<engine::CFileFilter> Filters;
		Filters.push_back({Tr("Teeworlds map"), "*.map"});
		Filters.push_back({"every file", "*"});

		// the export directory of this target, so the dialog opens where the
		// last export of the same version landed
		const storage::path Directory = storage::GetExportDirectoryForTarget(Profile.m_pId);
		if(!m_FileDialog.Open(engine::dialog_kind::save_file, m_pGraphics->Window(),
			std::string("Export for ") + Profile.m_pName, Directory.string(),
			Name + "." + Profile.m_pId + ".map", Filters, false))
		{
			Notify(base::FormatRuntime(Tr("the file dialog could not be opened: {}"), m_FileDialog.Error()));
			return false;
		}
		m_FileDialogPurpose = file_dialog_purpose::export_map;
		m_FileDialogTarget = Target;
		m_FileDialogDefaultName = Name;
		return true;
	}

	bool CEditor::PollFileDialog()
	{
		if(m_FileDialogPurpose == file_dialog_purpose::none)
			return false;
		if(!m_FileDialog.Poll())
			return false;

		const file_dialog_purpose Purpose = m_FileDialogPurpose;
		m_FileDialogPurpose = file_dialog_purpose::none;

		if(m_FileDialog.Cancelled() || m_FileDialog.Selected().empty())
		{
			Notify( Tr("cancelled") );
			return true;
		}
		// only the first selection is used: the mode asks for one file
		const std::string Path = m_FileDialog.Selected().front();

		switch(Purpose)
		{
		case file_dialog_purpose::open:
			if(IsProjectFile(Path) || base::EndsWith(Path, CUnitwpPackage::Extension))
				LoadProject(Path);
			else
				OpenFile(Path);
			break;
		case file_dialog_purpose::save:
		case file_dialog_purpose::save_as:
			if(base::EndsWith(Path, CUnitwpPackage::Extension))
				SaveProject(Path);
			else if(base::EndsWith(Path, ".map"))
				// a map name means a package that travels, so the map can be
				// handed to a client as it is
				SavePackage(Path.substr(0, Path.size() - 4) + CUnitwpPackage::Extension);
			else
				// a name without the extension becomes a project, which is the
				// format that keeps the editor state
				SaveProject(Path + CUnitwpPackage::Extension);
			break;
		case file_dialog_purpose::export_map:
			ExportMap(Path, m_FileDialogTarget);
			break;
		case file_dialog_purpose::import_image:
		{
			std::string Error;
			const int Index = AddImageFromFile(Path, &Error);
			if(Index < 0)
				Notify(  Error.empty() ? Tr("the image could not be imported") : Error  );
			else
				m_SelectedImage = Index;
			break;
		}
		case file_dialog_purpose::import_sound:
		{
			std::string Error;
			const int Index = AddSoundFromFile(Path, &Error);
			if(Index < 0)
				Notify(  Error.empty() ? Tr("the sound could not be imported") : Error  );
			else
				m_SelectedSound = Index;
			break;
		}
		case file_dialog_purpose::export_image:
		{
			std::string Error;
			const std::string Target = base::EndsWith(Path, ".png") ? Path : Path + ".png";
			if(!ExportImage(m_FileDialogAsset, Target, &Error))
				Notify(  Error.empty() ? Tr("the image could not be exported") : Error  );
			else
				Notify(base::FormatRuntime(Tr("wrote {}"), Target));
			break;
		}
		case file_dialog_purpose::export_sound:
		{
			std::string Error;
			const std::string Target = base::EndsWith(Path, ".opus") ? Path : Path + ".opus";
			if(!ExportSound(m_FileDialogAsset, Target, &Error))
				Notify(  Error.empty() ? Tr("the sound could not be exported") : Error  );
			else
				Notify(base::FormatRuntime(Tr("wrote {}"), Target));
			break;
		}
		case file_dialog_purpose::none:
			break;
		}
		return true;
	}
} // namespace game

#else

namespace game
{
	// the headless build has no window and therefore no dialog: a caller that
	// wants a file names it, which is what the command line and the agent do
	bool CEditor::OpenFileDialog() { return false; }
	bool CEditor::SaveFileDialog(bool) { return false; }
	bool CEditor::ExportFileDialog(map_target) { return false; }
	bool CEditor::PollFileDialog() { return false; }
	bool CEditor::ImportImageDialog() { return false; }
	bool CEditor::ImportSoundDialog() { return false; }
	bool CEditor::ExportAssetDialog(bool) { return false; }
} // namespace game

#endif
