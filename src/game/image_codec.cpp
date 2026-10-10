/*
	stb_image is compiled into its own translation unit with STB_IMAGE_STATIC so
	that the copy nanovg carries (which is compiled with the public symbols) does
	not clash with this one. Only the PNG decoder is used, and it is needed by
	the headless build as well.
*/
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

/*
	The decoder has internal linkage (STB_IMAGE_STATIC) so it cannot clash with
	the copy nanovg compiles; these two wrappers are the public surface.
*/
extern "C" unsigned char *uniteed_stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	return stbi_load_from_memory(buffer, len, x, y, comp, req_comp);
}

extern "C" void uniteed_stbi_image_free(void *data)
{
	stbi_image_free(data);
}
