#include <string>
#include <vector>

#include "base/define.h"
#include "base/logger.h"
#include "base/storage.h"
#include "game/assets.h"
#include "game/editor/cli.h"
#include "game/map/map.h"

#if UNITEED_GUI
#include "engine/editor_app.h"
#endif

int main(int argc, char **argv)
{
	std::vector<std::string> Arguments;
	Arguments.reserve(static_cast<std::size_t>(argc));
	for(int i = 1; i < argc; i++)
		Arguments.emplace_back(argv[i]);

	// the first non option is the map to open in the editor
	std::string MapToOpen;
	std::string Screenshot;
	for(std::size_t i = 0; i < Arguments.size(); i++)
	{
		const std::string &Argument = Arguments[i];
		if(Argument == "--screenshot" && i + 1 < Arguments.size())
		{
			Screenshot = Arguments[++i];
			continue;
		}
		if(!Argument.empty() && Argument[0] != '-' && Argument.find('.') != std::string::npos)
		{
			if(MapToOpen.empty())
				MapToOpen = Argument;
		}
	}

	if(const char *pLevel = std::getenv("UNITEED_LOG"))
	{
		const std::string Level = pLevel;
		if(Level == "trace")
			base::logger::SetLevel(base::log_level::trace);
		else if(Level == "info")
			base::logger::SetLevel(base::log_level::info);
		else if(Level == "warn")
			base::logger::SetLevel(base::log_level::warn);
		else if(Level == "error")
			base::logger::SetLevel(base::log_level::error);
	}

	LOG_INFO("uniteed", "%s %s, data directory '%s'", GAME_DISPLAY_NAME, GAME_VERSION, storage::GetDataDirectory().string().c_str());

	// the screenshot option always goes to the editor, even with a map argument
	if(!Screenshot.empty())
	{
#if UNITEED_GUI
		return engine::RunEditor(MapToOpen, Screenshot);
#else
		LOG_ERROR("uniteed", "this build has no editor window");
		return 1;
#endif
	}

	const int Result = game::RunCommandLine(Arguments);
	if(Result >= 0)
		return Result;

#if UNITEED_GUI
	return engine::RunEditor(MapToOpen, Screenshot);
#else
	(void)MapToOpen;
	(void)Screenshot;
	LOG_ERROR("uniteed", "this build has no editor window, run with --help for the command line");
	return 1;
#endif
}
