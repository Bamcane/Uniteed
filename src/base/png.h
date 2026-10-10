#ifndef BASE_PNG_H
#define BASE_PNG_H

/*
	A very small PNG writer: 8 bit RGBA, no interlacing, one deflate stream.
	It exists so the editor can save a map thumbnail into a .unitwp package and
	so the screenshot option works without pulling in another library.
*/

#include <cstdint>
#include <string>
#include <vector>

#include "base/storage.h"

namespace base
{
	// pixels are width * height * 4 bytes, RGBA order
	bool WritePng(const storage::path &FileName, int Width, int Height, const storage::byte_vector &Pixels);
} // namespace base

#endif // BASE_PNG_H
