#ifndef ENGINE_FONT_FIND_H
#define ENGINE_FONT_FIND_H

/*
	Where the interface fonts come from, per platform.

	Three faces are needed: a sans serif, its bold companion and one with CJK
	coverage. Windows reads %WINDIR%\Fonts, the per user font directory and the
	Fonts registry key; macOS reads /System/Library/Fonts and /Library/Fonts;
	Linux asks fontconfig and falls back to the distribution directories.

	A configured override always wins, so a font that lives somewhere unusual
	does not have to be moved.
*/

#include <string>
#include <vector>

namespace engine::fonts
{
	// Which of the three faces is wanted; also the order of the candidate lists.
	enum class face
	{
		primary = 0,
		bold,
		cjk,
	};

	// The three faces plus the configured overrides; an empty override means
	// "ask the platform".
	struct request
	{
		std::string m_Primary;
		std::string m_Bold;
		std::string m_Cjk;
	};

	struct result
	{
		std::string m_Primary;
		std::string m_Bold;
		std::string m_Cjk;
	};

	// Resolves the three faces, logging what was picked. A face that cannot be
	// found is returned empty and the text engine deals with it.
	result Resolve(const request &Request);

	// The candidate paths of a face, in the order they are tried and with the
	// override first. A leading "~" is expanded.
	std::vector<std::string> Candidates(face Face, const std::string &Override = {});

	// True when the path names an existing regular file.
	bool Exists(const std::string &Path);
} // namespace engine::fonts

#endif // ENGINE_FONT_FIND_H
