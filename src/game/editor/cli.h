#ifndef GAME_EDITOR_CLI_H
#define GAME_EDITOR_CLI_H

/*
	The headless command line of Uniteed. It is also the test harness: every
	format conversion can be run without a window, which is what the selftest
	uses to verify the round trips.
*/

#include <string>
#include <vector>

namespace game
{
	// handles the command line. Returns:
	//   >= 0  the process should exit with this code
	//   -1    no command was given, the caller may start the editor window
	int RunCommandLine(const std::vector<std::string> &Arguments);

	// the round trip check of every bundled map through every target profile
	int RunSelfTest(const std::vector<std::string> &MapFiles);
} // namespace game

#endif // GAME_EDITOR_CLI_H
