#ifndef ENGINE_EDITOR_APP_H
#define ENGINE_EDITOR_APP_H

/*
	The editor application: window, main loop and the offscreen screenshot mode
	used to smoke test the interface without a display interaction.
*/

#include <string>

namespace engine
{
	// an empty map path starts with a new map; a non empty screenshot path
	// renders one frame into that PNG and exits
	int RunEditor(const std::string &MapPath, const std::string &ScreenshotPath = "");
} // namespace engine

#endif // ENGINE_EDITOR_APP_H
