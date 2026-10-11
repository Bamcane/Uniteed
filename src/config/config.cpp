#include "config.h"

#include <sstream>
#include <string>
#include <vector>

#include "base/logger.h"
#include "base/string.h"

namespace config
{
	storage::path CConfig::DefaultPath()
	{
		return storage::GetAppDataDirectory() / "config.cfg";
	}

	void CConfig::Load(const storage::path &FileName)
	{
		const std::optional<std::string> Text = storage::read_text(FileName);
		if(!Text.has_value())
		{
			LOG_INFO("config", "no config at '{}', using the defaults", FileName.string().c_str());
			return;
		}

		int Loaded = 0;
		std::istringstream Stream(*Text);
		std::string Line;
		while(std::getline(Stream, Line))
		{
			const std::string Trimmed = base::Trim(Line);
			if(Trimmed.empty() || base::StartsWith(Trimmed, "//"))
				continue;
			std::size_t Pos = Trimmed.find_first_of(" \t");
			if(Pos == std::string::npos)
				continue;
			const std::string Name = Trimmed.substr(0, Pos);
			const std::string Value = base::Trim(Trimmed.substr(Pos + 1));
			m_Values[Name] = Value;
			Loaded++;
		}
		LOG_INFO("config", "loaded {:d} settings from '{}'", Loaded, FileName.string().c_str());
	}

	bool CConfig::Save(const storage::path &FileName) const
	{
		std::string Text = "// Uniteed settings\n";
		for(const auto &[Name, Value] : m_Values)
			Text += Name + " " + Value + "\n";
		if(!storage::write_text(FileName, Text))
		{
			LOG_WARN("config", "could not write '{}'", FileName.string().c_str());
			return false;
		}
		return true;
	}

	std::string CConfig::GetRaw(std::string_view Name, const std::string &Default) const
	{
		const auto It = m_Values.find(std::string(Name));
		return It == m_Values.end() ? Default : It->second;
	}
} // namespace config
