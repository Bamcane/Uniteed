#ifndef BASE_I18N_H
#define BASE_I18N_H

/*
	The interface in another language.

	The source text *is* the key: every string the interface shows is written in
	English where it is drawn, and wrapped in `Tr()` when it is handed to the
	painter. A language is a JSON document under `<data>/lang`, and a string it
	does not carry comes out as the English source, so a half finished
	translation is still a usable interface and adding a language cannot leave a
	hole in it.

		{
			"id": "zh-Hans",
			"name": "简体中文",
			"strings": {
				"Files": "文件"
			}
		}

	`name` is the language's own name, which is what the settings menu lists: a
	user looking for their language cannot recognise a name written in one they
	do not read. `id` defaults to the file name and is what the settings file and
	`--language` use. English is not a file - it is the source text itself.

	Translation happens at the call site rather than inside ui::Text(), and that
	is deliberate: the same function draws the names of the *user's* layers,
	groups and files, and a map that has a layer called "Tiles" must keep that
	name. Only strings this program wrote are looked up.

	The catalog is read once per language, and the strings are kept in a deque:
	an entry never moves, so `Tr()` can hand out a pointer that stays valid while
	a frame is drawn - including the frame the language is switched on.
*/

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/storage.h"

namespace base::i18n
{
	// a language that is installed: what it calls itself, and where it was read
	// from (empty for English, which has no file)
	struct CLanguage
	{
		std::string m_Id;
		std::string m_Name;
		storage::path m_Path;
	};

	// the languages under <data>/lang, English first. It reads the directory,
	// so it is called once at start up and again when a test wants to see the
	// installed set.
	const std::vector<CLanguage> &Languages();
	void RefreshLanguages();

	const std::string &CurrentId();

	/*
		Switches the interface to a language, without touching the strings that
		are already loaded, so the frame a switch happens on can still finish
		drawing. An id that is not installed leaves the language where it is and
		returns false; "en" always works, because it needs no file.
	*/
	bool SetLanguage(std::string_view Id);

	// the language the environment asks for, read from LC_ALL, LC_MESSAGES and
	// LANG in that order: what a session starts in before anybody chooses
	std::string SystemLanguageId();

	// the translation of an English source string, or the source itself
	const char *Tr(const char *pSource);

	// how many strings the loaded catalog carries
	std::size_t EntryCount();
} // namespace base::i18n

#endif // BASE_I18N_H
