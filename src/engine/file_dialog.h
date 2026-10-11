#ifndef ENGINE_FILE_DIALOG_H
#define ENGINE_FILE_DIALOG_H

/*
	The native file dialogs: open a file, save a file or pick a folder.

	SDL shows a dialog without blocking and calls back later, from inside
	SDL_PumpEvents, which the frame loop already runs. There is therefore no
	event hook to attach to: Open hands the request over, and the editor asks
	Poll once per frame whether the answer has arrived.

	Everything here is GUI only: the headless build does not compile the
	translation unit that includes SDL, the way src/engine/main.cpp keeps its
	window code behind UNITEED_GUI.
*/

#include <atomic>
#include <string>
#include <vector>

#include <SDL3/SDL_dialog.h>

namespace engine
{
	enum class dialog_kind
	{
		open_file,
		save_file,
		pick_folder,
	};

	struct CFileFilter
	{
		std::string m_Name;    // "Uniteed package"
		// a glob ("*.unitwp", "*.png;*.jpg"), SDL's own extension list
		// ("unitwp"), or "*" for every file; Open hands SDL the shape it wants
		std::string m_Pattern;
	};

	/*
		One dialog at a time. The object owns the strings SDL borrows while a
		dialog is up (the filter array and the default location), so none of
		the caller's arguments have to outlive the call.

		The callback is a plain C function that receives this object as its
		userdata and only writes into it: the editor is never touched from
		there, because the call lands in the middle of a pumped event batch.
	*/
	class CFileDialog
	{
	public:
		// Opens the platform dialog. Returns false when a dialog is already
		// open or SDL refuses to start one. The callbacks never block.
		//
		// SDL 3.4's SDL_ShowOpenFileDialog, SDL_ShowSaveFileDialog and
		// SDL_ShowOpenFolderDialog take no title of their own, so Title is
		// logged rather than shown; a caller that needs it on screen would have
		// to go through SDL_ShowFileDialogWithProperties instead.
		bool Open(dialog_kind Kind, SDL_Window *pWindow,
			const std::string &Title,
			const std::string &DefaultPath,          // may be empty
			const std::string &DefaultName,          // save dialogs
			const std::vector<CFileFilter> &Filters,
			bool bAllowMultiple = false);

		// Call once per frame, after SDL has pumped events. Returns true on the
		// frame the dialog completed, and fills the result.
		bool Poll();

		bool IsOpen() const;
		void Cancel();                       // SDL3 3.4: SDL_HideDialog if available, else mark cancelled
		void SetLastDirectory(const std::string &Directory);
		const std::string &LastDirectory() const;

		// the outcome of the last completed dialog
		const std::vector<std::string> &Selected() const;   // absolute paths, empty when cancelled
		bool Cancelled() const;
		const std::string &Error() const;

		// whether a dialog can be asked for at all; see the comment on the
		// definition for what SDL is able to report here
		bool Available() const;

	private:
		// the C callback SDL invokes; pUser is the CFileDialog
		static void SDLCALL Callback(void *pUser, const char * const *pFiles, int Filter);

		// the handshake between Callback and Poll; both run on the main thread,
		// the atomic is what makes the result visible to the polling frame
		std::atomic<bool> m_bOpen{false};
		std::atomic<bool> m_bCompleted{false};

		bool m_bCancelled = false;
		std::string m_Error;
		std::vector<std::string> m_vSelected;
		// where the last dialog ended, so the next one starts there
		std::string m_LastDirectory;

		// SDL borrows the filter array and the strings behind it until the
		// callback has run, so both live here rather than in Open's frame
		std::vector<SDL_DialogFileFilter> m_vFilters;
		std::vector<std::string> m_vFilterText;
		// the default_location string has to outlive the Show* call as well
		std::string m_DefaultLocation;
	};
} // namespace engine

#endif // ENGINE_FILE_DIALOG_H
