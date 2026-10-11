/*
	Rules as JSON, and as the DDRaceNetwork `.rules` text.

	The JSON shape is the one the CLI and the agent interface document, and it
	is deliberately flat and explicit so a rule can be written by hand:

	  {
	    "autorules": 1,
	    "name": "grass on death",
	    "description": "...",
	    "tileset": "grass_main",
	    "configs": [
	      {
	        "name": "main",
	        "region": [0, 0, 0, 0],          // x, y, width, height; 0 0 0 0 = whole layer
	        "runs": [
	          {
	            "name": "run 1",
	            "read": 6, "target": -1, "copy": true, "reference": 0,
	            "rules": [
	              {
	                "tile": 1, "flags": ["hflip", "rotate"],
	                "name": "hookable",
	                "when": [ { "at": [1, 0], "not": [0] } ],
	                "modulo": [ { "every": [3, 1], "offset": [0, 0] } ],
	                "random": 1.0, "skip_empty": false, "skip_full": false
	              }
	            ]
	          }
	        ]
	      }
	    ]
	  }

	A tile is either a number or a role name: `"tile": "death"` and
	`"tile": 2` mean the same thing. The flag list accepts "hflip", "vflip" and
	"rotate" as well as the raw bitmask under "flag_bits".
*/

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <string>

#include "base/logger.h"
#include "base/string.h"
#include "game/rules/rules.h"

namespace game::rules
{
	namespace
	{
		const char *const g_apFlagNames[] = {"hflip", "vflip", "rotate"};

		int FlagBitFromName(std::string_view Name)
		{
			if(Name == "hflip" || Name == "xflip")
				return 0x1;
			if(Name == "vflip" || Name == "yflip")
				return 0x2;
			if(Name == "rotate")
				return 0x8;
			return 0;
		}

		base::json FlagsToJson(int Flags)
		{
			base::json List = base::json::array();
			for(const char *pName : g_apFlagNames)
				if((Flags & FlagBitFromName(pName)) != 0)
					List.push_back(std::string(pName));
			if((Flags & ~(0x1 | 0x2 | 0x8)) != 0)
			{
				// anything the names cannot express is kept as the raw mask
				base::json Object = base::json::object();
				Object["flag_bits"] = Flags;
				List.push_back(std::move(Object));
			}
			return List;
		}

		int FlagsFromJson(const base::json &Value)
		{
			if(Value.is_number())
				return static_cast<int>(Value.as_int());
			if(!Value.is_array())
				return 0;
			int Flags = 0;
			for(std::size_t i = 0; i < Value.size(); i++)
			{
				const base::json &Entry = Value.at(i);
				if(Entry.is_string())
					Flags |= FlagBitFromName(Entry.as_string());
				else if(Entry.is_object())
					Flags |= static_cast<int>(Entry["flag_bits"].as_int());
			}
			return Flags;
		}

		// a tile is a number or a role name
		base::json TileToJson(int Tile, int Role)
		{
			if(Role >= 0)
				return std::string(RoleName(static_cast<tile_role>(Role)));
			return Tile;
		}

		void TileFromJson(const base::json &Value, int &Tile, int &Role)
		{
			if(Value.is_string())
			{
				const tile_role Parsed = RoleFromName(Value.as_string());
				if(Parsed != tile_role::custom)
				{
					Role = static_cast<int>(Parsed);
					Tile = RoleTileIndex(Parsed);
					return;
				}
				// an unknown name is a hard zero rather than a silent role
				Role = -1;
				Tile = 0;
				return;
			}
			Role = -1;
			Tile = static_cast<int>(Value.as_int());
		}

		base::json IndexInfoToJson(const CIndexInfo &Info)
		{
			base::json Object = base::json::object();
			Object["tile"] = TileToJson(Info.m_Tile, Info.m_Role);
			if(Info.m_Flag != 0 || Info.m_bTestFlag)
			{
				Object["flags"] = FlagsToJson(Info.m_Flag);
				Object["test_flags"] = Info.m_bTestFlag;
			}
			return Object;
		}

