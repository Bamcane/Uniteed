#include "engine/file_dialog.h"

/*
	The state machine behind CFileDialog.

	SDL 3.4's SDL_ShowOpenFileDialog, SDL_ShowSaveFileDialog and
	SDL_ShowOpenFolderDialog all return void: they hand the request to the
	platform and, in the same breath, say that the answer comes to the
	callback later. A request the platform refuses is reported the other way
	round, by setting an error and never calling back, so Open clears the error
	first and reads it afterwards to tell a started dialog from a refused one.

	The callback runs on the main thread on every platform the editor ships
	on, and the file list is freed by SDL the moment the callback returns:
	every path is copied before that, and nothing outside this object is
	touched from inside the callback.
*/

#include <cstddef>
#include <filesystem>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include "base/logger.h"
#include "base/storage.h"

namespace engine
{
	namespace
	{
		// the folder a chosen path lives in, for the next dialog to start at
		std::string ParentDirectory(const std::string &File)
		{
			const storage::path Parent = storage::path(File).parent_path();
			return Parent.empty() ? std::string() : Parent.string();
		}

		// a folder and a file name as one path, which is what the save dialog
		// wants as its default location
		std::string JoinPath(const std::string &Directory, const std::string &Name)
		{
			if(Directory.empty())
				return Name;
			return (storage::path(Directory) / Name).string();
		}

		/*
			SDL 3.4 wants a semicolon separated list of extensions ("unitwp" or
			"png;jpg"), and refuses the whole dialog when a filter carries
			anything else: a caller writing the natural glob ("*.unitwp") would
			get no dialog at all. The asterisk and the leading dot are therefore
			dropped, a lone "*" stays the "all files" filter, and a pattern that
			ends up empty becomes "all files" as well, because SDL treats an
			empty pattern as an error too.
		*/
		std::string NormalizePattern(const std::string &Pattern)
		{
			std::string Result;
			std::size_t Start = 0;
			while(true)
			{
				const std::size_t End = Pattern.find(';', Start);
				std::string Part = Pattern.substr(Start, End == std::string::npos ? std::string::npos : End - Start);

				const std::size_t First = Part.find_first_not_of(" \t");
				const std::size_t Last = Part.find_last_not_of(" \t");
				Part = First == std::string::npos ? std::string() : Part.substr(First, Last - First + 1);

				if(Part == "*")
					return "*";
				if(Part.starts_with("*."))
					Part.erase(0, 2);
				else if(Part.starts_with("*"))
					Part.erase(0, 1);
				else if(Part.starts_with("."))
					Part.erase(0, 1);

				if(!Part.empty())
				{
					if(!Result.empty())
						Result.push_back(';');
					Result += Part;
				}

				if(End == std::string::npos)
					break;
				Start = End + 1;
			}
			return Result.empty() ? "*" : Result;
		}
	} // namespace

	void SDLCALL CFileDialog::Callback(void *pUser, const char * const *pFiles, int Filter)
	{
		(void)Filter; // the selected filter is not part of the result

		auto *pDialog = static_cast<CFileDialog *>(pUser);
		if(pDialog == nullptr)
			return;
		// a request that was cancelled no longer wants an answer; the platform
		// may still send one, because SDL 3.4 cannot take a dialog back
		if(!pDialog->m_bOpen.load(std::memory_order_acquire))
			return;

		pDialog->m_vSelected.clear();
		if(pFiles == nullptr)
		{
			// NULL is an error, an empty list is the user cancelling
			pDialog->m_bCancelled = false;
			pDialog->m_Error = SDL_GetError();
			if(pDialog->m_Error.empty())
				pDialog->m_Error = "the file dialog failed";
			LOG_WARN("dialog", "%s", pDialog->m_Error.c_str());
		}
		else
		{
			// SDL frees the list when this returns, so the paths are copied now
			for(int i = 0; pFiles[i] != nullptr; i++)
				pDialog->m_vSelected.emplace_back(pFiles[i]);

			pDialog->m_bCancelled = pDialog->m_vSelected.empty();
			pDialog->m_Error.clear();
			if(!pDialog->m_vSelected.empty())
				pDialog->m_LastDirectory = ParentDirectory(pDialog->m_vSelected.back());

			if(pDialog->m_bCancelled)
				LOG_INFO("dialog", "the dialog was cancelled");
			else
				LOG_INFO("dialog", "the dialog returned %zu path(s), the last in '%s'",
					pDialog->m_vSelected.size(), pDialog->m_LastDirectory.c_str());
		}

		// the result is complete before the flag goes up, which is what makes
		// it safe for Poll to read it without another lock
		pDialog->m_bCompleted.store(true, std::memory_order_release);
	}

