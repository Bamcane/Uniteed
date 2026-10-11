#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <system_error>
#include <utility>

namespace base
{
	namespace
	{
		// The parser recurses one level per container, so the limit has to be
		// low enough that a hostile document cannot exhaust the C stack.
		constexpr int JSON_MAX_DEPTH = 200;

		/*
			A shared null value for every read that misses: a missing object
			member, an out of range index, a path that does not resolve. The
			editor is single threaded, so the single instance is enough; writes
			to it are simply dropped.
		*/
		json &NullValue()
		{
			static json s_Null;
			return s_Null;
		}

		// true for an all digits segment that fits into a std::size_t, which is
		// what a path treats as an array index
		bool ParseIndex(std::string_view Segment, std::size_t &Index)
		{
			if(Segment.empty())
				return false;
			std::size_t Value = 0;
			for(const char c : Segment)
			{
				if(c < '0' || c > '9')
					return false;
				const std::size_t Digit = static_cast<std::size_t>(c - '0');
				if(Value > (std::numeric_limits<std::size_t>::max() - Digit) / 10)
					return false;
				Value = Value * 10 + Digit;
			}
			Index = Value;
			return true;
		}

		// ------------------------------------------------------- serializing
		void AppendIndent(std::string &Out, int Indent, int Depth)
		{
			Out.append(static_cast<std::size_t>(Indent) * static_cast<std::size_t>(Depth), ' ');
		}

		void AppendString(std::string &Out, std::string_view Text)
		{
			static const char aHex[] = "0123456789abcdef";
			Out += '"';
			for(const char RawChar : Text)
			{
				const unsigned char c = static_cast<unsigned char>(RawChar);
				switch(c)
				{
				case '"': Out += "\\\""; break;
				case '\\': Out += "\\\\"; break;
				case '\b': Out += "\\b"; break;
				case '\f': Out += "\\f"; break;
				case '\n': Out += "\\n"; break;
				case '\r': Out += "\\r"; break;
				case '\t': Out += "\\t"; break;
				default:
					if(c < 0x20)
					{
						Out += "\\u00";
						Out.push_back(aHex[(c >> 4) & 0xF]);
						Out.push_back(aHex[c & 0xF]);
					}
					else
					{
						Out.push_back(RawChar);
					}
					break;
				}
			}
			Out += '"';
		}

		// ---------------------------------------------------------- parsing
		struct json_parser
		{
			std::string_view m_Text;
			std::size_t m_Pos = 0;
			std::string *m_pError = nullptr;

			bool Fail(std::string_view Message, std::size_t Offset)
			{
				if(m_pError)
					*m_pError = "json: " + std::string(Message) + " at offset " + std::to_string(Offset);
				return false;
			}

			void SkipWhitespace()
			{
				while(m_Pos < m_Text.size())
				{
					const char c = m_Text[m_Pos];
					if(c != ' ' && c != '\t' && c != '\n' && c != '\r')
						break;
					m_Pos++;
				}
			}

			bool ParseValue(json &Out, int Depth)
			{
				if(Depth > JSON_MAX_DEPTH)
					return Fail("nesting depth exceeds " + std::to_string(JSON_MAX_DEPTH), m_Pos);

				SkipWhitespace();
				if(m_Pos >= m_Text.size())
					return Fail("unexpected end of input", m_Pos);

				const char c = m_Text[m_Pos];
				switch(c)
				{
				case '{': return ParseObject(Out, Depth);
				case '[': return ParseArray(Out, Depth);
				case '"':
				{
					std::string Text;
					if(!ParseString(Text))
						return false;
					Out = json(std::move(Text));
					return true;
				}
				case 't': return ParseLiteral("true", json(true), Out);
				case 'f': return ParseLiteral("false", json(false), Out);
				case 'n': return ParseLiteral("null", json(), Out);
				default: break;
				}

				if(c == '-' || (c >= '0' && c <= '9'))
					return ParseNumber(Out);

				std::string Message = "unexpected character '";
				Message += c;
				Message += '\'';
				return Fail(Message, m_Pos);
			}

