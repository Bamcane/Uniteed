#ifndef BASE_UUID_H
#define BASE_UUID_H

/*
	One place that makes the random identities the project and the package
	share: a map can be recognised by the UUID it carries, whether it is read
	from a .unitwp container or from the project that produced it.
*/

#include <string>
#include <string_view>

namespace base
{
	// a version 4 UUID in its canonical text form, for example
	// "6a1f0d3c-1a2b-4c5d-8e9f-0123456789ab"
	std::string NewUuid();

	// true when the text looks like one, used when a client hands an identity
	// back and a malformed one should not become a project id
	bool LooksLikeUuid(std::string_view Text);
} // namespace base

#endif // BASE_UUID_H