		CIndexInfo IndexInfoFromJson(const base::json &Value)
		{
			CIndexInfo Info;
			if(Value.is_object())
			{
				TileFromJson(Value["tile"], Info.m_Tile, Info.m_Role);
				Info.m_Flag = FlagsFromJson(Value["flags"]);
				Info.m_bTestFlag = Value["test_flags"].as_bool(false);
			}
			else
			{
				TileFromJson(Value, Info.m_Tile, Info.m_Role);
			}
			return Info;
		}

		// a "when" entry: an offset and the tiles that must (or must not) be there
		base::json PosRuleToJson(const CPosRule &Rule)
		{
			base::json Object = base::json::object();
			base::json At = base::json::array();
			At.push_back(Rule.m_X);
			At.push_back(Rule.m_Y);
			Object["at"] = std::move(At);
			if(Rule.m_bGuide)
				Object["guide"] = true;
			if(Rule.m_Mode == pos_rule_mode::none)
				return Object;

			base::json List = base::json::array();
			for(const CIndexInfo &Info : Rule.m_vIndices)
				List.push_back(IndexInfoToJson(Info));
			Object[Rule.m_Mode == pos_rule_mode::index ? "is" : "not"] = std::move(List);
			return Object;
		}

		CPosRule PosRuleFromJson(const base::json &Value)
		{
			CPosRule Rule;
			Rule.m_bGuide = Value["guide"].as_bool(false);
			const base::json &At = Value["at"];
			if(At.is_array() && At.size() >= 2)
			{
				Rule.m_X = static_cast<int>(At.at(0).as_int());
				Rule.m_Y = static_cast<int>(At.at(1).as_int());
			}
			const base::json &Is = Value["is"];
			const base::json &Not = Value["not"];
			const base::json &List = Is.is_array() ? Is : Not;
			if(List.is_array())
			{
				Rule.m_Mode = Is.is_array() ? pos_rule_mode::index : pos_rule_mode::not_index;
				for(std::size_t i = 0; i < List.size(); i++)
					Rule.m_vIndices.push_back(IndexInfoFromJson(List.at(i)));
			}
			return Rule;
		}

		base::json IndexRuleToJson(const CIndexRule &Rule)
		{
			base::json Object = base::json::object();
			Object["tile"] = TileToJson(Rule.m_Tile, Rule.m_Role);
			if(!Rule.m_Name.empty())
				Object["name"] = Rule.m_Name;
			if(Rule.m_Flag != 0)
				Object["flags"] = FlagsToJson(Rule.m_Flag);
			if(!Rule.m_vRules.empty())
			{
				base::json When = base::json::array();
				for(const CPosRule &PosRule : Rule.m_vRules)
					When.push_back(PosRuleToJson(PosRule));
				Object["when"] = std::move(When);
			}
			if(!Rule.m_vModuloRules.empty())
			{
				base::json Modulo = base::json::array();
				for(const CModuloRule &Mod : Rule.m_vModuloRules)
				{
					base::json Entry = base::json::object();
					base::json Every = base::json::array();
					Every.push_back(Mod.m_ModX);
					Every.push_back(Mod.m_ModY);
					Entry["every"] = std::move(Every);
					base::json Offset = base::json::array();
					Offset.push_back(Mod.m_OffsetX);
					Offset.push_back(Mod.m_OffsetY);
					Entry["offset"] = std::move(Offset);
					Modulo.push_back(std::move(Entry));
				}
				Object["modulo"] = std::move(Modulo);
			}
			if(Rule.m_RandomProbability < 1.0f)
				Object["random"] = static_cast<double>(Rule.m_RandomProbability);
			if(!Rule.m_bDefaultRule)
				Object["default"] = false;
			if(Rule.m_bSkipEmpty)
				Object["skip_empty"] = true;
			if(Rule.m_bSkipFull)
				Object["skip_full"] = true;
			return Object;
		}

