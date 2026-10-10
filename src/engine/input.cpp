#include "input.h"

#include <GLFW/glfw3.h>

namespace engine
{
	void CInput::OnKey(int Key, int Scancode, int Action, int Mods)
	{
		(void)Scancode;
		(void)Mods;
		if(Key < 0 || Key >= MaxKeys)
			return;
		if(Action == GLFW_PRESS)
		{
			if(!m_aKeys[static_cast<std::size_t>(Key)])
				m_aKeysPressed[static_cast<std::size_t>(Key)] = true;
			m_aKeys[static_cast<std::size_t>(Key)] = true;
		}
		else if(Action == GLFW_RELEASE)
		{
			m_aKeys[static_cast<std::size_t>(Key)] = false;
			m_aKeysReleased[static_cast<std::size_t>(Key)] = true;
		}
	}

	void CInput::OnChar(unsigned int Codepoint)
	{
		if(Codepoint >= 32 && Codepoint < 127)
			m_TypedText.push_back(static_cast<char>(Codepoint));
	}

	void CInput::OnMouseButton(int Button, int Action, int Mods)
	{
		(void)Mods;
		if(Button < 0 || Button >= 8)
			return;
		if(Action == GLFW_PRESS)
		{
			if(!m_aMouse[static_cast<std::size_t>(Button)])
				m_aMousePressed[static_cast<std::size_t>(Button)] = true;
			m_aMouse[static_cast<std::size_t>(Button)] = true;
		}
		else if(Action == GLFW_RELEASE)
		{
			m_aMouse[static_cast<std::size_t>(Button)] = false;
			m_aMouseReleased[static_cast<std::size_t>(Button)] = true;
		}
	}

	void CInput::OnCursorPos(double X, double Y) { m_MousePosition = base::vec2(static_cast<float>(X), static_cast<float>(Y)); }
	void CInput::OnScroll(double XOffset, double YOffset)
	{
		(void)XOffset;
		m_Scroll += static_cast<float>(YOffset);
	}

	void CInput::OnFocusLost()
	{
		m_aKeys.fill(false);
		m_aMouse.fill(false);
	}

	void CInput::NewFrame()
	{
		m_TypedText.clear();
		m_Scroll = 0.0f;
	}

	void CInput::EndFrame()
	{
		m_aKeysPressed.fill(false);
		m_aKeysReleased.fill(false);
		m_aMousePressed.fill(false);
		m_aMouseReleased.fill(false);
		m_PreviousMousePosition = m_MousePosition;
	}

	bool CInput::KeyDown(int Key) const { return Key >= 0 && Key < MaxKeys && m_aKeys[static_cast<std::size_t>(Key)]; }
	bool CInput::KeyPressed(int Key) const { return Key >= 0 && Key < MaxKeys && m_aKeysPressed[static_cast<std::size_t>(Key)]; }
	bool CInput::KeyReleased(int Key) const { return Key >= 0 && Key < MaxKeys && m_aKeysReleased[static_cast<std::size_t>(Key)]; }

	bool CInput::ModifierDown(int Modifier) const
	{
		return KeyDown(GLFW_KEY_LEFT_CONTROL) || KeyDown(GLFW_KEY_RIGHT_CONTROL) ||
			KeyDown(GLFW_KEY_LEFT_SHIFT) || KeyDown(GLFW_KEY_RIGHT_SHIFT) ||
			KeyDown(GLFW_KEY_LEFT_ALT) || KeyDown(GLFW_KEY_RIGHT_ALT) ||
			KeyDown(GLFW_KEY_LEFT_SUPER) || KeyDown(GLFW_KEY_RIGHT_SUPER);
	}

	bool CInput::MouseDown(mouse_button Button) const
	{
		const int Index = static_cast<int>(Button);
		return Index >= 0 && Index < 8 && m_aMouse[static_cast<std::size_t>(Index)];
	}

	bool CInput::MousePressed(mouse_button Button) const
	{
		const int Index = static_cast<int>(Button);
		return Index >= 0 && Index < 8 && m_aMousePressed[static_cast<std::size_t>(Index)];
	}

	bool CInput::MouseReleased(mouse_button Button) const
	{
		const int Index = static_cast<int>(Button);
		return Index >= 0 && Index < 8 && m_aMouseReleased[static_cast<std::size_t>(Index)];
	}

	bool CInput::BackspacePressed() const { return KeyPressed(GLFW_KEY_BACKSPACE); }
} // namespace engine
