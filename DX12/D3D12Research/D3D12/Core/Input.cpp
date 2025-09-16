#include "stdafx.h"
#include "Input.h"

Input& Input::Instance()
{
	static Input i{};
	return i;
}

void Input::SetWindow(HWND window)
{
	m_pWindow = window;
}

void Input::Update()
{
	for (size_t i = 0; i < m_KeyStates.size(); i++)
	{
		m_KeyStates[i] = (KeyState)(m_KeyStates[i] & KeyState::Down);
	}
	
	m_MouseWheel = 0;
	m_MouseDelta = Vector2(0, 0);

	POINT p;
	::GetCursorPos(&p);
	::ScreenToClient(m_pWindow, &p);
	UpdateMousePosition((float)p.x, (float)p.y);
}

void Input::UpdateKey(uint32_t keyCode, bool isDown)
{
	m_KeyStates[keyCode] = isDown ? KeyState::DownAndPressed : KeyState::None;
}

void Input::UpdateMouseKey(uint32_t keyCode, bool isDown)
{
	m_MouseStates[keyCode] = isDown ? KeyState::DownAndPressed : KeyState::None;
}

void Input::UpdateMouseWheel(float mouseWheel)
{
	m_MouseWheel = mouseWheel;
}

bool Input::IsKeyDown(uint32_t keyCode)
{
	return m_KeyStates[keyCode] > KeyState::None;
}

bool Input::IsKeyPressed(uint32_t keyCode)
{
	return (m_KeyStates[keyCode] & KeyState::Pressed) == KeyState::Pressed;
}

bool Input::IsMouseDown(uint32_t keyCode)
{
	return m_MouseStates[keyCode] > KeyState::None;
}

bool Input::IsMousePressed(uint32_t keyCode)
{
	return (m_MouseStates[keyCode] & KeyState::Pressed) == KeyState::Pressed;
}

Vector2 Input::GetMousePosition() const
{
	return m_CurrentMousePosition;
}

Vector2 Input::GetMouseDelta() const
{
	return m_MouseDelta;
}

void Input::UpdateMousePosition(float x, float y)
{
	m_MouseDelta = Vector2(x, y) - m_CurrentMousePosition;
	m_CurrentMousePosition = Vector2(x, y);	
}

void Input::UpdateMouseDelta(float x, float y)
{
	m_MouseDelta = Vector2(x, y);
}