		CIndexRule IndexRuleFromJson(const base::json &Value)
		{
			CIndexRule Rule;
			if(!Value.is_object())
			{
				// the terse form: a rule is just the tile it writes
				TileFromJson(Value, Rule.m_Tile, Rule.m_Role);
				return Rule;
			}
			TileFromJson(Value["tile"], Rule.m_Tile, Rule.m_Role);
			Rule.m_Name = Value["name"].as_string();
			Rule.m_Flag = FlagsFromJson(Value["flags"]);
			Rule.m_RandomProbability = static_cast<float>(Value["random"].as_double(1.0));
			Rule.m_bDefaultRule = Value["default"].as_bool(true);
			Rule.m_bSkipEmpty = Value["skip_empty"].as_bool(false);
			Rule.m_bSkipFull = Value["skip_full"].as_bool(false);

			const base::json &When = Value["when"];
			if(When.is_array())
				for(std::size_t i = 0; i < When.size(); i++)
					Rule.m_vRules.push_back(PosRuleFromJson(When.at(i)));

			const base::json &Modulo = Value["modulo"];
			if(Modulo.is_array())
				for(std::size_t i = 0; i < Modulo.size(); i++)
				{
					const base::json &Entry = Modulo.at(i);
					CModuloRule Mod;
					const base::json &Every = Entry["every"];
					const base::json &Offset = Entry["offset"];
					if(Every.is_array() && Every.size() >= 2)
					{
						Mod.m_ModX = static_cast<int>(Every.at(0).as_int(1));
						Mod.m_ModY = static_cast<int>(Every.at(1).as_int(1));
					}
					if(Offset.is_array() && Offset.size() >= 2)
					{
						Mod.m_OffsetX = static_cast<int>(Offset.at(0).as_int());
						Mod.m_OffsetY = static_cast<int>(Offset.at(1).as_int());
					}
					Rule.m_vModuloRules.push_back(Mod);
				}

			Rule.m_RandomProbability = std::clamp(Rule.m_RandomProbability, 0.0f, 1.0f);
			return Rule;
		}

		base::json RunToJson(const CRun &Run)
		{
			base::json Object = base::json::object();
			if(!Run.m_Name.empty())
				Object["name"] = Run.m_Name;
			Object["read"] = Run.m_ReadSlot;
			Object["target"] = Run.m_TargetSlot;
			Object["copy"] = Run.m_bCopy;
			if(Run.m_Reference != 0)
				Object["reference"] = Run.m_Reference;
			base::json Rules = base::json::array();
			for(const CIndexRule &Rule : Run.m_vIndexRules)
				Rules.push_back(IndexRuleToJson(Rule));
			Object["rules"] = std::move(Rules);
			return Object;
		}

		CRun RunFromJson(const base::json &Value)
		{
			CRun Run;
			Run.m_Name = Value["name"].as_string();
			Run.m_ReadSlot = static_cast<int>(Value["read"].as_int(6));
			Run.m_TargetSlot = static_cast<int>(Value["target"].as_int(-1));
			Run.m_bCopy = Value["copy"].as_bool(true);
			Run.m_Reference = static_cast<int>(Value["reference"].as_int(0));
			const base::json &Rules = Value["rules"];
			if(Rules.is_array())
				for(std::size_t i = 0; i < Rules.size(); i++)
					Run.m_vIndexRules.push_back(IndexRuleFromJson(Rules.at(i)));
			return Run;
		}

		base::json ConfigToJson(const CConfiguration &Config)
		{
			base::json Object = base::json::object();
			if(!Config.m_Name.empty())
				Object["name"] = Config.m_Name;
			base::json Region = base::json::array();
			Region.push_back(Config.m_StartX);
			Region.push_back(Config.m_StartY);
			Region.push_back(Config.m_EndX > Config.m_StartX ? Config.m_EndX - Config.m_StartX : 0);
			Region.push_back(Config.m_EndY > Config.m_StartY ? Config.m_EndY - Config.m_StartY : 0);
			Object["region"] = std::move(Region);
			base::json Runs = base::json::array();
			for(const CRun &Run : Config.m_vRuns)
				Runs.push_back(RunToJson(Run));
			Object["runs"] = std::move(Runs);
			return Object;
		}

