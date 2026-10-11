#ifndef BASE_JSON_H
#define BASE_JSON_H

/*
	A small JSON value with a parser and a serializer, used by the editor for
	settings, the .unitwp package manifest and every other sidecar file.

	The layer is deliberately self contained: no external dependency, no SDL,
	no GUI header, no exception in the format path. Parsing reports a failure
	through a std::string error that always carries the byte offset, and the
	typed reads fall back to a default instead of aborting. The same module is
	compiled into the window build and into the headless command line.

	The value is a variant of the six JSON types, so an integer stays an
	integer and is written back without a decimal point. Objects keep their
	members in insertion order (that is the deterministic dump order) and keep
	a hash index next to the ordered arrays so a lookup by key stays O(1).
*/

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace base
{
	class json;

	/*
		The storage of a JSON object: the keys and the values in insertion
		order, plus a hash index from key to slot. Keeping the index beside the
		order is what makes both a stable dump and a fast member lookup
		possible. Erasing a member shifts the later slots down and reindexes
		them, which is rare enough to not matter.
	*/
	struct json_object
	{
		// transparent hashing, so that a lookup with the std::string_view that
		// the caller already has does not build a temporary std::string
		struct string_hash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view Key) const { return std::hash<std::string_view>{}(Key); }
		};

		std::vector<std::string> m_vKeys;
		std::vector<json> m_vValues;
		std::unordered_map<std::string, std::size_t, string_hash, std::equal_to<>> m_Index;

		std::size_t size() const { return m_vKeys.size(); }

		// nullptr when the key is not there
		const json *Find(std::string_view Key) const;
		// the value of the key, inserting a null member when it is missing
		json &Insert(std::string_view Key);
		bool Erase(std::string_view Key);
	};

	class json
	{
	public:
		enum class json_type
		{
			null,
			boolean,
			number,
			string,
			array,
			object,
		};

		using array_type = std::vector<json>;

	private:
		// the alternative order is internal; type() maps it onto json_type,
		// and an integer is kept apart from a double so that dump() can write
		// it back without a decimal point
		std::variant<std::nullptr_t, bool, long long, double, std::string, array_type, json_object> m_Value;

		void DumpTo(std::string &Out, int Indent, int Depth) const;
		static void SetPath(json &Node, std::string_view Path, json Value);
		static bool ParseInto(std::string_view Text, json &Out, std::string *pError);

	public:
		// ------------------------------------------------ construction
		json(); // null
		json(std::nullptr_t);
		json(bool Value);
		json(int Value);
		json(long long Value);
		json(double Value);
		json(const char *pValue);
		json(std::string Value);

		static json object(); // empty object
		static json array();  // empty array

		/*
			Parses a document. On failure the result is null and *pError, when
			given, carries a message with the byte offset, for example
			"json: unexpected character '}' at offset 12". Trailing bytes after
			the top level value are an error, and nesting deeper than 200
			levels is rejected before it can exhaust the C stack.
		*/
		static json parse(std::string_view Text, std::string *pError = nullptr);
		// the fallback is returned whenever the text cannot be parsed; a
		// document that legitimately parses to null still yields null
		static json parse_or(std::string_view Text, const json &Fallback, std::string *pError = nullptr);

		// ------------------------------------------------------ inspection
		json_type type() const;
		bool is_null() const;
		bool is_bool() const;
		bool is_number() const;
		bool is_string() const;
		bool is_array() const;
		bool is_object() const;
		const char *type_name() const;

		// ------------------------------------------- typed reads with defaults
		// all of them are total: a value of the wrong type yields Default
		bool as_bool(bool Default = false) const;
		long long as_int(long long Default = 0) const;
		double as_double(double Default = 0.0) const;
		std::string as_string(std::string Default = {}) const;

		// Indent < 0 is compact, Indent >= 0 is pretty printed with that many
		// spaces per level
		std::string dump(int Indent = -1) const;

		// ------------------------------------------------- object access
		// a missing member reads as null; the non-const form inserts a null
		// member when the key is missing (and turns a null value into an
		// object, so `json J; J["a"] = 1;` works)
		const json &operator[](std::string_view Key) const;
		json &operator[](std::string_view Key);

		bool has(std::string_view Key) const;
		bool erase(std::string_view Key);
		std::vector<std::string> keys() const; // insertion order
		std::size_t size() const;              // members or elements, 0 otherwise

		// -------------------------------------------------- array access
		// out of range (or not an array) reads as null; writing to the shared
		// null that is returned for a miss is dropped
		const json &at(std::size_t Index) const;
		json &at(std::size_t Index);

		// appends to an array; a null becomes an array first, anything else
		// is left untouched
		void push_back(json Value);

		// resets the value to null, dropping whatever it held
		void clear();

		// ---------------------------------------------------- iteration
		// the elements of an array in order. A value that is not an array
		// yields an empty range; the members of an object are reached through
		// keys() and operator[] instead, because their value type differs.
		std::vector<json>::iterator begin();
		std::vector<json>::iterator end();
		std::vector<json>::const_iterator begin() const;
		std::vector<json>::const_iterator end() const;

		// ------------------------------------------------- path helpers
		/*
			Path style lookup, used heavily by the editor: "a.b.0.c" walks
			objects by key and arrays by index and returns a null value when
			anything along the way is missing or has the wrong type. An empty
			path is the value itself. A segment is taken as a key while the
			current value is an object and as an index while it is an array, so
			keys containing a '.' cannot be addressed.
		*/
		const json &Get(std::string_view Path) const;
		/*
			Writes by the same path. Missing intermediate steps are created:
			a numeric segment (all digits) makes an array, any other segment
			makes an object, and an array is grown with nulls as needed. An
			empty path replaces the value itself.
		*/
		void Set(std::string_view Path, json Value);
	};
} // namespace base

#endif // BASE_JSON_H
