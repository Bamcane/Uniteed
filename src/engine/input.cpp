#include "input.h"

#include <SDL3/SDL.h>

namespace engine
{
	CInput::CKeyState *CInput::Find(int Key)
	{
		for(CKeyState &State : m_Keys)
			if(State.m_Key == Key)
				return &State;
		return nullptr;
	}

	const CInput::CKeyState *CInput::Find(int Key) const
	{
		for(const CKeyState &State : m_Keys)
			if(State.m_Key == Key)
				return &State;
		return nullptr;
	}

	void CInput::OnKey(int Key, int Action, int Mods)
	{
		m_Modifiers = Mods;
		if(Key == 0)
			return;
		if(Action == action_press)
		{
			CKeyState *pState = Find(Key);
			if(pState == nullptr)
			{
				m_Keys.push_back(CKeyState{Key, true, true, false});
				return;
			}
			// a repeated key down keeps the press flag from the first one
			if(!pState->m_bDown)
				pState->m_bPressed = true;
			pState->m_bDown = true;
			pState->m_bReleased = false;
		}
		else if(Action == action_release)
		{
			if(CKeyState *pState = Find(Key))
			{
				pState->m_bDown = false;
				pState->m_bReleased = true;
			}
		}
	}

	void CInput::OnText(const char *pText)
	{
		if(pText == nullptr)
			return;
		m_TypedText += pText;
	}

	void CInput::OnMouseButton(int Button, int Action)
	{
		if(Button < 0 || Button >= 8)
			return;
		if(Action == action_press)
		{
			if(!m_aMouse[static_cast<std::size_t>(Button)])
				m_aMousePressed[static_cast<std::size_t>(Button)] = true;
			m_aMouse[static_cast<std::size_t>(Button)] = true;
		}
		else if(Action == action_release)
		{
			m_aMouse[static_cast<std::size_t>(Button)] = false;
			m_aMouseReleased[static_cast<std::size_t>(Button)] = true;
		}
	}

	void CInput::OnCursorPos(double X, double Y)
	{
		m_MousePosition = base::vec2(static_cast<float>(X), static_cast<float>(Y));
	}

	void CInput::OnScroll(double XOffset, double YOffset)
	{
		(void)XOffset;
		m_Scroll += static_cast<float>(YOffset);
	}

	void CInput::OnFocusLost()
	{
		for(CKeyState &State : m_Keys)
		{
			State.m_bDown = false;
			State.m_bPressed = false;
			State.m_bReleased = false;
		}
		m_aMouse.fill(false);
		m_Modifiers = 0;
	}

	void CInput::NewFrame()
	{
		m_TypedText.clear();
		m_Scroll = 0.0f;
	}

	void CInput::EndFrame()
	{
		for(CKeyState &State : m_Keys)
		{
			State.m_bPressed = false;
			State.m_bReleased = false;
		}
		m_aMousePressed.fill(false);
		m_aMouseReleased.fill(false);
		m_PreviousMousePosition = m_MousePosition;
	}

	bool CInput::KeyDown(int Key) const
	{
		const CKeyState *pState = Find(Key);
		return pState != nullptr && pState->m_bDown;
	}

	bool CInput::KeyPressed(int Key) const
	{
		const CKeyState *pState = Find(Key);
		return pState != nullptr && pState->m_bPressed;
	}

	bool CInput::KeyReleased(int Key) const
	{
		const CKeyState *pState = Find(Key);
		return pState != nullptr && pState->m_bReleased;
	}

	bool CInput::ModifierDown(int Modifier) const { return (m_Modifiers & Modifier) != 0; }

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

	bool CInput::BackspacePressed() const { return KeyPressed(SDLK_BACKSPACE); }
} // namespace engine
