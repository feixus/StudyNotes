#include "stdafx.h"
#include "Graphics.h"

Graphics* m_pGraphics;
HWND m_Hwnd = nullptr;
bool mMaximized = false;
bool mResizing = false;
bool mMinimized = false;

LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	std::cout << "hello dx12" << std::endl;

	int width = 1024, height = 512;

	WNDCLASSW wc;

	wc.hInstance = GetModuleHandle(nullptr);
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hIcon = 0;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpfnWndProc = WndProc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpszClassName = L"wndClass";
	wc.lpszMenuName = nullptr;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (!RegisterClass(&wc))
	{
		return 0;
	}

	int displayWidth = GetSystemMetrics(SM_CXSCREEN);
	int displayHeight = GetSystemMetrics(SM_CYSCREEN);

	DWORD windowStyle = WS_OVERLAPPEDWINDOW;

	RECT windowRec = { 0, 0, (LONG)width, (LONG)height };
	AdjustWindowRect(&windowRec, windowStyle, false);

	uint32_t windowWidth = windowRec.right - windowRec.left;
	uint32_t windowHeight = windowRec.bottom - windowRec.top;

	int x = (displayWidth - windowWidth) / 2;
	int y = (displayHeight - windowHeight) / 2;

	m_Hwnd = CreateWindowEx(0,
		L"wndClass",
		L"Hello World DX12",
		windowStyle,
		x,
		y,
		windowWidth,
		windowHeight,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		nullptr
	);

	if (!m_Hwnd) return 0;

	ShowWindow(m_Hwnd, SW_SHOWDEFAULT);

	if (!UpdateWindow(m_Hwnd)) return 0;

	GameTimer::Start();
	GameTimer::Reset();

	m_pGraphics = new Graphics(width, height);
	m_pGraphics->Initialize(m_Hwnd);

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		GameTimer::Tick();

		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			m_pGraphics->Update();
		}
	}

	m_pGraphics->Shutdown();

	GameTimer::Stop();

	return 0;
}

LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		// resize the window
	case WM_SIZE:
	{
		uint32_t windowWidth = LOWORD(lParam);
		uint32_t windowHeight = HIWORD(lParam);
		if (m_pGraphics && m_pGraphics->GetDevice())
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mMinimized = false;
				mMaximized = true;
				m_pGraphics->OnResize(windowWidth, windowHeight);
			}
			else if (wParam == SIZE_RESTORED)
			{
				// restoring from minimized state
				if (mMinimized)
				{
					mMinimized = false;
					m_pGraphics->OnResize(windowWidth, windowHeight);
				}
				// restoring from maximized state
				else if (mMaximized)
				{
					mMaximized = false;
					m_pGraphics->OnResize(windowWidth, windowHeight);
				}
				else if (mResizing)
				{

				}
				else  // api call such as SetWindowPos/ mSwapchain->SetFullscreenState
				{
					m_pGraphics->OnResize(windowWidth, windowHeight);
				}
			}
		}
	}
		return 0;
	case WM_KEYUP:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}
