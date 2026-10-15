#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

#include "base/define.h"
#include "base/logger.h"
#include "base/storage.h"
#include "game/assets.h"
#include "game/editor/admin.h"
#include "game/editor/agent.h"
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
	/*
		The screenshot options pick the state the frame is rendered in. The
		editor has no scripting layer, so this is how the tile canvas and the
		quad canvas can each be captured without a mouse. They only exist in
		the window build, so the parsing and the state are guarded.
	*/
#if UNITEED_GUI
	engine::CScreenshotSetup Setup;
	std::vector<engine::CInputStep> Script;
	int PendingModifiers = 0;
#endif
	for(std::size_t i = 0; i < Arguments.size(); i++)
	{
		const std::string &Argument = Arguments[i];
		if(Argument == "--screenshot" && i + 1 < Arguments.size())
		{
			Screenshot = Arguments[++i];
			continue;
		}
#if UNITEED_GUI
		if(Argument == "--layer" && i + 1 < Arguments.size())
		{
			const std::string Value = Arguments[++i];
			const std::size_t Colon = Value.find(':');
			Setup.m_Group = std::atoi(Value.substr(0, Colon).c_str());
			// "G:" with nothing after the colon is the group row itself
			Setup.m_bGroupSelected = Colon != std::string::npos && Colon + 1 == Value.size();
			Setup.m_Layer = (Colon == std::string::npos || Setup.m_bGroupSelected) ? -1 : std::atoi(Value.substr(Colon + 1).c_str());
			continue;
		}
		if(Argument == "--quad" && i + 1 < Arguments.size())
		{
			Setup.m_Quad = std::atoi(Arguments[++i].c_str());
			continue;
		}
		if(Argument == "--corner" && i + 1 < Arguments.size())
		{
			Setup.m_Corner = std::atoi(Arguments[++i].c_str());
			continue;
		}
		if(Argument == "--dock" && i + 1 < Arguments.size())
		{
			// "left:envelopes" or just "envelopes" for the right side
			const std::string Value = Arguments[++i];
			const std::size_t Colon = Value.find(':');
			if(Colon == std::string::npos)
				Setup.m_RightDock = Value;
			else
			{
				Setup.m_LeftDock = Value.substr(0, Colon);
				Setup.m_RightDock = Value.substr(Colon + 1);
			}
			continue;
		}
		if(Argument == "--envelope" && i + 1 < Arguments.size())
		{
			const std::string Value = Arguments[++i];
			const std::size_t Colon = Value.find(':');
			Setup.m_Envelope = std::atoi(Value.substr(0, Colon).c_str());
			if(Colon != std::string::npos)
				Setup.m_EnvelopePoint = std::atoi(Value.substr(Colon + 1).c_str());
			continue;
		}
		if(Argument == "--color")
		{
			Setup.m_bOpenColorPicker = true;
			continue;
		}
		if(Argument == "--time" && i + 1 < Arguments.size())
		{
			Setup.m_EnvelopeTime = static_cast<float>(std::atof(Arguments[++i].c_str()));
			continue;
		}
		if(Argument == "--ui-scale-change" && i + 1 < Arguments.size())
		{
			// the same, but applied after the first frame, so the icons have to
			// notice that the display changed under them
			Setup.m_UiScaleChange = static_cast<float>(std::atof(Arguments[++i].c_str()));
			continue;
		}
		if(Argument == "--quit-after" && i + 1 < Arguments.size())
		{
			// run the window for this many frames and quit as if it had been
			// closed, which is what a test uses to reach the settings it saves
			Setup.m_QuitAfter = std::atoi(Arguments[++i].c_str());
			continue;
		}
		if(Argument == "--language" && i + 1 < Arguments.size())
		{
			/*
				The catalog id of the language the interface is built in, so a
				picture of the interface in another language is one command line
				away and does not depend on the settings of whoever runs it.
			*/
			Setup.m_Language = Arguments[++i];
			continue;
		}
		if(Argument == "--menu" && i + 1 < Arguments.size())
		{
			// "files" or "settings": the menu of the title bar row to open
			Setup.m_Menu = Arguments[++i];
			continue;
		}
		if(Argument == "--titlebar" && i + 1 < Arguments.size())
		{
			/*
				"system" brings the platform's own title bar back, "client"
				asks for the drawn one. Both exist because a compositor that
				will not move a borderless window is not something the editor
				can detect: the flag is the way out.
			*/
			const std::string Value = Arguments[++i];
			Setup.m_ClientDecorations = Value == "system" ? 0 : 1;
			continue;
		}
		if(Argument == "--reopen" && i + 1 < Arguments.size())
		{
			// another map, opened after the first frame: a map switch
			Setup.m_OpenAfter = Arguments[++i];
			continue;
		}
		if(Argument == "--ui-scale" && i + 1 < Arguments.size())
		{
			/*
				The resolution the vector drawings of the interface are
				rasterized at. The real editor takes it from the display, and
				this takes it from the command line so a scaled display can be
				reproduced on any machine.
			*/
			Setup.m_UiScale = static_cast<float>(std::atof(Arguments[++i].c_str()));
			continue;
		}
		if(Argument == "--mods" && i + 1 < Arguments.size())
		{
			// the SDL_Keymod bits, so a scripted Ctrl+Z really is a Ctrl+Z: the
			// modifiers are pushed and released inside the same step as the key
			PendingModifiers = std::atoi(Arguments[++i].c_str());
			continue;
		}
		if(Argument == "--press" && i + 1 < Arguments.size())
		{
			engine::CInputStep Step;
			Step.m_PressButton = std::atoi(Arguments[++i].c_str());
			Script.push_back(Step);
			continue;
		}
		if(Argument == "--release" && i + 1 < Arguments.size())
		{
			engine::CInputStep Step;
			Step.m_ReleaseButton = std::atoi(Arguments[++i].c_str());
			Script.push_back(Step);
			continue;
		}
		if(Argument == "--move" && i + 1 < Arguments.size())
		{
			const std::string Value = Arguments[++i];
			int X = 0, Y = 0;
			if(std::sscanf(Value.c_str(), "%d,%d", &X, &Y) >= 2)
			{
				engine::CInputStep Move;
				Move.m_Move = base::vec2(static_cast<float>(X), static_cast<float>(Y));
				Script.push_back(Move);
			}
			continue;
		}
		if(Argument == "--scroll" && i + 1 < Arguments.size())
		{
			// the wheel, which is how a scripted run zooms the canvas
			engine::CInputStep Step;
			Step.m_Scroll = std::atoi(Arguments[++i].c_str());
			Script.push_back(Step);
			continue;
		}
		if(Argument == "--click" && i + 1 < Arguments.size())
		{
			// x,y[,button]: one frame that moves the cursor and clicks, which
			// is how the interaction behind a screenshot is scripted
			const std::string Value = Arguments[++i];
			int X = 0, Y = 0, Button = 1;
			if(std::sscanf(Value.c_str(), "%d,%d,%d", &X, &Y, &Button) >= 2)
			{
				engine::CInputStep Move;
				Move.m_Move = base::vec2(static_cast<float>(X), static_cast<float>(Y));
				Script.push_back(Move);
				engine::CInputStep Click;
				Click.m_PressButton = Button;
				Script.push_back(Click);
				engine::CInputStep Release;
				Release.m_ReleaseButton = Button;
				Script.push_back(Release);
			}
			continue;
		}
		if(Argument == "--click-point" && i + 1 < Arguments.size())
		{
			/*
				quad:point[,button]: the same click, aimed at a point of a quad of
				the selected layer instead of at a pixel - 0..3 are the corners
				and 4 the pivot. It is how a script colours a corner, which is a
				right click on the point, without knowing where the corner is on
				screen; the editor is asked for that while the script runs.
			*/
			const std::string Value = Arguments[++i];
			int Quad = 0, Point = 0, Button = 1;
			if(std::sscanf(Value.c_str(), "%d:%d,%d", &Quad, &Point, &Button) >= 2)
			{
				engine::CInputStep Move;
				Move.m_QuadPoint = base::ivec2(Quad, Point);
				Script.push_back(Move);
				engine::CInputStep Click;
				Click.m_PressButton = Button;
				Script.push_back(Click);
				engine::CInputStep Release;
				Release.m_ReleaseButton = Button;
				Script.push_back(Release);
			}
			continue;
		}
		if(Argument == "--double-click" && i + 1 < Arguments.size())
		{
			/*
				The two presses of a double click, counted the way the platform
				counts them: the first selects, the second is the one a name
				field in the layer tree opens on.
			*/
			const std::string Value = Arguments[++i];
			int X = 0, Y = 0, Button = 1;
			if(std::sscanf(Value.c_str(), "%d,%d,%d", &X, &Y, &Button) >= 2)
			{
				engine::CInputStep Move;
				Move.m_Move = base::vec2(static_cast<float>(X), static_cast<float>(Y));
				Script.push_back(Move);
				engine::CInputStep First;
				First.m_PressButton = Button;
				Script.push_back(First);
				engine::CInputStep FirstUp;
				FirstUp.m_ReleaseButton = Button;
				Script.push_back(FirstUp);
				Script.push_back(Move);
				engine::CInputStep Second;
				Second.m_PressButton = Button;
				Second.m_Clicks = 2;
				Script.push_back(Second);
				engine::CInputStep SecondUp;
				SecondUp.m_ReleaseButton = Button;
				Script.push_back(SecondUp);
			}
			continue;
		}
		if(Argument == "--type" && i + 1 < Arguments.size())
		{
			// text for the field the script has just opened
			engine::CInputStep Step;
			Step.m_TypeText = Arguments[++i];
			Script.push_back(Step);
			continue;
		}
		if(Argument == "--key" && i + 1 < Arguments.size())
		{
			/*
				One key press, with the release in the same frame: a later --key
				has to read as a *new* press, and a key that stayed down would
				look like a repeat. --press/--release are the options for a key
				that genuinely has to be held.
			*/
			const std::string Value = Arguments[++i];
			engine::CInputStep Step;
			Step.m_PressKey = std::atoi(Value.c_str());
			Step.m_ReleaseKey = Step.m_PressKey;
			Step.m_Modifiers = PendingModifiers;
			PendingModifiers = 0;
			Script.push_back(Step);
			continue;
		}
#endif // UNITEED_GUI
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

	LOG_INFO("uniteed", "{} {}, data directory '{}'", GAME_DISPLAY_NAME, GAME_VERSION, storage::GetDataDirectory().string().c_str());

	/*
		The agent interface comes first: a method name is a valid command, and
		so is --agent. Both share one implementation, and neither opens a
		window, which is what makes the editor usable from a script.
	*/
	{
		std::string Command = Arguments.empty() ? std::string() : Arguments.front();
		if(Command == "--agent-describe" || Command == "agent.describe" || Command == "admin.describe" ||
			Command == "agent.list" || Command == "admin.list")
		{
			if(Command == "agent.describe" || Command == "admin.describe" || Command == "--agent-describe")
				std::cout << game::admin::DescribeText();
			else
				std::cout << game::admin::CSession::Describe().dump(1) << "\n";
			return 0;
		}
		if(Command == "--agent")
		{
			// the arguments after the flag configure the session before the
			// first request
			std::vector<std::string> Rest(Arguments.begin() + 1, Arguments.end());
			return game::admin::RunAgent(Rest);
		}
		if(game::admin::IsAgentCommand(Command))
		{
			std::vector<std::string> Rest(Arguments.begin() + 1, Arguments.end());
			return game::admin::RunOneShot(Command, Rest);
		}
	}

	/*
		The options the editor window owns. They are read here and handed to
		the window, and the command line does not know any of them: handing it
		a copy would be read as an unknown command, which is what used to
		happen to `uniteed --ui-scale 2`.
	*/
	const auto IsWindowOption = [](const std::string &Argument) {
		return Argument == "--screenshot" || Argument == "--layer" || Argument == "--quad" ||
			Argument == "--corner" || Argument == "--dock" || Argument == "--envelope" ||
			Argument == "--color" || Argument == "--time" || Argument == "--ui-scale" ||
			Argument == "--ui-scale-change" || Argument == "--titlebar" || Argument == "--menu" ||
			Argument == "--language" ||
			Argument == "--reopen" || Argument == "--mods" || Argument == "--press" ||
			Argument == "--release" || Argument == "--move" || Argument == "--scroll" ||
			Argument == "--click" || Argument == "--click-point" || Argument == "--key" || Argument == "--quit-after" ||
			Argument == "--double-click" || Argument == "--type";
	};
	const bool bWindowOptions = !Arguments.empty() && IsWindowOption(Arguments.front());

	// the screenshot option always goes to the editor, even with a map argument
	if(!Screenshot.empty())
	{
#if UNITEED_GUI
		if(!Script.empty())
			return engine::RunEditorScript(MapToOpen, Screenshot, Setup, Script);
		return engine::RunEditor(MapToOpen, Screenshot, Setup);
#else
		LOG_ERROR("uniteed", "this build has no editor window");
		return 1;
#endif
	}

	// a command line that starts with one of the editor's own options is the
	// editor, not a command
	if(!bWindowOptions)
	{
		const int Result = game::RunCommandLine(Arguments);
		if(Result >= 0)
			return Result;
	}

#if UNITEED_GUI
	/*
		The window gets the same options as the offscreen modes: the interface
		scale and the choice of title bar are things a user sets from the
		command line, not only from a screenshot script.
	*/
	return engine::RunEditor(MapToOpen, Screenshot, Setup);
#else
	(void)MapToOpen;
	(void)Screenshot;
	LOG_ERROR("uniteed", "this build has no editor window, run with --help for the command line");
	return 1;
#endif
}