		CConfiguration ConfigFromJson(const base::json &Value)
		{
			CConfiguration Config;
			Config.m_Name = Value["name"].as_string();
			const base::json &Region = Value["region"];
			if(Region.is_array() && Region.size() >= 4)
			{
				Config.m_StartX = static_cast<int>(Region.at(0).as_int());
				Config.m_StartY = static_cast<int>(Region.at(1).as_int());
				const int Width = static_cast<int>(Region.at(2).as_int());
				const int Height = static_cast<int>(Region.at(3).as_int());
				Config.m_EndX = Width > 0 ? Config.m_StartX + Width : Config.m_StartX;
				Config.m_EndY = Height > 0 ? Config.m_StartY + Height : Config.m_StartY;
			}
			const base::json &Runs = Value["runs"];
			if(Runs.is_array())
				for(std::size_t i = 0; i < Runs.size(); i++)
					Config.m_vRuns.push_back(RunFromJson(Runs.at(i)));
			return Config;
		}

		// a fresh configuration and run, so a rule set created in the editor and
		// one loaded from JSON start from the same shape
		CConfiguration DefaultConfig(std::string_view Name)
		{
			CConfiguration Config;
			Config.m_Name = std::string(Name);
			CRun Run;
			Run.m_Name = "run 1";
			Config.m_vRuns.push_back(std::move(Run));
			return Config;
		}

		// ----------------------------------------------------- DDNet text reader
		// "XFLIP", "YFLIP", "ROTATE", "NONE", or a role name: the reference only
		// knows the three flag words
		bool MatchFlagWord(std::string_view Word, int &Flags, bool &bTestFlags)
		{
			if(Word == "NONE")
			{
				Flags = 0;
				bTestFlags = true;
				return true;
			}
			const int Bit = FlagBitFromName(base::ToLower(std::string(Word)));
			if(Bit == 0)
				return false;
			Flags |= Bit;
			return true;
		}
	} // namespace

	// --------------------------------------------------------------- public JSON
	base::json ToJson(const CRuleSet &Rules)
	{
		base::json Document = base::json::object();
		Document["autorules"] = Rules.m_Version;
		Document["name"] = Rules.m_Name;
		if(!Rules.m_Description.empty())
			Document["description"] = Rules.m_Description;
		if(!Rules.m_Tileset.empty())
			Document["tileset"] = Rules.m_Tileset;
		base::json Configs = base::json::array();
		for(const CConfiguration &Config : Rules.m_vConfigs)
			Configs.push_back(ConfigToJson(Config));
		Document["configs"] = std::move(Configs);
		return Document;
	}

	CRuleSet FromJson(const base::json &Document, std::string *pError)
	{
		CRuleSet Rules;
		const auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = Message;
			return CRuleSet{};
		};

		if(!Document.is_object())
			return Fail("a rule set has to be a JSON object");
		if(Document["autorules"].as_int(0) <= 0)
			return Fail("this document is not an autorules set (no \"autorules\" key)");

