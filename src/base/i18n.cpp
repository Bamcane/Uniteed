#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <optional>
#include <unordered_map>

#include "base/json.h"
#include "base/logger.h"
#include "base/string.h"

namespace base::i18n
{
	namespace
	{
		/*
			An entry never moves once it is in the deque, which is what lets Tr()
			hand out a pointer into the catalog: switching the language appends
			rather than replaces, because the frame the switch happens on is
			still drawing the strings it looked up a moment ago.
		*/
		std::deque<std::string> s_Strings;
		std::unordered_map<std::string, std::size_t> s_Index;
		std::vector<CLanguage> s_Languages;
		std::string s_Current = "en";

		storage::path LanguageDirectory() { return storage::GetDataDirectory() / "lang"; }

		// the id a document names itself with, or the file name when it does
		// not: "zh-Hans.json" is the language "zh-Hans"
		std::string IdFromFile(const storage::path &File, const json &Document)
		{
			const std::string Named = Document.Get("id").as_string();
			return Named.empty() ? File.stem().string() : Named;
		}
	} // namespace

	void RefreshLanguages()
	{
		s_Languages.clear();
		// English is the source text: there is no document for it, and it
		// cannot be missing
		s_Languages.push_back(CLanguage{"en", "English", storage::path()});

		std::vector<storage::path> Files = storage::list_directory(LanguageDirectory(), ".json");
		// the list is the order a menu shows, so it cannot depend on what the
		// file system happened to hand back
		std::sort(Files.begin(), Files.end());
		for(const storage::path &File : Files)
		{
			const std::optional<std::string> Text = storage::read_text(File);
			if(!Text.has_value())
				continue;
			std::string Error;
			const json Document = json::parse(*Text, &Error);
			if(!Document.is_object())
			{
				LOG_WARN("i18n", "'{}' is not a language document: {}", File.filename().string().c_str(), Error);
				continue;
			}
			const std::string Id = IdFromFile(File, Document);
			if(Id.empty() || Id == "en")
				continue;
			s_Languages.push_back(CLanguage{Id, Document.Get("name").as_string(Id), File});
		}
		LOG_INFO("i18n", "{} languages installed", s_Languages.size());
	}

	const std::vector<CLanguage> &Languages()
	{
		if(s_Languages.empty())
			RefreshLanguages();
		return s_Languages;
	}

	const std::string &CurrentId() { return s_Current; }

	bool SetLanguage(std::string_view Id)
	{
		if(Id == "en")
		{
			// the source text *is* the English interface: there is nothing to
			// look up, and an entry loaded earlier must not answer
			s_Index.clear();
			s_Current = "en";
			return true;
		}

		const auto It = std::find_if(Languages().begin(), Languages().end(),
			[Id](const CLanguage &Language) { return Language.m_Id == Id; });
		if(It == Languages().end() || It->m_Path.empty())
			return false;
		const std::optional<std::string> Text = storage::read_text(It->m_Path);
		if(!Text.has_value())
			return false;
		std::string Error;
		const json Document = json::parse(*Text, &Error);
		const json &Strings = Document.Get("strings");
		if(!Strings.is_object())
		{
			LOG_WARN("i18n", "'{}' has no 'strings' object: {}", It->m_Path.filename().string().c_str(), Error);
			return false;
		}

		std::unordered_map<std::string, std::size_t> Loaded;
		for(const std::string &Source : Strings.keys())
		{
			const std::string Value = Strings[Source].as_string();
			if(Value.empty())
				continue;
			s_Strings.push_back(Value);
			Loaded[Source] = s_Strings.size() - 1;
		}

		s_Index = std::move(Loaded);
		s_Current = It->m_Id;
		LOG_INFO("i18n", "interface language '{}' from '{}', {} strings", s_Current,
			It->m_Path.filename().string().c_str(), s_Index.size());
		return true;
	}

	std::string SystemLanguageId()
	{
		/*
			The POSIX variables a session sets, in the order they win. Only the
			plain "zh" a Chinese desktop asks for is recognised: this build ships
			one translation, and guessing at the rest of a locale would be
			pretending to more than there is.
		*/
		for(const char *pName : {"LC_ALL", "LC_MESSAGES", "LANG"})
		{
			const char *pValue = std::getenv(pName);
			if(pValue == nullptr || *pValue == '\0')
				continue;
			std::string Value(pValue);
			for(char &Character : Value)
				Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
			return Value.compare(0, 2, "zh") == 0 ? "zh-Hans" : "en";
		}
		return "en";
	}

	const char *Tr(const char *pSource)
	{
		if(pSource == nullptr || s_Index.empty())
			return pSource;
		const auto It = s_Index.find(pSource);
		return It == s_Index.end() ? pSource : s_Strings[It->second].c_str();
	}

	std::size_t EntryCount() { return s_Index.size(); }
} // namespace base::i18n
