#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

/*
	Settings, in the shape TWICE's config module uses: a typed key with a default
	value, a string map behind it, and a plain text file on disk. The file lives
	in the application data directory (libsugarx::get_data_home_directory()):

		~/.local/share/uniteed/config.cfg

	one "key value" pair per line, "//" starts a comment.
*/

#include <charconv>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

#include "base/storage.h"

namespace config
{
	template<typename T>
	struct key
	{
		std::string_view m_Name;
		T m_Default;
	};

	// the value types the file format understands
	template<typename T>
	std::string ToText(const T &Value)
	{
		if constexpr(std::is_same_v<T, bool>)
			return Value ? "1" : "0";
		else if constexpr(std::is_same_v<T, std::string>)
			return Value;
		else
			return std::to_string(Value);
	}

	template<typename T>
	T FromText(const std::string &Text, const T &Default)
	{
		if constexpr(std::is_same_v<T, bool>)
			return Text == "1" || Text == "true" || Text == "yes";
		else if constexpr(std::is_same_v<T, std::string>)
			return Text;
		else if constexpr(std::is_same_v<T, float>)
		{
			char *pEnd = nullptr;
			const float Value = std::strtof(Text.c_str(), &pEnd);
			return pEnd != Text.c_str() ? Value : Default;
		}
		else if constexpr(std::is_integral_v<T>)
		{
			T Value = Default;
			const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			return Result.ec == std::errc() ? Value : Default;
		}
		else
			return Default;
	}

	class CConfig
	{
	public:
		// the config file of the current user, may not exist yet
		static storage::path DefaultPath();

		// missing entries keep their default, a broken file only warns
		void Load(const storage::path &FileName);
		bool Save(const storage::path &FileName) const;

		template<typename T>
		T Get(const key<T> &Key) const
		{
			const auto It = m_Values.find(std::string(Key.m_Name));
			if(It == m_Values.end())
				return Key.m_Default;
			return FromText<T>(It->second, Key.m_Default);
		}

		template<typename T>
		void Set(const key<T> &Key, const T &Value) { m_Values[std::string(Key.m_Name)] = ToText(Value); }

		bool Has(std::string_view Name) const { return m_Values.find(std::string(Name)) != m_Values.end(); }
		void SetRaw(std::string_view Name, const std::string &Value) { m_Values[std::string(Name)] = Value; }
		std::string GetRaw(std::string_view Name, const std::string &Default = {}) const;
		void Clear() { m_Values.clear(); }

	private:
		std::unordered_map<std::string, std::string> m_Values;
	};
} // namespace config

#endif // CONFIG_CONFIG_H
