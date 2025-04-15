#include "WindowsApplication.hpp"
#include <tchar.h>

using namespace My;

int My::WindowsApplication::Initialize()
{
    int result;

    result = BaseApplication::Initialize();
    if (result != 0) exit(result);

    HINSTANCE hInstance = GetModuleHandle(NULL);

    HWND hWnd;
    WNDCLASSEX wc;

    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = _T("GameEngine");

    RegisterClassEx(&wc);

    hWnd = CreateWindowExW(0,
                            L"GameEngine",
                            m_Config.appName,
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            m_Config.screenWidth, m_Config.screenHeight,
                            NULL,
                            NULL,
                            hInstance,
                            this);

    ShowWindow(hWnd, SW_SHOW);

    m_hWnd = hWnd;

    return result;                            
}

void My::WindowsApplication::Finalize()
{

}

void My::WindowsApplication::Tick()
{
    MSG msg;

    // instead of GetMessage here, should not block the thread at anywhere except the engine execution driver module
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        // translate keystroke messages into the right format
        TranslateMessage(&msg);

        // send the message to the WindowProc function
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK My::WindowsApplication::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowsApplication* pThis;
    if (message == WM_NCCREATE)
    {
        pThis = reinterpret_cast<WindowsApplication*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);

        SetLastError(0);
        if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
        {
            if (GetLastError() != 0)
                return -1;
        }
    }
    else
    {
        pThis = reinterpret_cast<WindowsApplication*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    switch (message)
    {
    case WM_PAINT:
        // replace this part with Rendering Module
        {
            if (pThis)
            pThis->OnDraw();
        } break;
    case WM_KEYDOWN:
        {
            m_bQuit = true;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        BaseApplication::m_bQuit = true;
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}


