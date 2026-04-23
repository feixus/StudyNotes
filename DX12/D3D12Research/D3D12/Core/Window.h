#pragma once

class Window
{
public:
    static constexpr const char* WINDOW_CLASS_NAME = "WndClass";

    Window(const char* pTitle, uint32_t width, uint32_t height);
    ~Window();

    bool PollMessages();

    void SetWindowTitle(const char* pTitle);

    HWND GetNativeWindow() const { return m_Window; }
    IntVector2 GetRect() const { return IntVector2(m_DisplayWidth, m_DisplayHeight); }

    DECLARE_MULTICAST_DELEGATE(OnFocusChangedDelegate, bool);
    OnFocusChangedDelegate OnFocusChanged;

    DECLARE_MULTICAST_DELEGATE(OnResizeDelegate, uint32_t, uint32_t);
    OnResizeDelegate OnResize;

    DECLARE_MULTICAST_DELEGATE(OnCharInputDelegate, uint32_t);
    OnCharInputDelegate OnCharInput;

    DECLARE_MULTICAST_DELEGATE(OnKeyInputDelegate, uint32_t, bool);
    OnKeyInputDelegate OnKeyInput;

    DECLARE_MULTICAST_DELEGATE(OnMouseInputDelegate, uint32_t, bool);
    OnMouseInputDelegate OnMouseKeyInput;

    DECLARE_MULTICAST_DELEGATE(OnMouseMoveDelegate, uint32_t, uint32_t);
    OnMouseMoveDelegate OnMouseMove;

    DECLARE_MULTICAST_DELEGATE(OnMouseScrollDelegate, float);
    OnMouseScrollDelegate OnMouseScroll;

private:
    static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND m_Window{nullptr};
    bool m_Minimized{false};
    bool m_Maximized{false};
    int m_DisplayWidth{0};
    int m_DisplayHeight{0};
    bool m_IsResizing{false};
};