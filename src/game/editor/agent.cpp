/*
	The one shot subcommands and the resident agent mode.

	The argument parsing is deliberately dull: `--key value` becomes a string
	member, `--key` becomes true, and a value that is valid JSON is used as
	JSON rather than as a string. That last rule is what lets
	`--region '[0,0,4,4]'` and `--entity spawn` both read naturally, and it
	means a caller can always fall back to `--params '<the whole object>'`.
*/

#include "agent.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "base/define.h"
#include "base/logger.h"
#include "base/string.h"
#include "game/editor/admin.h"
#include "game/editor/admin_ops.h"
#include "game/editor/editor.h"
#include "game/editor/cli.h"
#include "game/map/envelope.h"

namespace game::admin
{
	namespace
	{
		// the agent level commands, which are not editor methods
		bool IsAgentLevel(const std::string &Command)
		{
			return Command == "agent" || Command == "agent.list" || Command == "admin.list" ||
				Command == "agent.describe" || Command == "admin.describe" || Command == "--agent-describe";
		}

		// a value that parses as JSON is used as JSON, so numbers stay numbers
		base::json ValueFromText(const std::string &Text)
		{
			if(Text.empty())
				return std::string();
			// only try the parse when the text starts like a JSON value; this
			// keeps a plain word from being reported as a syntax error
			const char Front = Text.front();
			if(Front == '{' || Front == '[' || Front == '"' || Front == '-' || (Front >= '0' && Front <= '9') ||
				Text == "true" || Text == "false" || Text == "null")
			{
				std::string Error;
				const base::json Parsed = base::json::parse(Text, &Error);
				if(Error.empty())
					return Parsed;
			}
			return Text;
		}

		// --key value pairs and --params, into a parameter object
		base::json ParamsFromArguments(const std::vector<std::string> &Arguments)
		{
			base::json Params = base::json::object();
			for(std::size_t i = 0; i < Arguments.size(); i++)
			{
				const std::string &Argument = Arguments[i];
				if(!base::StartsWith(Argument, "--"))
					continue;
				std::string Key = Argument.substr(2);
				// --key=value is accepted beside --key value
				const std::size_t Equals = Key.find('=');
				if(Equals != std::string::npos)
				{
					const std::string Text = Key.substr(Equals + 1);
					Key = Key.substr(0, Equals);
					if(Key == "params")
					{
						const base::json Extra = ValueFromText(Text);
						if(Extra.is_object())
							for(const std::string &Member : Extra.keys())
								Params[Member] = Extra[Member];
					}
					else
						Params[Key] = ValueFromText(Text);
					continue;
				}
				if(i + 1 < Arguments.size() && !base::StartsWith(Arguments[i + 1], "--"))
				{
					const std::string Text = Arguments[++i];
					if(Key == "params")
					{
						const base::json Extra = ValueFromText(Text);
						if(Extra.is_object())
							for(const std::string &Member : Extra.keys())
								Params[Member] = Extra[Member];
					}
					else
						Params[Key] = ValueFromText(Text);
				}
				else
				{
					// a bare flag is a boolean
					Params[Key] = true;
				}
			}
			return Params;
		}

		// the member names of an object, for the "did you mean" hint
		std::string MemberHint(const std::string &Method)
		{
			const ops::CDescriptor *pDescriptor = ops::Find(Method);
			if(pDescriptor == nullptr)
				return {};
			return std::string("parameters: ") + (pDescriptor->m_pParams[0] == '\0' ? "(none)" : pDescriptor->m_pParams);
		}
	} // namespace

	bool IsAgentCommand(const std::string &Command)
	{
		if(Command.empty() || Command[0] != '-')
			return ops::Find(Command) != nullptr;
		// the option style spelling, --map.get
		if(base::StartsWith(Command, "--"))
			return IsAgentLevel(Command) || ops::Find(Command.substr(2)) != nullptr;
		return false;
	}

