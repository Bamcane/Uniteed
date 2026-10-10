#ifndef BASE_TIME_H
#define BASE_TIME_H

#include <chrono>
#include <cstdint>
#include <ctime>

namespace base
{
	inline std::int64_t UnixTime()
	{
		return static_cast<std::int64_t>(std::time(nullptr));
	}

	inline double Now()
	{
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}

	inline std::string Timestamp()
	{
		const std::time_t Time = std::time(nullptr);
		char aBuffer[64];
		std::tm Tm{};
#if defined(_WIN32)
		localtime_s(&Tm, &Time);
#else
		localtime_r(&Time, &Tm);
#endif
		std::strftime(aBuffer, sizeof(aBuffer), "%Y-%m-%d %H:%M:%S", &Tm);
		return aBuffer;
	}
} // namespace base

#endif // BASE_TIME_H
