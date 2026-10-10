#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

/*
	Keyboard and mouse state. The window events push into this object and the
	editor reads it once per frame. Key codes are the SDL key symbols
	(SDLK_*), exposed as plain ints so the interface does not need SDL.
*/

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "base/math.h"

namespace engine
{
	class CInput
	{
	public:
		enum class mouse_button
		{
			left = 1,
			middle = 2,
			right = 3,
			x1 = 4,
			x2 = 5,
		};

		// the action values SDL uses, passed straight through
		static constexpr int action_release = 0;
		static constexpr int action_press = 1;

		// the modifier bits, matching SDL_Keymod
		static constexpr int mod_shift = 0x0003;
		static constexpr int mod_ctrl = 0x00C0;
		static constexpr int mod_alt = 0x0300;
		static constexpr int mod_gui = 0x0C00;

		void OnKey(int Key, int Action, int Mods);
		void OnText(const char *pText);
		void OnMouseButton(int Button, int Action);
		void OnCursorPos(double X, double Y);
		void OnScroll(double XOffset, double YOffset);
		void OnFocusLost();

		// call once per frame after the events have been processed
		void NewFrame();
		// call once per frame after the editor has read the state
		void EndFrame();

		bool KeyDown(int Key) const;
		bool KeyPressed(int Key) const;  // went down this frame
		bool KeyReleased(int Key) const; // went up this frame
		bool ModifierDown(int Modifier) const;
		bool MouseDown(mouse_button Button) const;
		bool MousePressed(mouse_button Button) const;
		bool MouseReleased(mouse_button Button) const;

		base::vec2 MousePosition() const { return m_MousePosition; }
		base::vec2 MouseDelta() const { return m_MousePosition - m_PreviousMousePosition; }
		float Scroll() const { return m_Scroll; }

		// text typed this frame, for the name fields
		const std::string &TypedText() const { return m_TypedText; }
		bool BackspacePressed() const;

	private:
		// only the keys the editor asks about are tracked, so the state is a
		// small list rather than an array indexed by a sparse keycode
		struct CKeyState
		{
			int m_Key = 0;
			bool m_bDown = false;
			bool m_bPressed = false;
			bool m_bReleased = false;
		};

		CKeyState *Find(int Key);
		const CKeyState *Find(int Key) const;

		std::vector<CKeyState> m_Keys;
		std::array<bool, 8> m_aMouse{};
		std::array<bool, 8> m_aMousePressed{};
		std::array<bool, 8> m_aMouseReleased{};

		int m_Modifiers = 0;
		base::vec2 m_MousePosition;
		base::vec2 m_PreviousMousePosition;
		float m_Scroll = 0.0f;
		std::string m_TypedText;
	};
} // namespace engine

#endif // ENGINE_INPUT_H
