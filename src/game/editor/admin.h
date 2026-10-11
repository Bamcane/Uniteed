#ifndef GAME_EDITOR_ADMIN_H
#define GAME_EDITOR_ADMIN_H

/*
	The administrative interface of the editor: one JSON request in, one JSON
	response out, over every operation the editor can perform without a mouse.

	This is what makes Uniteed usable as an agent tool. The window calls none of
	it, but the command line and the `--agent` mode do, and they call the *same*
	CEditor the window drives, so `tiles.set` from an agent and a click in the
	canvas cannot disagree about what an edit means.

	A request is

		{ "method": "tiles.set", "params": { ... } }

	and a response is

		{ "ok": true,  "method": "tiles.set", "result": { ... } }
		{ "ok": false, "method": "tiles.set", "error": "..." }

	`agent.describe` returns the whole method list with the parameters each one
	accepts, so a caller never has to be told out of band. Every method is
	documented in docs/AGENT.md, which `--agent-describe` prints.
*/

#include <string>
#include <vector>

#include "base/json.h"

namespace game
{
	class CEditor;

	namespace admin
	{
		// the running request: what remains of one line of input
		struct CRequest
		{
			std::string m_Method;
			base::json m_Params;   // an object, empty when the request had none
			base::json m_Id;       // echoed back, so a client can match an answer
		};

		struct CResponse
		{
			bool m_bOk = false;
			base::json m_Result; // an object, even when empty
			std::string m_Error;
		};

		/*
			The administrative session. It owns the editor, so its state survives
			from one request to the next, which is what lets an agent open a
			project, make a dozen edits and export without the editor state
			being rebuilt in between.
		*/
		class CSession
		{
		public:
			CSession();
			~CSession();

			CEditor &Editor() { return *m_pEditor; }
			const CEditor &Editor() const { return *m_pEditor; }

			// false when the method is unknown, which is an error response
			bool Has(const std::string &Method) const;
			// runs one method against the session's editor
			CResponse Run(const std::string &Method, const base::json &Params);

			// one request document, the shape --agent reads on a line
			base::json Handle(const base::json &Request);

			// the method catalogue as a JSON array
			static base::json Describe();

			// the editor this session wrapped, if it did not create one: the
			// window build hands its own editor in so both share the state
			void Adopt(CEditor *pEditor);

		private:
			CEditor *m_pEditor = nullptr;
			bool m_bOwnsEditor = false;
		};

		// ------------------------------------------------------------ snapshots
		// a map as JSON: the shape `map.get`, `map.dump` and every response that
		// mentions a layer use
		base::json MapToJson(const CEditor &Editor, bool bIncludeTiles);
		// one layer, without the tile array unless asked
		base::json LayerToJson(const CEditor &Editor, int Group, int Layer, bool bIncludeTiles);
		// the project's metadata and its editor state, without the map
		base::json ProjectToJson(const CEditor &Editor);
		// the current selection, which is what most edits are relative to
		base::json SelectionToJson(const CEditor &Editor);

	} // namespace admin
} // namespace game

#endif // GAME_EDITOR_ADMIN_H
