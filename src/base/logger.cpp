#include "logger.h"

#include <cstdio>

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

	void logger::Log(log_level Level, const char *pTag, const std::string &Message)
	{
		if(Level < g_Level)
			return;
		std::FILE *pFile = Level >= log_level::warn ? stderr : stdout;
		std::fprintf(pFile, "%s[%-5s] %-12s %s\033[0m\n", LevelColor(Level), LevelName(Level), pTag ? pTag : "", Message.c_str());
		std::fflush(pFile);
	}

	void logger::Shutdown() {}
} // namespace base