			bool ParseLiteral(std::string_view Literal, json Value, json &Out)
			{
				if(m_Text.compare(m_Pos, Literal.size(), Literal) != 0)
					return Fail("invalid literal, expected '" + std::string(Literal) + "'", m_Pos);
				m_Pos += Literal.size();
				Out = std::move(Value);
				return true;
			}

			bool ParseObject(json &Out, int Depth)
			{
				m_Pos++; // '{'
				Out = json::object();
				SkipWhitespace();
				if(m_Pos < m_Text.size() && m_Text[m_Pos] == '}')
				{
					m_Pos++;
					return true;
				}

				while(true)
				{
					SkipWhitespace();
					if(m_Pos >= m_Text.size())
						return Fail("unexpected end of input", m_Pos);
					if(m_Text[m_Pos] != '"')
						return Fail("expected an object key", m_Pos);

					std::string Key;
					if(!ParseString(Key))
						return false;

					SkipWhitespace();
					if(m_Pos >= m_Text.size())
						return Fail("unexpected end of input", m_Pos);
					if(m_Text[m_Pos] != ':')
						return Fail("expected ':' after the object key", m_Pos);
					m_Pos++;

					json Value;
					if(!ParseValue(Value, Depth + 1))
						return false;
					// a duplicate key keeps its first position but takes the
					// last value, which is the common behavior
					Out[Key] = std::move(Value);

					SkipWhitespace();
					if(m_Pos >= m_Text.size())
						return Fail("unexpected end of input", m_Pos);
					if(m_Text[m_Pos] == ',')
					{
						m_Pos++;
						continue;
					}
					if(m_Text[m_Pos] == '}')
					{
						m_Pos++;
						return true;
					}
					return Fail("expected ',' or '}' in the object", m_Pos);
				}
			}

			bool ParseArray(json &Out, int Depth)
			{
				m_Pos++; // '['
				Out = json::array();
				SkipWhitespace();
				if(m_Pos < m_Text.size() && m_Text[m_Pos] == ']')
				{
					m_Pos++;
					return true;
				}

				while(true)
				{
					json Value;
					if(!ParseValue(Value, Depth + 1))
						return false;
					Out.push_back(std::move(Value));

					SkipWhitespace();
					if(m_Pos >= m_Text.size())
						return Fail("unexpected end of input", m_Pos);
					if(m_Text[m_Pos] == ',')
					{
						m_Pos++;
						continue;
					}
					if(m_Text[m_Pos] == ']')
					{
						m_Pos++;
						return true;
					}
					return Fail("expected ',' or ']' in the array", m_Pos);
				}
			}

			bool ParseString(std::string &Out)
			{
				m_Pos++; // '"'
				Out.clear();
				while(true)
				{
					if(m_Pos >= m_Text.size())
						return Fail("unterminated string", m_Pos);

					const unsigned char c = static_cast<unsigned char>(m_Text[m_Pos]);
					if(c == '"')
					{
						m_Pos++;
						return true;
					}
					if(c == '\\')
					{
						m_Pos++;
						if(!ParseEscape(Out))
							return false;
						continue;
					}
					// JSON strings may not contain a raw control character
					if(c < 0x20)
						return Fail("raw control character in string", m_Pos);
					Out.push_back(static_cast<char>(c));
					m_Pos++;
				}
			}

			bool ParseEscape(std::string &Out)
			{
				if(m_Pos >= m_Text.size())
					return Fail("unexpected end of input in the escape", m_Pos);

				const char c = m_Text[m_Pos++];
				switch(c)
				{
				case '"': Out.push_back('"'); return true;
				case '\\': Out.push_back('\\'); return true;
				case '/': Out.push_back('/'); return true;
				case 'b': Out.push_back('\b'); return true;
				case 'f': Out.push_back('\f'); return true;
				case 'n': Out.push_back('\n'); return true;
				case 'r': Out.push_back('\r'); return true;
				case 't': Out.push_back('\t'); return true;
				case 'u': return ParseUnicodeEscape(Out);
				default:
				{
					std::string Message = "invalid escape '\\";
					Message += c;
					Message += '\'';
					return Fail(Message, m_Pos - 1);
				}
				}
			}

