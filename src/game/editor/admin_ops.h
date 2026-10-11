#ifndef GAME_EDITOR_ADMIN_OPS_H
#define GAME_EDITOR_ADMIN_OPS_H

/*
	The method table of the administrative interface.

	One entry per method: its name, the parameters it takes, and the function
	that runs it. `agent.describe` walks this table, and so does the dispatch,
	so the documentation an agent receives is generated from the same data the
	dispatcher uses and cannot go stale.
*/

#include <functional>
#include <string>
#include <vector>

#include "base/json.h"

namespace game
{
	class CEditor;

	namespace admin
	{
		struct CResponse;

		namespace ops
		{
			struct CContext
			{
				CEditor &m_Editor;
				const base::json &m_Params;
				CResponse &m_Response;

				// a parameter with a default, so a method body reads as the
				// one line it is
				int Int(const char *pName, int Default = 0) const
				{
					const base::json &Value = m_Params[pName];
					return Value.is_number() ? static_cast<int>(Value.as_int(Default)) : Default;
				}
				bool Bool(const char *pName, bool Default = false) const
				{
					const base::json &Value = m_Params[pName];
					return Value.is_bool() ? Value.as_bool(Default) : Default;
				}
				std::string Str(const char *pName, const std::string &Default = {}) const
				{
					const base::json &Value = m_Params[pName];
					return Value.is_string() ? Value.as_string() : Default;
				}
				const base::json &Json(const char *pName) const { return m_Params[pName]; }
				bool Has(const char *pName) const { return m_Params.has(pName); }

				// the way a handler reports a failure
				bool Fail(const std::string &Message) const;
				// the way a handler reports a success with a payload
				bool Ok(base::json Result) const;
			};

			struct CDescriptor
			{
				const char *m_pName;
				// one line per parameter, as it appears in agent.describe
				const char *m_pParams;
				const char *m_pDescription;
				std::function<bool(const CContext &)> m_pHandler;
			};

			const std::vector<CDescriptor> &Table();
			const CDescriptor *Find(const std::string &Method);
			base::json Describe();
		} // namespace ops
	} // namespace admin
} // namespace game

#endif // GAME_EDITOR_ADMIN_OPS_H
