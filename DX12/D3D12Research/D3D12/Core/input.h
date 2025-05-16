#pragma once

enum KeyState
{
	None,
	Down,
	Pressed,
	DownAndPressed = Down | Pressed,
};

class Input
{
public:
	static Input& Instance();

	void SetWindow(HWND window);

	void Update();
	void UpdateKey(uint32_t keyCode, bool isDown);
	void UpdateMouseKey(uint32_t keyCode, bool isDown);

	bool IsKeyDown(uint32_t keyCode);
	bool IsKeyPressed(uint32_t keyCode);

	bool IsMouseDown(uint32_t keyCode);
	bool IsMousePressed(uint32_t keyCode);

	Vector2 GetMousePosition() const;
	Vector2 GetMouseDelta() const;

private:
	void UpdateMousePosition();

	std::array<KeyState, 256> m_KeyStates{};
	std::array<KeyState, 5> m_MouseStates{};

	Input() = default;

	HWND m_pWindow{};
	Vector2 m_LastMousePosition;
	Vector2 m_CurrentMousePosition;
};