			bool ParseHex4(std::uint32_t &Out)
			{
				if(m_Pos + 4 > m_Text.size())
					return Fail("truncated \\u escape", m_Pos);

				std::uint32_t Value = 0;
				for(int i = 0; i < 4; i++)
				{
					const char c = m_Text[m_Pos + i];
					Value <<= 4;
					if(c >= '0' && c <= '9')
						Value |= static_cast<std::uint32_t>(c - '0');
					else if(c >= 'a' && c <= 'f')
						Value |= static_cast<std::uint32_t>(c - 'a' + 10);
					else if(c >= 'A' && c <= 'F')
						Value |= static_cast<std::uint32_t>(c - 'A' + 10);
					else
						return Fail("invalid \\u escape", m_Pos + i);
				}
				m_Pos += 4;
				Out = Value;
				return true;
			}

			bool ParseUnicodeEscape(std::string &Out)
			{
				std::uint32_t Code = 0;
				if(!ParseHex4(Code))
					return false;

				// a high surrogate must be followed by a low surrogate to form
				// one code point; anything else becomes the replacement
				// character rather than invalid UTF-8
				if(Code >= 0xD800 && Code <= 0xDBFF)
				{
					if(m_Pos + 1 < m_Text.size() && m_Text[m_Pos] == '\\' && m_Text[m_Pos + 1] == 'u')
					{
						const std::size_t SavedPos = m_Pos;
						m_Pos += 2;
						std::uint32_t Low = 0;
						if(!ParseHex4(Low))
							return false;
						if(Low >= 0xDC00 && Low <= 0xDFFF)
						{
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
							AppendUtf8(Out, Code);
							return true;
						}
						// not a pair after all: parse the second escape again
						// as a standalone one
						m_Pos = SavedPos;
					}
					AppendUtf8(Out, 0xFFFD);
					return true;
				}
				if(Code >= 0xDC00 && Code <= 0xDFFF)
				{
					AppendUtf8(Out, 0xFFFD);
					return true;
				}

				AppendUtf8(Out, Code);
				return true;
			}

