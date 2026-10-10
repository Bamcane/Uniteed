/*
	The PNG decoder of the project: SDL has no image loader of its own, and the
	map resources are PNG. stb_image is compiled into its own translation unit
	and kept to internal linkage, so only the two wrappers below have external
	names. It is part of the headless build too, because the command line opens
	maps and packages that carry resources.
*/
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

/*
	The decoder has internal linkage, these two wrappers are the public surface.
*/
extern "C" unsigned char *uniteed_stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	return stbi_load_from_memory(buffer, len, x, y, comp, req_comp);
}

extern "C" void uniteed_stbi_image_free(void *data)
{
	stbi_image_free(data);
}
