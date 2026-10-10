#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

/*
	Keyboard and mouse state, in the shape SandboxCmd uses: GLFW callbacks push
	into this object and the editor reads it once per frame. Key codes are GLFW
	key codes, exposed as plain ints so the interface does not need GLFW.
*/

#include <array>
#include <string>

#include "base/math.h"

namespace engine
{
	class CInput
	{
	public:
		enum class mouse_button
		{
			left = 0,
			right = 1,
			middle = 2,
		};

		void OnKey(int Key, int Scancode, int Action, int Mods);
		void OnChar(unsigned int Codepoint);
		void OnMouseButton(int Button, int Action, int Mods);
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
		static constexpr int MaxKeys = 512;
		std::array<bool, MaxKeys> m_aKeys{};
		std::array<bool, MaxKeys> m_aKeysPressed{};
		std::array<bool, MaxKeys> m_aKeysReleased{};
		std::array<bool, 8> m_aMouse{};
		std::array<bool, 8> m_aMousePressed{};
		std::array<bool, 8> m_aMouseReleased{};

		base::vec2 m_MousePosition;
		base::vec2 m_PreviousMousePosition;
		float m_Scroll = 0.0f;
		std::string m_TypedText;
	};
} // namespace engine

#endif // ENGINE_INPUT_H
