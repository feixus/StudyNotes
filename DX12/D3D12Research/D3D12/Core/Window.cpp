#include "stdafx.h"
#include "Window.h"

Window::Window(uint32_t width, uint32_t height)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.hInstance = GetModuleHandleA(0);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpfnWndProc = WndProcStatic;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = "WndClass";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    check(RegisterClassExA(&wc));

    int displayWidth = GetSystemMetrics(SM_CXSCREEN);
    int displayHeight = GetSystemMetrics(SM_CYSCREEN);

    DWORD windowStyle = WS_OVERLAPPEDWINDOW;
    RECT windowRec = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRect(&windowRec, windowStyle, false);

    int x = (displayWidth - width) / 2;
    int y = (displayHeight - height) / 2;

	m_Window = CreateWindowExA(
        0,
		WINDOW_CLASS_NAME,
		"",
        windowStyle,
        x,
        y,
		windowRec.right - windowRec.left,
		windowRec.bottom - windowRec.top,
        nullptr,
        nullptr,
        GetModuleHandleA(0),
        this
    );

    check(m_Window);
    ShowWindow(m_Window, SW_SHOWDEFAULT);
    UpdateWindow(m_Window);
}

Window::~Window()
{
    CloseWindow(m_Window);
    UnregisterClassA(WINDOW_CLASS_NAME, GetModuleHandleA(0));
}

bool Window::PollMessages()
{
    MSG msg{};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);

        if (msg.message == WM_QUIT)
        {
            return false;
        }
    }

    POINT p;
    ::GetCursorPos(&p);
    ScreenToClient(m_Window, &p);
    OnMouseMove.Broadcast(p.x, p.y);

    return true;
}

void Window::SetTitle(const char *pTitle)
{
    SetWindowTextA(m_Window, pTitle);
}

LRESULT Window::WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window* pThis = nullptr;
    if (message == WM_NCCREATE)
    {
        pThis = static_cast<Window*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtrA(hWnd, GWLP_USERDATA));
        if (pThis)
        {
            return pThis->WndProc(hWnd, message, wParam, lParam);
        }
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT Window::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   switch (message)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    case WM_ACTIVATE:
    {
        OnFocusChanged.Broadcast(LOWORD(wParam) != WA_INACTIVE);
        break;
    }
    case WM_SIZE:
    {
        int newWidth = LOWORD(lParam);
        int newHeight = HIWORD(lParam);
        bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
        bool shouldResize = false;

        m_DisplayWidth = LOWORD(lParam);
        m_DisplayHeight = HIWORD(lParam);
        
        if (wParam == SIZE_MINIMIZED)
        {
            OnFocusChanged.Broadcast(false);
            m_Minimized = true;
            m_Maximized = false;
        }
        else if (wParam == SIZE_MAXIMIZED)
        {
            OnFocusChanged.Broadcast(true);
            m_Minimized = false;
            m_Maximized = true;
            shouldResize = true;
        }
        else if (wParam == SIZE_RESTORED)
        {
            if (m_Minimized)
            {
                OnFocusChanged.Broadcast(true);
                m_Minimized = false;
                shouldResize = true;
            }
            else if (m_Maximized)
            {
                OnFocusChanged.Broadcast(true);
                m_Maximized = false;
                shouldResize = true;
            }
            else if (!m_IsResizing) // api call such as SetWindowPos or mSwapchain->SetFullscreenState
            {
                shouldResize = true;
            }
        }

        if (resized && shouldResize)
        {
            OnResize.Broadcast(m_DisplayWidth, m_DisplayHeight);
        }
        
        break;
    }
    case WM_MOUSEWHEEL:
    {
        float mouseWheel = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        OnMouseScroll.Broadcast(mouseWheel);
        return 0;
    }
    case WM_KEYUP:
    {
        OnKeyInput.Broadcast((uint32_t)wParam, false);
        break;	
    }
    case WM_KEYDOWN:
    {
        OnKeyInput.Broadcast((uint32_t)wParam, true);
        break;	
    }
    case WM_CHAR:
    {
        if (wParam < 256)
        {
            OnCharInput.Broadcast((uint32_t)wParam);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    {
        OnMouseKeyInput.Broadcast(VK_LBUTTON, true);
        break;	
    }
    case WM_LBUTTONUP:
    {
        OnMouseKeyInput.Broadcast(VK_LBUTTON, false);
        break;	
    }
    case WM_MBUTTONDOWN:
    {
        OnMouseKeyInput.Broadcast(VK_MBUTTON, true);
        break;	
    }
    case WM_MBUTTONUP:
    {
        OnMouseKeyInput.Broadcast(VK_MBUTTON, false);
        break;	
    }
    case WM_RBUTTONDOWN:
    {
        OnMouseKeyInput.Broadcast(VK_RBUTTON, true);
        break;	
    }
    case WM_RBUTTONUP:
    {
        OnMouseKeyInput.Broadcast(VK_RBUTTON, false);
        break;	
    }
    case WM_ENTERSIZEMOVE: // resizing or moving window with the mouse
    {
        OnFocusChanged.Broadcast(false);
        m_IsResizing = true;
        break;	
    }
    case WM_EXITSIZEMOVE:
    {
        OnFocusChanged.Broadcast(true);
        RECT rect;
        GetClientRect(hWnd, &rect);
        int newWidth = rect.right - rect.left;
        int newHeight = rect.bottom - rect.top;
        bool resized = newWidth != m_DisplayWidth || newHeight != m_DisplayHeight;
        if (resized)
        {
            m_DisplayWidth = newWidth;
            m_DisplayHeight = newHeight;
            OnResize.Broadcast(m_DisplayWidth, m_DisplayHeight);
        }
        m_IsResizing = false;
        break;
    }
    }
    return DefWindowProcA(hWnd, message, wParam, lParam);
}
