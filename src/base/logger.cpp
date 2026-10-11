#include "logger.h"

#include <cstdio>
#include <cstdlib>

namespace base
{
	namespace
	{
		log_level g_Level = log_level::info;

		const char *LevelName(log_level Level)
		{
			switch(Level)
			{
			case log_level::trace: return "trace";
			case log_level::info: return "info";
			case log_level::warn: return "warn";
			case log_level::error: return "error";
			}
			return "?";
		}

		const char *LevelColor(log_level Level)
		{
			switch(Level)
			{
			case log_level::trace: return "\033[90m";
			case log_level::info: return "\033[0m";
			case log_level::warn: return "\033[33m";
			case log_level::error: return "\033[31m";
			}
			return "\033[0m";
		}
	} // namespace

	void logger::SetLevel(log_level Level) { g_Level = Level; }
	log_level logger::Level() { return g_Level; }

	namespace
	{
		/*
			Colour only when a human is likely to read it. An agent reads the
			program's output, and an escape sequence in the middle of a log line
			is noise at best; `NO_COLOR` in the environment turns it off as well,
			which is the convention every terminal tool follows.
		*/
		bool UseColour(std::FILE *pFile)
		{
			if(std::getenv("NO_COLOR") != nullptr)
				return false;
			if(std::getenv("UNITEED_NO_COLOR") != nullptr)
				return false;
			return std::ferror(pFile) == 0;
		}
	} // namespace

	void logger::Log(log_level Level, const char *pTag, const std::string &Message)
	{
		if(Level < g_Level)
			return;
		/*
			Every log line goes to stderr. The program's stdout is a document:
			the agent protocol is one JSON object per line, and the command line
			writes its answer there. A progress line mixed into that stream would
			make the output unparseable, so the two are never the same file.
		*/
		std::FILE *pFile = stderr;
		const bool bColour = UseColour(pFile);
		std::fprintf(pFile, "%s[%-5s] %-12s %s%s\n", bColour ? LevelColor(Level) : "",
			LevelName(Level), pTag ? pTag : "", Message.c_str(), bColour ? "\033[0m" : "");
		std::fflush(pFile);
	}

	void logger::Shutdown() {}
} // namespace base