	bool CFileDialog::Open(dialog_kind Kind, SDL_Window *pWindow,
		const std::string &Title,
		const std::string &DefaultPath,
		const std::string &DefaultName,
		const std::vector<CFileFilter> &Filters,
		bool bAllowMultiple)
	{
		if(m_bOpen.load(std::memory_order_acquire))
			return false;

		// a fresh request drops the outcome of the previous one; this happens
		// before SDL is asked, so a refusal can leave its own error behind
		m_bCancelled = false;
		m_Error.clear();
		m_vSelected.clear();
		m_bCompleted.store(false, std::memory_order_release);
		// the title cannot travel with the three entry points SDL offers, so it
		// only reaches the log
		LOG_TRACE("dialog", "opening '%s'", Title.c_str());

		/*
			The filter array and its strings are borrowed by SDL until the
			callback has run, so the text is copied into storage that lives as
			long as the object. The pointers stay valid because the vectors are
			not touched again while the dialog is open: Open refuses to start a
			second request.
		*/
		m_vFilters.clear();
		m_vFilterText.clear();
		m_vFilterText.reserve(Filters.size() * 2);
		for(const CFileFilter &Filter : Filters)
		{
			m_vFilterText.push_back(Filter.m_Name);
			m_vFilterText.push_back(NormalizePattern(Filter.m_Pattern));
		}
		for(std::size_t i = 0; i < Filters.size(); i++)
			m_vFilters.push_back(SDL_DialogFileFilter{m_vFilterText[i * 2].c_str(), m_vFilterText[i * 2 + 1].c_str()});

		m_DefaultLocation = DefaultPath.empty() ? m_LastDirectory : DefaultPath;
		// a save dialog starts on a file, the other two start on a folder
		if(Kind == dialog_kind::save_file && !DefaultName.empty())
			m_DefaultLocation = JoinPath(m_DefaultLocation, DefaultName);

		const SDL_DialogFileFilter *pFilters = m_vFilters.empty() ? nullptr : m_vFilters.data();
		const int FilterCount = static_cast<int>(m_vFilters.size());
		const char *pLocation = m_DefaultLocation.empty() ? nullptr : m_DefaultLocation.c_str();

		// the error is the only signal a refused request gives, so it is
		// cleared here and inspected after the call
		SDL_ClearError();
		m_bOpen.store(true, std::memory_order_release);
		switch(Kind)
		{
		case dialog_kind::open_file:
			SDL_ShowOpenFileDialog(&CFileDialog::Callback, this, pWindow, pFilters, FilterCount, pLocation, bAllowMultiple);
			break;
		case dialog_kind::save_file:
			SDL_ShowSaveFileDialog(&CFileDialog::Callback, this, pWindow, pFilters, FilterCount, pLocation);
			break;
		case dialog_kind::pick_folder:
			SDL_ShowOpenFolderDialog(&CFileDialog::Callback, this, pWindow, pLocation, bAllowMultiple);
			break;
		}

		const char *pError = SDL_GetError();
		// a callback that ran before the call returned has already finished the
		// request and owns the outcome, error or not
		if(!m_bCompleted.load(std::memory_order_acquire) && pError != nullptr && pError[0] != '\0')
		{
			m_bOpen.store(false, std::memory_order_release);
			m_Error = pError;
			LOG_ERROR("dialog", "SDL refused the file dialog: %s", pError);
			return false;
		}
		return true;
	}

	bool CFileDialog::Poll()
	{
		if(!m_bCompleted.load(std::memory_order_acquire))
			return false;

		// the flag is the handshake: the callback wrote the result before
		// raising it, and this is the frame that consumes it
		m_bCompleted.store(false, std::memory_order_release);
		m_bOpen.store(false, std::memory_order_release);
		return true;
	}

	bool CFileDialog::IsOpen() const
	{
		// a finished dialog is no longer open, even before Poll has collected
		// the result
		return m_bOpen.load(std::memory_order_acquire) && !m_bCompleted.load(std::memory_order_acquire);
	}

	void CFileDialog::Cancel()
	{
		/*
			SDL 3.4 has no SDL_HideDialog, so a dialog that is already on
			screen cannot be taken back. The request is only marked cancelled:
			Poll reports it at once and a late answer from the platform is
			dropped. On a desktop the window stays up until the user answers
			it, and that answer goes nowhere.

			A dialog that has already finished is left alone, so a selection
			that arrived before this call is not thrown away.
		*/
		if(!IsOpen())
			return;

		m_bOpen.store(false, std::memory_order_release);
		m_bCancelled = true;
		m_Error.clear();
		m_vSelected.clear();
		m_bCompleted.store(true, std::memory_order_release);
	}

	void CFileDialog::SetLastDirectory(const std::string &Directory)
	{
		m_LastDirectory = Directory;
	}

	const std::string &CFileDialog::LastDirectory() const
	{
		return m_LastDirectory;
	}

	const std::vector<std::string> &CFileDialog::Selected() const
	{
		return m_vSelected;
	}

	bool CFileDialog::Cancelled() const
	{
		return m_bCancelled;
	}

	const std::string &CFileDialog::Error() const
	{
		return m_Error;
	}

	bool CFileDialog::Available() const
	{
		/*
			SDL has no query for whether a native dialog backend exists: the
			entry points have been there since 3.2 and SDL falls back to its own
			implementation where the platform has none, so a request is always
			accepted. What every backend does need is the video subsystem, which
			is the one precondition worth reporting here.
		*/
		return SDL_WasInit(SDL_INIT_VIDEO) != 0;
	}
} // namespace engine