			static void AppendUtf8(std::string &Out, std::uint32_t Code)
			{
				if(Code < 0x80)
				{
					Out.push_back(static_cast<char>(Code));
				}
				else if(Code < 0x800)
				{
					Out.push_back(static_cast<char>(0xC0 | (Code >> 6)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
				else if(Code < 0x10000)
				{
					Out.push_back(static_cast<char>(0xE0 | (Code >> 12)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
				else
				{
					Out.push_back(static_cast<char>(0xF0 | (Code >> 18)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 12) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
					Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
				}
			}

			bool ParseNumber(json &Out)
			{
				const std::size_t Start = m_Pos;
				bool bIsFloat = false;

				if(m_Text[m_Pos] == '-')
					m_Pos++;
				if(m_Pos >= m_Text.size())
					return Fail("unexpected end of input in the number", m_Pos);

				// the integer part, no leading zero unless it stands alone
				if(m_Text[m_Pos] == '0')
				{
					m_Pos++;
					if(m_Pos < m_Text.size() && m_Text[m_Pos] >= '0' && m_Text[m_Pos] <= '9')
						return Fail("invalid number", Start);
				}
				else if(m_Text[m_Pos] >= '1' && m_Text[m_Pos] <= '9')
				{
					while(m_Pos < m_Text.size() && m_Text[m_Pos] >= '0' && m_Text[m_Pos] <= '9')
						m_Pos++;
				}
				else
				{
					return Fail("invalid number", Start);
				}

				if(m_Pos < m_Text.size() && m_Text[m_Pos] == '.')
				{
					bIsFloat = true;
					m_Pos++;
					const std::size_t Digits = m_Pos;
					while(m_Pos < m_Text.size() && m_Text[m_Pos] >= '0' && m_Text[m_Pos] <= '9')
						m_Pos++;
					if(m_Pos == Digits)
						return Fail("invalid number, expected a fraction", Start);
				}

				if(m_Pos < m_Text.size() && (m_Text[m_Pos] == 'e' || m_Text[m_Pos] == 'E'))
				{
					bIsFloat = true;
					m_Pos++;
					if(m_Pos < m_Text.size() && (m_Text[m_Pos] == '+' || m_Text[m_Pos] == '-'))
						m_Pos++;
					const std::size_t Digits = m_Pos;
					while(m_Pos < m_Text.size() && m_Text[m_Pos] >= '0' && m_Text[m_Pos] <= '9')
						m_Pos++;
					if(m_Pos == Digits)
						return Fail("invalid number, expected an exponent", Start);
				}

				const std::string_view Text = m_Text.substr(Start, m_Pos - Start);
				if(!bIsFloat)
				{
					long long Integer = 0;
					const std::from_chars_result Result = std::from_chars(Text.data(), Text.data() + Text.size(), Integer);
					if(Result.ec == std::errc() && Result.ptr == Text.data() + Text.size())
					{
						Out = json(Integer);
						return true;
					}
					// too large for a long long: keep it as a double below
				}

				double Number = 0.0;
				const std::from_chars_result Result = std::from_chars(Text.data(), Text.data() + Text.size(), Number);
				if(Result.ec != std::errc() || Result.ptr != Text.data() + Text.size())
					return Fail("number out of range", Start);
				Out = json(Number);
				return true;
			}
		};
	} // namespace

	// ------------------------------------------------------- json_object
	const json *json_object::Find(std::string_view Key) const
	{
		const auto It = m_Index.find(Key);
		if(It == m_Index.end())
			return nullptr;
		return &m_vValues[It->second];
	}

	json &json_object::Insert(std::string_view Key)
	{
		const auto It = m_Index.find(Key);
		if(It != m_Index.end())
			return m_vValues[It->second];

		const std::size_t Index = m_vKeys.size();
		m_vKeys.emplace_back(Key);
		m_vValues.emplace_back();
		m_Index.emplace(m_vKeys.back(), Index);
		return m_vValues[Index];
	}

	bool json_object::Erase(std::string_view Key)
	{
		const auto It = m_Index.find(Key);
		if(It == m_Index.end())
			return false;

		const std::size_t Index = It->second;
		m_vKeys.erase(m_vKeys.begin() + static_cast<std::ptrdiff_t>(Index));
		m_vValues.erase(m_vValues.begin() + static_cast<std::ptrdiff_t>(Index));
		m_Index.erase(It);
		// every member behind the erased one moved down by a slot
		for(std::size_t i = Index; i < m_vKeys.size(); i++)
			m_Index.insert_or_assign(m_vKeys[i], i);
		return true;
	}

	// -------------------------------------------------------- construction
	json::json() :
		m_Value(nullptr) {}

	json::json(std::nullptr_t) :
		m_Value(nullptr) {}

	json::json(bool Value) :
		m_Value(Value) {}

	json::json(int Value) :
		m_Value(static_cast<long long>(Value)) {}

	json::json(long long Value) :
		m_Value(Value) {}

	json::json(double Value) :
		m_Value(Value) {}

	json::json(const char *pValue) :
		m_Value(pValue ? std::string(pValue) : std::string()) {}

	json::json(std::string Value) :
		m_Value(std::move(Value)) {}

	json json::object()
	{
		json Result;
		Result.m_Value = json_object();
		return Result;
	}

	json json::array()
	{
		json Result;
		Result.m_Value = array_type();
		return Result;
	}

	// -------------------------------------------------------------- parsing
	bool json::ParseInto(std::string_view Text, json &Out, std::string *pError)
	{
		if(pError)
			pError->clear();

		json_parser Parser;
		Parser.m_Text = Text;
		Parser.m_pError = pError;

		json Result;
		if(!Parser.ParseValue(Result, 0))
			return false;

		Parser.SkipWhitespace();
		if(Parser.m_Pos != Text.size())
			return Parser.Fail("trailing garbage", Parser.m_Pos);

		Out = std::move(Result);
		return true;
	}

	json json::parse(std::string_view Text, std::string *pError)
	{
		json Result;
		ParseInto(Text, Result, pError);
		return Result;
	}

	json json::parse_or(std::string_view Text, const json &Fallback, std::string *pError)
	{
		json Result;
		if(!ParseInto(Text, Result, pError))
			return Fallback;
		return Result;
	}

	// ----------------------------------------------------------- inspection
	json::json_type json::type() const
	{
		switch(m_Value.index())
		{
		case 0: return json_type::null;
		case 1: return json_type::boolean;
		case 2:
		case 3: return json_type::number;
		case 4: return json_type::string;
		case 5: return json_type::array;
		default: return json_type::object;
		}
	}

	bool json::is_null() const { return std::holds_alternative<std::nullptr_t>(m_Value); }
	bool json::is_bool() const { return std::holds_alternative<bool>(m_Value); }
	bool json::is_number() const { return std::holds_alternative<long long>(m_Value) || std::holds_alternative<double>(m_Value); }
	bool json::is_string() const { return std::holds_alternative<std::string>(m_Value); }
	bool json::is_array() const { return std::holds_alternative<array_type>(m_Value); }
	bool json::is_object() const { return std::holds_alternative<json_object>(m_Value); }

	const char *json::type_name() const
	{
		switch(type())
		{
		case json_type::null: return "null";
		case json_type::boolean: return "boolean";
		case json_type::number: return "number";
		case json_type::string: return "string";
		case json_type::array: return "array";
		case json_type::object: return "object";
		}
		return "null";
	}

	// ------------------------------------------------------- typed reads
	bool json::as_bool(bool Default) const
	{
		if(const bool *pValue = std::get_if<bool>(&m_Value))
			return *pValue;
		return Default;
	}

	long long json::as_int(long long Default) const
	{
		if(const long long *pValue = std::get_if<long long>(&m_Value))
			return *pValue;
		if(const double *pValue = std::get_if<double>(&m_Value))
			return static_cast<long long>(*pValue);
		return Default;
	}

	double json::as_double(double Default) const
	{
		if(const double *pValue = std::get_if<double>(&m_Value))
			return *pValue;
		if(const long long *pValue = std::get_if<long long>(&m_Value))
			return static_cast<double>(*pValue);
		return Default;
	}

	std::string json::as_string(std::string Default) const
	{
		if(const std::string *pValue = std::get_if<std::string>(&m_Value))
			return *pValue;
		return Default;
	}

	// ------------------------------------------------------ serializing
	void json::DumpTo(std::string &Out, int Indent, int Depth) const
	{
		switch(type())
		{
		case json_type::null:
			Out += "null";
			return;
		case json_type::boolean:
			Out += std::get<bool>(m_Value) ? "true" : "false";
			return;
		case json_type::string:
			AppendString(Out, std::get<std::string>(m_Value));
			return;
		case json_type::number:
		{
			if(const long long *pInteger = std::get_if<long long>(&m_Value))
			{
				char aBuffer[32];
				const std::to_chars_result Result = std::to_chars(aBuffer, aBuffer + sizeof(aBuffer), *pInteger);
				Out.append(aBuffer, Result.ptr);
				return;
			}

			const double Value = std::get<double>(m_Value);
			// JSON has no infinity or NaN, so those degrade to null
			if(!std::isfinite(Value))
			{
				Out += "null";
				return;
			}

			char aBuffer[64];
			const std::to_chars_result Result = std::to_chars(aBuffer, aBuffer + sizeof(aBuffer), Value);
			if(Result.ec != std::errc())
			{
				Out += "null";
				return;
			}
			const std::string_view Text(aBuffer, static_cast<std::size_t>(Result.ptr - aBuffer));
			// to_chars already gives the shortest text that round trips; a
			// form without a fraction or an exponent would read back as an
			// integer, so mark it as a double
			Out += Text;
			if(Text.find_first_of(".eE") == std::string_view::npos)
				Out += ".0";
			return;
		}
		case json_type::array:
		{
			const array_type &Elements = std::get<array_type>(m_Value);
			if(Elements.empty())
			{
				Out += "[]";
				return;
			}
			Out += '[';
			for(std::size_t i = 0; i < Elements.size(); i++)
			{
				if(i != 0)
					Out += ',';
				if(Indent >= 0)
				{
					Out += '\n';
					AppendIndent(Out, Indent, Depth + 1);
				}
				Elements[i].DumpTo(Out, Indent, Depth + 1);
			}
			if(Indent >= 0)
			{
				Out += '\n';
				AppendIndent(Out, Indent, Depth);
			}
			Out += ']';
			return;
		}
		case json_type::object:
		{
			const json_object &Object = std::get<json_object>(m_Value);
			if(Object.m_vKeys.empty())
			{
				Out += "{}";
				return;
			}
			Out += '{';
			for(std::size_t i = 0; i < Object.m_vKeys.size(); i++)
			{
				if(i != 0)
					Out += ',';
				if(Indent >= 0)
				{
					Out += '\n';
					AppendIndent(Out, Indent, Depth + 1);
				}
				AppendString(Out, Object.m_vKeys[i]);
				Out += Indent >= 0 ? ": " : ":";
				Object.m_vValues[i].DumpTo(Out, Indent, Depth + 1);
			}
			if(Indent >= 0)
			{
				Out += '\n';
				AppendIndent(Out, Indent, Depth);
			}
			Out += '}';
			return;
		}
		}
	}

	std::string json::dump(int Indent) const
	{
		std::string Out;
		DumpTo(Out, Indent, 0);
		return Out;
	}

	// ------------------------------------------------------- object access
	const json &json::operator[](std::string_view Key) const
	{
		if(const json_object *pObject = std::get_if<json_object>(&m_Value))
		{
			if(const json *pFound = pObject->Find(Key))
				return *pFound;
		}
		return NullValue();
	}

	json &json::operator[](std::string_view Key)
	{
		if(json_object *pObject = std::get_if<json_object>(&m_Value))
			return pObject->Insert(Key);
		// a null value turns into an object on the first write; any other
		// scalar is left alone and the write goes to the shared null
		if(is_null())
		{
			m_Value = json_object();
			return std::get<json_object>(m_Value).Insert(Key);
		}
		return NullValue();
	}

	bool json::has(std::string_view Key) const
	{
		const json_object *pObject = std::get_if<json_object>(&m_Value);
		return pObject && pObject->Find(Key) != nullptr;
	}

	bool json::erase(std::string_view Key)
	{
		json_object *pObject = std::get_if<json_object>(&m_Value);
		return pObject && pObject->Erase(Key);
	}

	std::vector<std::string> json::keys() const
	{
		if(const json_object *pObject = std::get_if<json_object>(&m_Value))
			return pObject->m_vKeys;
		return {};
	}

	std::size_t json::size() const
	{
		if(const array_type *pArray = std::get_if<array_type>(&m_Value))
			return pArray->size();
		if(const json_object *pObject = std::get_if<json_object>(&m_Value))
			return pObject->size();
		return 0;
	}

	// -------------------------------------------------------- array access
	const json &json::at(std::size_t Index) const
	{
		if(const array_type *pArray = std::get_if<array_type>(&m_Value))
		{
			if(Index < pArray->size())
				return (*pArray)[Index];
		}
		return NullValue();
	}

	json &json::at(std::size_t Index)
	{
		if(array_type *pArray = std::get_if<array_type>(&m_Value))
		{
			if(Index < pArray->size())
				return (*pArray)[Index];
		}
		return NullValue();
	}

	void json::push_back(json Value)
	{
		if(is_null())
			m_Value = array_type();
		if(array_type *pArray = std::get_if<array_type>(&m_Value))
			pArray->push_back(std::move(Value));
	}

	void json::clear()
	{
		m_Value = nullptr;
	}

	// ------------------------------------------------------------ iteration
	std::vector<json>::iterator json::begin()
	{
		if(array_type *pArray = std::get_if<array_type>(&m_Value))
			return pArray->begin();
		static array_type s_Empty;
		return s_Empty.begin();
	}

	std::vector<json>::iterator json::end()
	{
		if(array_type *pArray = std::get_if<array_type>(&m_Value))
			return pArray->end();
		static array_type s_Empty;
		return s_Empty.end();
	}

	std::vector<json>::const_iterator json::begin() const
	{
		if(const array_type *pArray = std::get_if<array_type>(&m_Value))
			return pArray->begin();
		static const array_type s_Empty;
		return s_Empty.begin();
	}

	std::vector<json>::const_iterator json::end() const
	{
		if(const array_type *pArray = std::get_if<array_type>(&m_Value))
			return pArray->end();
		static const array_type s_Empty;
		return s_Empty.end();
	}

	// ---------------------------------------------------------- path helpers
	const json &json::Get(std::string_view Path) const
	{
		if(Path.empty())
			return *this;

		const json *pCurrent = this;
		std::size_t Start = 0;
		while(true)
		{
			std::size_t End = Path.find('.', Start);
			if(End == std::string_view::npos)
				End = Path.size();
			const std::string_view Segment = Path.substr(Start, End - Start);

			if(pCurrent->is_object())
			{
				const json *pFound = std::get<json_object>(pCurrent->m_Value).Find(Segment);
				if(!pFound)
					return NullValue();
				pCurrent = pFound;
			}
			else if(pCurrent->is_array())
			{
				std::size_t Index = 0;
				if(!ParseIndex(Segment, Index))
					return NullValue();
				const array_type &Elements = std::get<array_type>(pCurrent->m_Value);
				if(Index >= Elements.size())
					return NullValue();
				pCurrent = &Elements[Index];
			}
			else
			{
				return NullValue();
			}

			if(End == Path.size())
				break;
			Start = End + 1;
		}
		return *pCurrent;
	}

	void json::Set(std::string_view Path, json Value)
	{
		if(Path.empty())
		{
			*this = std::move(Value);
			return;
		}
		SetPath(*this, Path, std::move(Value));
	}

	void json::SetPath(json &Node, std::string_view Path, json Value)
	{
		const std::size_t Dot = Path.find('.');
		const std::string_view Segment = Dot == std::string_view::npos ? Path : Path.substr(0, Dot);
		const bool bLast = Dot == std::string_view::npos;
		const std::string_view Rest = bLast ? std::string_view() : Path.substr(Dot + 1);

		std::size_t Index = 0;
		const bool bIndex = ParseIndex(Segment, Index);

		// an object always takes the segment as a key, even a numeric one, so
		// that a member named "0" is reachable; only an array (or a value that
		// has to become one) is indexed
		if(Node.is_object())
		{
			json &Child = std::get<json_object>(Node.m_Value).Insert(Segment);
			if(bLast)
				Child = std::move(Value);
			else
				SetPath(Child, Rest, std::move(Value));
			return;
		}

		if(Node.is_array())
		{
			if(!bIndex)
			{
				// a key on an array replaces it with an object
				Node = json::object();
				json &Child = std::get<json_object>(Node.m_Value).Insert(Segment);
				if(bLast)
					Child = std::move(Value);
				else
					SetPath(Child, Rest, std::move(Value));
				return;
			}
			array_type &Elements = std::get<array_type>(Node.m_Value);
			if(Index >= Elements.size())
				Elements.resize(Index + 1);
			if(bLast)
				Elements[Index] = std::move(Value);
			else
				SetPath(Elements[Index], Rest, std::move(Value));
			return;
		}

		// null or a scalar: the segment decides what the value becomes
		if(bIndex)
		{
			Node = json::array();
			array_type &Elements = std::get<array_type>(Node.m_Value);
			Elements.resize(Index + 1);
			if(bLast)
				Elements[Index] = std::move(Value);
			else
				SetPath(Elements[Index], Rest, std::move(Value));
			return;
		}

		Node = json::object();
		json &Child = std::get<json_object>(Node.m_Value).Insert(Segment);
		if(bLast)
			Child = std::move(Value);
		else
			SetPath(Child, Rest, std::move(Value));
	}
} // namespace base