	std::string DescribeText()
	{
		std::string Out = "Uniteed agent methods\n\n";
		Out += "Each one is a subcommand: uniteed <method> [--key value ...]\n";
		Out += "Or send them to a resident session: uniteed --agent\n\n";
		for(const ops::CDescriptor &Descriptor : ops::Table())
		{
			Out += base::Format("{}\n", Descriptor.m_pName);
			if(Descriptor.m_pParams[0] != '\0')
				Out += base::Format("    {}\n", Descriptor.m_pParams);
			Out += base::Format("    {}\n\n", Descriptor.m_pDescription);
		}
		return Out;
	}

	int RunOneShot(const std::string &Method, const std::vector<std::string> &Arguments)
	{
		const std::string Name = base::StartsWith(Method, "--") ? Method.substr(2) : Method;

		// a session of its own: the process is the session, so a one shot call
		// has nothing to keep
		CSession Session;
		base::json Request = base::json::object();
		Request["method"] = Name;
		Request["params"] = ParamsFromArguments(Arguments);

		const base::json Answer = Session.Handle(Request);
		std::cout << Answer.dump(1) << "\n";
		if(Answer["ok"].as_bool(false))
			return 0;
		std::cerr << base::Format("{}: {}\n", Name.c_str(), Answer["error"].as_string().c_str());
		const std::string Hint = MemberHint(Name);
		if(!Hint.empty())
			std::cerr << Hint << "\n";
		return 1;
	}

	int RunAgent(const std::vector<std::string> &Arguments)
	{
		CSession Session;

		/*
			Everything before --agent that looks like a request is applied first,
			so `uniteed --agent project.open --path x.map` starts with the project
			open. The rest of the command line is then handled by the resident
			loop, which is the same code path as a request on stdin.
		*/
		const base::json Initial = ParamsFromArguments(Arguments);
		std::string InitialMethod;
		for(std::size_t i = 0; i < Arguments.size(); i++)
			if(ops::Find(Arguments[i]) != nullptr)
			{
				InitialMethod = Arguments[i];
				break;
			}
		if(!InitialMethod.empty())
		{
			base::json Request = base::json::object();
			Request["method"] = InitialMethod;
			Request["params"] = Initial;
			const base::json Answer = Session.Handle(Request);
			if(!Answer["ok"].as_bool(false))
			{
				std::cerr << base::Format("the initial {} failed: {}\n", InitialMethod.c_str(),
					Answer["error"].as_string().c_str());
				return 1;
			}
		}

		// a banner goes to stderr, so stdout stays one JSON document per line
		std::cerr << base::Format("{} {} agent: {:d} methods, one JSON request per line\n",
			GAME_DISPLAY_NAME, GAME_VERSION, ops::Table().size());
		std::cerr << "send {\"method\":\"agent.describe\"} for the list, or {\"method\":\"quit\"} to stop\n";
		std::cout.flush();

		std::string Line;
		while(std::getline(std::cin, Line))
		{
			const std::string Trimmed = base::Trim(Line);
			if(Trimmed.empty())
				continue;

			std::string Error;
			const base::json Request = base::json::parse(Trimmed, &Error);
			base::json Answer = base::json::object();
			if(!Error.empty())
			{
				Answer["ok"] = false;
				Answer["error"] = Error;
				std::cout << Answer.dump(-1) << "\n";
				std::cout.flush();
				continue;
			}

			// a bare method name is accepted as a shorthand, which makes the
			// protocol usable by hand
			base::json Effective = Request;
			if(Request.is_string())
			{
				Effective = base::json::object();
				Effective["method"] = Request;
				Effective["params"] = base::json::object();
			}
			const std::string Method = Effective["method"].as_string();
			if(Method == "quit" || Method == "exit" || Method == "agent.quit")
			{
				Answer["ok"] = true;
				Answer["method"] = Method;
				Answer["result"] = base::json::object();
				std::cout << Answer.dump(-1) << "\n";
				std::cout.flush();
				break;
			}

			Answer = Session.Handle(Effective);
			std::cout << Answer.dump(-1) << "\n";
			std::cout.flush();
		}
		return 0;
	}
} // namespace game::admin
