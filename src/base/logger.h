#ifndef BASE_LOGGER_H
#define BASE_LOGGER_H

#include <string>

#include "string.h"

namespace base
{
	enum class log_level
	{
		trace,
		info,
		warn,
		error,
	};

	// a minimal stdout logger, the counterpart of the logger module used by the
	// reference projects but without any dependency
	class logger
	{
	public:
		static void SetLevel(log_level Level);
		static log_level Level();
		static void Log(log_level Level, const char *pTag, const std::string &Message);
		static void Shutdown();
	};
} // namespace base

#define LOG_TRACE(Tag, ...) base::logger::Log(base::log_level::trace, Tag, base::Format(__VA_ARGS__))
#define LOG_INFO(Tag, ...) base::logger::Log(base::log_level::info, Tag, base::Format(__VA_ARGS__))
#define LOG_WARN(Tag, ...) base::logger::Log(base::log_level::warn, Tag, base::Format(__VA_ARGS__))
#define LOG_ERROR(Tag, ...) base::logger::Log(base::log_level::error, Tag, base::Format(__VA_ARGS__))

#endif // BASE_LOGGER_H
