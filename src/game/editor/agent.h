#ifndef GAME_EDITOR_AGENT_H
#define GAME_EDITOR_AGENT_H

/*
	The command line as a tool an agent can drive.

	Two shapes, one implementation:

	  * every administrative method is also a one shot subcommand, so a script
	    or an agent that does not want a long lived process can run
	    `uniteed admin map.get --tiles` and read one JSON document back.

	  * `--agent` keeps a session and speaks one JSON request per line on stdin,
	    one JSON response per line on stdout. That is the mode an agent should
	    prefer: the editor keeps its state between calls, so opening a project,
	    making a dozen edits and exporting is a conversation rather than a
	    dozen process launches.

	The output is always one JSON document, so a caller never has to parse
	prose. Human readable text is what `--brief` adds, and only then.
*/

#include <string>
#include <vector>

#include "base/json.h"

namespace game
{
	namespace admin
	{
		// true when the first argument is an administrative method or one of the
		// agent level commands ("admin.list", "admin.describe"
		bool IsAgentCommand(const std::string &Command);

		// the resident mode: read a JSON request per line, answer with one JSON
		// document per line, until stdin ends or a request says "quit".
		// `Initial` is applied before the first request, so a session can be
		// started with a project already open.
		int RunAgent(const std::vector<std::string> &Arguments);

		// the one shot mode: --key value pairs and a --params JSON object become
		// the request, the answer goes to stdout
		int RunOneShot(const std::string &Method, const std::vector<std::string> &Arguments);

		// the catalogue, for `uniteed --agent-describe`
		std::string DescribeText();
	} // namespace admin
} // namespace game

#endif // GAME_EDITOR_AGENT_H
