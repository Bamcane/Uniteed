#include "uuid.h"

#include <atomic>

#include "base/string.h"
#include "base/time.h"

/*
	libsugarx ships the UUID generators as a header, and the implementation is
	enabled by exactly one translation unit. This is that translation unit, so
	nothing else has to know about the macro or about OpenSSL.
*/
#if defined(UNITEED_OPENSSL)
#define LIBSUGARX_UUID_GENERATION_IMPL
#include <sugar_uuid.h>
#endif

namespace base
{
	std::string NewUuid()
	{
#if defined(UNITEED_OPENSSL)
		return std::string(libsugarx::uuid::generate_v4_nullable().to_string().view());
#else
		// no cryptographic random source at build time: the identity is still
		// unique enough to recognise a file on one machine
		static std::atomic<unsigned> Counter{0};
		return Format("local-%lld-%08x", static_cast<long long>(UnixTime()), Counter.fetch_add(1));
#endif
	}

	bool LooksLikeUuid(std::string_view Text)
	{
		if(Text.size() != 36)
			return false;
		for(std::size_t i = 0; i < Text.size(); i++)
		{
			const char c = Text[i];
			if(i == 8 || i == 13 || i == 18 || i == 23)
			{
				if(c != '-')
					return false;
			}
			else if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
				return false;
		}
		return true;
	}
} // namespace base