		Rules.m_Version = static_cast<int>(Document["autorules"].as_int(1));
		Rules.m_Name = Document["name"].as_string();
		Rules.m_Description = Document["description"].as_string();
		Rules.m_Tileset = Document["tileset"].as_string();
		const base::json &Configs = Document["configs"];
		if(!Configs.is_array())
			return Fail("a rule set needs a \"configs\" array");
		for(std::size_t i = 0; i < Configs.size(); i++)
			Rules.m_vConfigs.push_back(ConfigFromJson(Configs.at(i)));
		return Rules;
	}

	bool SaveToFile(const CRuleSet &Rules, const storage::path &FileName, std::string *pError)
	{
		if(!storage::write_text(FileName, ToJson(Rules).dump(1)))
		{
			if(pError != nullptr)
				*pError = "failed to write '" + FileName.string() + "'";
			return false;
		}
		return true;
	}

	bool LoadFromFile(CRuleSet &Rules, const storage::path &FileName, std::string *pError)
	{
		const std::optional<std::string> Text = storage::read_text(FileName);
		if(!Text.has_value())
		{
			if(pError != nullptr)
				*pError = "cannot read '" + FileName.string() + "'";
			return false;
		}

		// a `.rules` file from the DDRace data directory is the line format;
		// anything else is JSON
		if(base::EndsWith(FileName.filename().string(), ".rules"))
			return LoadFromDdnetText(Rules, *Text, pError);

		std::string Error;
		const base::json Document = base::json::parse(*Text, &Error);
		if(!Error.empty())
		{
			if(pError != nullptr)
				*pError = Error;
			return false;
		}
		Rules = FromJson(Document, pError);
		return pError == nullptr || pError->empty();
	}

	// ------------------------------------------------------- DDNet text reader
	bool LoadFromDdnetText(CRuleSet &Rules, std::string_view Text, std::string *pError)
	{
		Rules = CRuleSet{};
		if(pError != nullptr)
			pError->clear();

		CConfiguration *pConfig = nullptr;
		CRun *pRun = nullptr;
		CIndexRule *pIndex = nullptr;
		std::size_t LineStart = 0;
		int LineNumber = 0;
		auto Fail = [&](const std::string &Message) {
			if(pError != nullptr)
				*pError = base::Format("line %d: %s", LineNumber, Message.c_str());
			return false;
		};

		while(LineStart <= Text.size())
		{
			const std::size_t LineEnd = Text.find('\n', LineStart);
			std::string Line(Text.substr(LineStart, LineEnd == std::string_view::npos ? std::string_view::npos : LineEnd - LineStart));
			LineStart = LineEnd == std::string_view::npos ? Text.size() + 1 : LineEnd + 1;
			LineNumber++;

			// the reference skips blank lines, comments and leading whitespace
			const std::string Trimmed = base::Trim(Line);
			if(Trimmed.empty() || Trimmed[0] == '#')
				continue;
			if(Trimmed[0] == '[')
			{
				Rules.m_vConfigs.push_back(DefaultConfig(Trimmed.substr(1)));
				pConfig = &Rules.m_vConfigs.back();
				pRun = &pConfig->m_vRuns.back();
				pIndex = nullptr;
				continue;
			}

			const std::vector<std::string> Words = [&] {
				std::vector<std::string> Out;
				std::string Current;
				for(const char Character : Trimmed)
				{
					if(Character == ' ' || Character == '\t')
					{
						if(!Current.empty())
							Out.push_back(Current);
						Current.clear();
					}
					else
					{
						Current.push_back(Character);
					}
				}
				if(!Current.empty())
					Out.push_back(Current);
				return Out;
			}();
			if(Words.empty())
				continue;

			if(Words[0] == "NewRun" && pConfig != nullptr)
			{
				CRun Run;
				Run.m_Name = base::Format("run %zu", pConfig->m_vRuns.size() + 1);
				pConfig->m_vRuns.push_back(std::move(Run));
				pRun = &pConfig->m_vRuns.back();
				pIndex = nullptr;
				continue;
			}
			if(Words[0] == "Index" && pRun != nullptr)
			{
				CIndexRule Rule;
				if(Words.size() >= 2)
					Rule.m_Tile = std::atoi(Words[1].c_str());
				for(std::size_t i = 2; i < Words.size(); i++)
				{
					int Flags = 0;
					bool bTestFlags = false;
					if(MatchFlagWord(Words[i], Flags, bTestFlags))
						Rule.m_Flag |= Flags;
				}
				pRun->m_vIndexRules.push_back(std::move(Rule));
				pIndex = &pRun->m_vIndexRules.back();
				continue;
			}
			if(Words[0] == "Pos" && pIndex != nullptr)
			{
				CPosRule Rule;
				if(Words.size() >= 3)
				{
					const std::size_t Comma = Words[1].find(',');
					if(Comma != std::string::npos)
					{
						Rule.m_X = std::atoi(Words[1].substr(0, Comma).c_str());
						Rule.m_Y = std::atoi(Words[1].substr(Comma + 1).c_str());
					}
					Rule.m_Mode = Words[2] == "NOTINDEX" ? pos_rule_mode::not_index : pos_rule_mode::index;
					// the rest is a list of indices, each with optional flags:
					// "1", "1|XFLIP", "1|XFLIP|ROTATE"
					for(std::size_t i = 3; i < Words.size(); i++)
					{
						CIndexInfo Info;
						std::size_t Start = 0;
						bool bFirst = true;
						while(Start <= Words[i].size())
						{
							const std::size_t Bar = Words[i].find('|', Start);
							const std::string Part = Words[i].substr(Start, Bar == std::string::npos ? std::string::npos : Bar - Start);
							if(bFirst)
							{
								Info.m_Tile = std::atoi(Part.c_str());
								bFirst = false;
							}
							else
							{
								int Flags = 0;
								bool bTestFlags = false;
								if(MatchFlagWord(Part, Flags, bTestFlags))
								{
									Info.m_Flag |= Flags;
									Info.m_bTestFlag = Info.m_bTestFlag || bTestFlags;
								}
							}
							if(Bar == std::string::npos)
								break;
							Start = Bar + 1;
						}
						Rule.m_vIndices.push_back(Info);
					}
					// a "1" leading the list is the DDRace way of writing that a
					// test must pass, so a bare index sets the test flag
					for(CIndexInfo &Info : Rule.m_vIndices)
						if(Info.m_Flag != 0)
							Info.m_bTestFlag = true;
				}
				pIndex->m_vRules.push_back(std::move(Rule));
				continue;
			}
			if(Words[0] == "Modulo" && pIndex != nullptr)
			{
				CModuloRule Mod;
				if(Words.size() >= 3)
				{
					const std::size_t Comma = Words[1].find(',');
					if(Comma != std::string::npos)
					{
						Mod.m_ModX = std::max(std::atoi(Words[1].substr(0, Comma).c_str()), 1);
						Mod.m_ModY = std::max(std::atoi(Words[1].substr(Comma + 1).c_str()), 1);
					}
					const std::size_t Comma2 = Words[2].find(',');
					if(Comma2 != std::string::npos)
					{
						Mod.m_OffsetX = std::atoi(Words[2].substr(0, Comma2).c_str());
						Mod.m_OffsetY = std::atoi(Words[2].substr(Comma2 + 1).c_str());
					}
				}
				pIndex->m_vModuloRules.push_back(Mod);
				continue;
			}
			if(pIndex != nullptr)
			{
				if(Words[0] == "Random")
					pIndex->m_RandomProbability = static_cast<float>(std::atof(Words.size() >= 2 ? Words[1].c_str() : "1"));
				else if(Words[0] == "DefaultRule")
					pIndex->m_bDefaultRule = Words.size() < 2 || std::atoi(Words[1].c_str()) != 0;
				else if(Words[0] == "SkipEmpty")
					pIndex->m_bSkipEmpty = Words.size() < 2 || std::atoi(Words[1].c_str()) != 0;
				else if(Words[0] == "SkipFull")
					pIndex->m_bSkipFull = Words.size() < 2 || std::atoi(Words[1].c_str()) != 0;
			}
		}

		if(Rules.m_vConfigs.empty())
			return Fail("no configurations found");
		Rules.m_Name = "DDNet rules";
		return true;
	}
} // namespace game::rules
